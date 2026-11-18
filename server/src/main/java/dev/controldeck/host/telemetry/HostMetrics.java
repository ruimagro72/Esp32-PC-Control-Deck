package dev.controldeck.host.telemetry;

import dev.controldeck.host.util.Values;

import java.io.IOException;
import java.nio.file.FileStore;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.LocalTime;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Linux host telemetry reader (/proc and /sys).
 *
 * Reading and computing are separate: {@link #read()} takes one set of counters,
 * {@link #compute(Reading, Reading)} turns two of them into the protocol-v3
 * {@code stats} frame. Policy: a value is either measured or null. Nothing is
 * estimated or filled with a placeholder.
 */
public final class HostMetrics {

    /** One set of raw counters and instantaneous readings. Persisted between processes as JSON. */
    public record Reading(
            double time,
            boolean supported,
            CpuTicks cpuTicks,
            Memory memory,
            Net net,
            Disk disk,
            Gpu gpu,
            Temps temps,
            int secondsSinceMidnight,
            String clock) {
    }

    public record CpuTicks(long total, long idle) {
    }

    public record Memory(double totalGb, double usedGb, double usagePercent) {
    }

    public record Rx(long rx, long tx) {
    }

    public record Net(String interfaceName, Map<String, Rx> counters) {
    }

    public record Disk(String device, String baseDevice, String mountPoint, Double usedPercent,
                       Double usedGb, Double totalGb, Long sectorsRead, Long sectorsWritten) {
    }

    /**
     * The chosen GPU at one moment. {@code driver}: the kernel driver ("i915", "xe", "amdgpu");
     * {@code requestedMhz}: the clock the driver asks for ({@code clockMhz} is the one it runs
     * at); {@code idleResidencyMs}: the GT's total time in RC6 (i915) or idle (xe), a counter;
     * {@code fanRpm}: the card's fan; {@code engines}: engine class ("render", "video", ...) to
     * percent busy since the previous reading, from the DRM clients (docs/03-SERVER.md),
     * null or empty when unknown; {@code interrupts}: the device's interrupts so far, summed
     * over the CPUs in /proc/interrupts, a counter.
     */
    public record Gpu(Double usagePercent, Double vramUsedGb, Double vramTotalGb, Integer clockMhz,
                      Double tempCelsius, Double powerWatts, Double energyUj, String driver,
                      Integer requestedMhz, Double idleResidencyMs, Integer fanRpm, Map<String, Double> engines,
                      Long interrupts) {
    }

    public record Temps(Double cpu, Double system, Double disk, Double gpu) {
    }

    private static final java.time.format.DateTimeFormatter DATE_FORMAT =
            java.time.format.DateTimeFormatter.ofPattern("dd/MM");
    private static final Pattern MEM_TOTAL = Pattern.compile("^MemTotal:\\s+(\\d+)\\s+kB");
    private static final Pattern MEM_AVAILABLE = Pattern.compile("^MemAvailable:\\s+(\\d+)\\s+kB");
    private static final Pattern MOUNT_ESCAPES = Pattern.compile("\\\\(040|011|134)");

    private final SysFs fs;
    private final String diskPath;
    private final HostSensors sensors;

    /**
     * @param root         prefix for /proc and /sys ("" on a real host)
     * @param diskPath     "auto" or a directory whose file system is reported
     * @param vramTotalGb  configured VRAM size for drivers that do not report one, or null
     */
    public HostMetrics(String root, String diskPath, Double vramTotalGb) {
        this(root, diskPath, vramTotalGb, System::nanoTime);
    }

    /** @param nanoClock the clock of the rates the sensors time themselves (GPU engines); a test sets it */
    public HostMetrics(String root, String diskPath, Double vramTotalGb, java.util.function.LongSupplier nanoClock) {
        this.fs = new SysFs(root);
        this.diskPath = (diskPath == null || diskPath.isEmpty() || "auto".equalsIgnoreCase(diskPath)) ? "/" : stripTrailingSlash(diskPath);
        this.sensors = new HostSensors(fs, vramTotalGb, nanoClock);
    }

    public static boolean platformSupportsLinux() {
        return System.getProperty("os.name", "").toLowerCase(Locale.ROOT).contains("linux");
    }

    public boolean isSupported() {
        return !fs.root().isEmpty() || platformSupportsLinux();
    }

    /** Raw counters and instantaneous readings at one point in time. */
    public Reading read() {
        double now = nowSeconds();
        if (!isSupported()) {
            return new Reading(now, false, null, null, null, null, null, null, 0, "");
        }

        HostMetrics.Gpu gpu = sensors.gpu();
        Disk disk = readDisk();
        Temps temps = sensors.temperatures(gpu == null ? null : gpu.tempCelsius(),
                disk == null ? null : disk.baseDevice());
        LocalTime local = LocalTime.now();

        return new Reading(
                now,
                true,
                readCpuTicks(),
                readMemory(),
                readNetwork(),
                disk,
                gpu,
                temps,
                local.getHour() * 3600 + local.getMinute() * 60 + local.getSecond(),
                String.format(Locale.ROOT, "%02d:%02d", local.getHour(), local.getMinute()));
    }

    /**
     * Build the protocol-v3 {@code stats} frame from two readings. {@code previous}
     * may be null, in which case every rate is null.
     */
    public Map<String, Object> compute(Reading previous, Reading current) {
        Map<String, Object> frame = new LinkedHashMap<>();
        LocalTime local = LocalTime.now();
        frame.put("type", "stats");
        for (String key : List.of("cpu", "ram", "ram_used_gb", "ram_total_gb", "gpu", "vram", "vram_used_gb",
                "vram_total_gb", "cpu_temp", "gpu_temp", "gpu_clock_mhz", "gpu_power_watts", "system_temp",
                "disk_temp", "disk_used", "disk_read_mbs", "disk_write_mbs", "net_down_mbps", "net_up_mbps")) {
            frame.put(key, null);
        }
        frame.put("clock", String.format(Locale.ROOT, "%02d:%02d", local.getHour(), local.getMinute()));
        frame.put("clock_seconds", local.getHour() * 3600 + local.getMinute() * 60 + local.getSecond());
        // The date as the Deck shows it when its clock is tapped (day/month).
        frame.put("date", java.time.LocalDate.now().format(DATE_FORMAT));

        if (current == null || !current.supported()) {
            return frame;
        }

        frame.put("clock", current.clock());
        frame.put("clock_seconds", current.secondsSinceMidnight());

        boolean havePrevious = previous != null && previous.supported();
        double elapsed = havePrevious ? current.time() - previous.time() : 0.0;
        boolean usable = havePrevious && elapsed > 0.05;

        // CPU
        if (usable && previous.cpuTicks() != null && current.cpuTicks() != null) {
            long total = current.cpuTicks().total() - previous.cpuTicks().total();
            long idle = current.cpuTicks().idle() - previous.cpuTicks().idle();
            if (total > 0 && idle >= 0) {
                frame.put("cpu", Values.round(100.0 * (total - idle) / total, 1));
            }
        }

        // RAM
        Memory memory = current.memory();
        if (memory != null) {
            frame.put("ram", memory.usagePercent());
            frame.put("ram_used_gb", memory.usedGb());
            frame.put("ram_total_gb", memory.totalGb());
        }

        // GPU
        Gpu gpu = current.gpu();
        if (gpu != null) {
            frame.put("gpu", gpu.usagePercent());
            frame.put("vram_used_gb", gpu.vramUsedGb());
            frame.put("vram_total_gb", gpu.vramTotalGb());
            if (gpu.vramUsedGb() != null && gpu.vramTotalGb() != null && gpu.vramTotalGb() > 0) {
                frame.put("vram", Values.round(100.0 * gpu.vramUsedGb() / gpu.vramTotalGb(), 1));
            }
            frame.put("gpu_clock_mhz", gpu.clockMhz());
            frame.put("gpu_temp", gpu.tempCelsius());

            Double previousEnergy = (havePrevious && previous.gpu() != null) ? previous.gpu().energyUj() : null;
            if (gpu.powerWatts() != null) {
                frame.put("gpu_power_watts", gpu.powerWatts());
            } else if (usable && gpu.energyUj() != null && previousEnergy != null && gpu.energyUj() >= previousEnergy) {
                // Energy counter of the GPU device itself (hwmon energy1_input), never the CPU's RAPL.
                frame.put("gpu_power_watts", Values.round((gpu.energyUj() - previousEnergy) / (elapsed * 1_000_000.0), 1));
            }
        }

        // Temperatures
        Temps temps = current.temps();
        if (temps != null) {
            frame.put("cpu_temp", temps.cpu());
            frame.put("system_temp", temps.system());
            frame.put("disk_temp", temps.disk());
        }

        // Disk
        Disk disk = current.disk();
        if (disk != null) {
            frame.put("disk_used", disk.usedPercent());
            Disk before = havePrevious ? previous.disk() : null;
            if (usable && before != null && Objects.equals(before.device(), disk.device())
                    && disk.sectorsRead() != null && before.sectorsRead() != null
                    && disk.sectorsWritten() != null && before.sectorsWritten() != null) {
                long read = disk.sectorsRead() - before.sectorsRead();
                long write = disk.sectorsWritten() - before.sectorsWritten();
                if (read >= 0 && write >= 0) {
                    frame.put("disk_read_mbs", Values.round(read * 512.0 / elapsed / 1048576.0, 6));
                    frame.put("disk_write_mbs", Values.round(write * 512.0 / elapsed / 1048576.0, 6));
                }
            }
        }

        // Network
        Net net = current.net();
        if (usable && net != null && net.interfaceName() != null && previous.net() != null
                && net.counters().containsKey(net.interfaceName())
                && previous.net().counters() != null
                && previous.net().counters().containsKey(net.interfaceName())) {
            Rx now = net.counters().get(net.interfaceName());
            Rx before = previous.net().counters().get(net.interfaceName());
            long rx = now.rx() - before.rx();
            long tx = now.tx() - before.tx();
            if (rx >= 0 && tx >= 0) {
                frame.put("net_down_mbps", Values.round(rx * 8.0 / elapsed / 1_000_000.0, 6));
                frame.put("net_up_mbps", Values.round(tx * 8.0 / elapsed / 1_000_000.0, 6));
            }
        }

        return frame;
    }

    // -- CPU / memory ------------------------------------------------------------

    CpuTicks readCpuTicks() {
        List<String> lines = fs.lines("/proc/stat");
        if (lines.isEmpty()) {
            return null;
        }
        String[] fields = lines.get(0).trim().split("\\s+");
        if (fields.length < 5 || !"cpu".equals(fields[0])) {
            return null;
        }
        long[] ticks = new long[fields.length - 1];
        long total = 0;
        for (int i = 1; i < fields.length; i++) {
            ticks[i - 1] = parseLongOrZero(fields[i]);
            total += ticks[i - 1];
        }
        long idle = ticks[3] + (ticks.length > 4 ? ticks[4] : 0);
        return new CpuTicks(total, idle);
    }

    Memory readMemory() {
        List<String> lines = fs.lines("/proc/meminfo");
        if (lines.isEmpty()) {
            return null;
        }
        Long total = null;
        Long available = null;
        for (String line : lines) {
            Matcher m = MEM_TOTAL.matcher(line);
            if (m.find()) {
                total = Long.parseLong(m.group(1));
                continue;
            }
            m = MEM_AVAILABLE.matcher(line);
            if (m.find()) {
                available = Long.parseLong(m.group(1));
            }
        }
        if (total == null || available == null || total <= 0) {
            return null;
        }
        long used = Math.max(0, total - available);
        return new Memory(
                Values.round(total / 1048576.0, 2),
                Values.round(used / 1048576.0, 2),
                Values.round(100.0 * used / total, 1));
    }

    // -- network -----------------------------------------------------------------

    /** A network card of the machine (it has a device behind it), not a bridge, VPN or container link. */
    public boolean physicalInterface(String name) {
        return name != null && !name.contains("/") && fs.exists("/sys/class/net/" + name + "/device");
    }

    Net readNetwork() {
        String defaultInterface = defaultInterface();
        Map<String, Rx> counters = new HashMap<>();
        for (String line : fs.lines("/proc/net/dev")) {
            int colon = line.indexOf(':');
            if (colon < 0) {
                continue;
            }
            String name = line.substring(0, colon).trim();
            String[] fields = line.substring(colon + 1).trim().split("\\s+");
            if (name.isEmpty() || fields.length < 16) {
                continue;
            }
            counters.put(name, new Rx(parseLongOrZero(fields[0]), parseLongOrZero(fields[8])));
        }

        String interfaceName = defaultInterface;
        if (interfaceName == null || !counters.containsKey(interfaceName)) {
            List<String> candidates = new ArrayList<>();
            for (String name : counters.keySet()) {
                if (!"lo".equals(name)) {
                    candidates.add(name);
                }
            }
            interfaceName = candidates.size() == 1 ? candidates.get(0) : null;
        }
        return new Net(interfaceName, counters);
    }

    private String defaultInterface() {
        List<String> lines = fs.lines("/proc/net/route");
        for (int index = 1; index < lines.size(); index++) {
            String[] fields = lines.get(index).trim().split("\\s+");
            if (fields.length >= 4 && "00000000".equals(fields[1]) && (parseHex(fields[3]) & 1) == 1) {
                return fields[0];
            }
        }
        return null;
    }

    // -- disk --------------------------------------------------------------------

    Disk readDisk() {
        Mount mount = findMount(diskPath);

        Double usedPercent = null;
        Double usedGb = null;
        Double totalGb = null;
        try {
            FileStore store = Files.getFileStore(Path.of(fs.root().isEmpty() ? diskPath : "/"));
            double total = store.getTotalSpace();
            double free = store.getUsableSpace();
            if (total > 0) {
                double used = Math.max(0.0, total - free);
                usedPercent = Values.round(100.0 * used / total, 1);
                usedGb = Values.round(used / 1073741824.0, 2);
                totalGb = Values.round(total / 1073741824.0, 2);
            }
        } catch (IOException | SecurityException e) {
            // No file store information: the percentages stay null.
        }

        String baseDevice = null;
        Long sectorsRead = null;
        Long sectorsWritten = null;
        if (mount != null && mount.source().startsWith("/dev/")) {
            baseDevice = baseBlockDevice(Path.of(mount.source()).getFileName().toString());
            for (String line : fs.lines("/proc/diskstats")) {
                String[] fields = line.trim().split("\\s+");
                if (fields.length >= 10 && fields[2].equals(baseDevice)) {
                    sectorsRead = parseLongOrZero(fields[5]);
                    sectorsWritten = parseLongOrZero(fields[9]);
                    break;
                }
            }
        }

        if (usedPercent == null && sectorsRead == null) {
            return null;
        }
        return new Disk(baseDevice, baseDevice, mount == null ? diskPath : mount.mountPoint(),
                usedPercent, usedGb, totalGb, sectorsRead, sectorsWritten);
    }

    private record Mount(String source, String mountPoint) {
    }

    /** The longest mount point that contains {@code path}. */
    private Mount findMount(String path) {
        Mount best = null;
        for (String line : fs.lines("/proc/mounts")) {
            String[] fields = line.trim().split("\\s+");
            if (fields.length < 3) {
                continue;
            }
            String mountPoint = unescapeMount(fields[1]);
            boolean contains = "/".equals(mountPoint)
                    || path.equals(mountPoint)
                    || path.startsWith(stripTrailingSlash(mountPoint) + "/");
            if (contains && (best == null || mountPoint.length() >= best.mountPoint().length())) {
                best = new Mount(fields[0], mountPoint);
            }
        }
        return best;
    }

    /** sda1 -> sda, nvme0n1p2 -> nvme0n1, mmcblk0p1 -> mmcblk0. */
    String baseBlockDevice(String device) {
        String sysPath = fs.absolute("/sys/class/block/" + device);
        Path partition = Path.of(sysPath, "partition");
        if (Files.isRegularFile(partition)) {
            try {
                Path real = Path.of(sysPath).toRealPath();
                if (real.getParent() != null && real.getParent().getFileName() != null) {
                    return real.getParent().getFileName().toString();
                }
            } catch (IOException e) {
                // fall through to the name-based rules
            }
        }
        Matcher m = Pattern.compile("^(nvme\\d+n\\d+)p\\d+$").matcher(device);
        if (m.matches()) {
            return m.group(1);
        }
        m = Pattern.compile("^(mmcblk\\d+)p\\d+$").matcher(device);
        if (m.matches()) {
            return m.group(1);
        }
        m = Pattern.compile("^((?:sd|vd|xvd|hd)[a-z]+)\\d+$").matcher(device);
        if (m.matches()) {
            return m.group(1);
        }
        return device;
    }

    // -- helpers -----------------------------------------------------------------

    static String unescapeMount(String raw) {
        Matcher m = MOUNT_ESCAPES.matcher(raw);
        StringBuilder out = new StringBuilder();
        while (m.find()) {
            String replacement = switch (m.group(1)) {
                case "040" -> " ";
                case "011" -> "\t";
                default -> "\\";
            };
            m.appendReplacement(out, Matcher.quoteReplacement(replacement));
        }
        m.appendTail(out);
        return out.toString();
    }

    static String stripTrailingSlash(String path) {
        String stripped = path.replaceAll("/+$", "");
        return stripped.isEmpty() ? "/" : stripped;
    }

    static long parseLongOrZero(String text) {
        try {
            return Long.parseLong(text);
        } catch (NumberFormatException e) {
            return 0L;
        }
    }

    static long parseHex(String text) {
        try {
            return Long.parseLong(text, 16);
        } catch (NumberFormatException e) {
            return 0L;
        }
    }

    static double nowSeconds() {
        return System.currentTimeMillis() / 1000.0;
    }
}
