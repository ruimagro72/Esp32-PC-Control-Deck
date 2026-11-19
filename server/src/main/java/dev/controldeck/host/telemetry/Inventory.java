package dev.controldeck.host.telemetry;

import dev.controldeck.host.util.Values;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Static component characteristics: what the hardware is, not what it is doing.
 * Every field the host cannot read is null.
 */
public final class Inventory {

    private static final Map<String, String> VENDORS = Map.of(
            "0x8086", "Intel", "0x10de", "NVIDIA", "0x1002", "AMD");
    private static final Pattern MOUNT_ESCAPES = Pattern.compile("\\\\(040|011|134)");
    private static final Pattern CARD = Pattern.compile(".*/card\\d+$");
    private static final Pattern VIRTUAL_DISK = Pattern.compile("^(loop|ram|zram|dm-|md|sr|fd).*");
    private static final Pattern BDF = Pattern.compile("([0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\\.[0-9a-f])$", Pattern.CASE_INSENSITIVE);
    private static final Pattern LSPCI_LINE = Pattern.compile("^\\S+\\s+\"[^\"]*\"\\s+\"([^\"]*)\"\\s+\"([^\"]*)\"");
    private static final List<String> LSPCI_CANDIDATES = List.of("/usr/bin/lspci", "/usr/sbin/lspci", "/bin/lspci", "/sbin/lspci");
    /** Most bytes read from lspci output; a normal line is a few hundred bytes. */
    private static final int MAX_LSPCI_OUTPUT_BYTES = 1 << 20;

    private final SysFs fs;
    private final String diskPath;
    private final Double configuredVramTotalGb;
    private final HostSensors sensors;   // card choice and vendor plugins, shared with the samples

    public Inventory(String root, String diskPath, Double configuredVramTotalGb) {
        this.fs = new SysFs(root);
        this.diskPath = (diskPath == null || diskPath.isEmpty() || "auto".equalsIgnoreCase(diskPath)) ? "/" : HostMetrics.stripTrailingSlash(diskPath);
        this.configuredVramTotalGb = configuredVramTotalGb;
        this.sensors = new HostSensors(this.fs, configuredVramTotalGb, System::nanoTime);
    }

    public Map<String, Object> all() {
        Map<String, Object> all = new LinkedHashMap<>();
        Map<String, Object> platform = new LinkedHashMap<>();
        platform.put("os", osFamily());
        platform.put("kernel", System.getProperty("os.version", ""));
        platform.put("architecture", System.getProperty("os.arch", ""));
        platform.put("collector", supported() ? "linux" : "unsupported");
        all.put("platform", platform);
        all.put("cpu", cpu());
        all.put("ram", ram());
        all.put("gpu", gpu());
        all.put("network", network());
        all.put("disk", disk());
        return all;
    }

    /** One component's inventory, or null for an unknown component. */
    public Map<String, Object> component(String component) {
        if (!MetricRegistry.COMPONENTS.contains(component)) {
            return null;
        }
        return castMap(all().get(component));
    }

    // -- sections ----------------------------------------------------------------

    Map<String, Object> cpu() {
        String model = null;
        int threads = 0;
        Set<String> cores = new HashSet<>();
        String physicalId = null;
        for (String line : fs.lines("/proc/cpuinfo")) {
            Matcher m;
            if ((m = Pattern.compile("^model name\\s*:\\s*(.+)$").matcher(line)).find()) {
                if (model == null) {
                    model = m.group(1).trim();
                }
            } else if (line.matches("^processor\\s*:.*")) {
                threads++;
            } else if ((m = Pattern.compile("^physical id\\s*:\\s*(\\d+)").matcher(line)).find()) {
                physicalId = m.group(1);
            } else if ((m = Pattern.compile("^core id\\s*:\\s*(\\d+)").matcher(line)).find()) {
                cores.add((physicalId == null ? "0" : physicalId) + ":" + m.group(1));
            }
        }
        Double maxKhz = fs.signedNumeric("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq").orElse(null);
        Map<String, Object> cpu = new LinkedHashMap<>();
        cpu.put("model", model);
        cpu.put("physical_cores", cores.isEmpty() ? null : cores.size());
        cpu.put("logical_threads", threads > 0 ? threads : null);
        cpu.put("max_mhz", maxKhz != null ? (Object) (int) Math.round(maxKhz / 1000.0) : null);
        return cpu;
    }

    Map<String, Object> ram() {
        for (String line : fs.lines("/proc/meminfo")) {
            Matcher m = Pattern.compile("^MemTotal:\\s+(\\d+)\\s+kB").matcher(line);
            if (m.find()) {
                Map<String, Object> ram = new LinkedHashMap<>();
                ram.put("total_gb", Values.round(Long.parseLong(m.group(1)) / 1048576.0, 2));
                ram.put("type", null);
                ram.put("speed_mhz", null);
                return ram;
            }
        }
        Map<String, Object> ram = new LinkedHashMap<>();
        ram.put("total_gb", null);
        ram.put("type", null);
        ram.put("speed_mhz", null);
        return ram;
    }

    Map<String, Object> gpu() {
        Map<String, Object> empty = new LinkedHashMap<>();
        empty.put("name", null);
        empty.put("vendor", null);
        empty.put("pci_id", null);
        empty.put("driver", null);
        empty.put("vram_total_gb", configuredVramTotalGb);
        empty.put("vram_total_source", configuredVramTotalGb != null ? "config" : null);

        List<String> cards = new ArrayList<>();
        for (String path : fs.globRelative("/sys/class/drm/card*")) {
            if (CARD.matcher(path).matches() && Files.isDirectory(Path.of(fs.absolute(path + "/device")))) {
                cards.add(path);
            }
        }
        if (cards.isEmpty()) {
            return empty;
        }
        cards.sort(java.util.Comparator.<String>comparingInt(sensors::rank).reversed()
                .thenComparing(java.util.Comparator.naturalOrder()));

        String card = cards.get(0);
        String device = card + "/device";
        String vendorId = fs.trimmed(device + "/vendor").toLowerCase(java.util.Locale.ROOT);
        String deviceId = fs.trimmed(device + "/device").toLowerCase(java.util.Locale.ROOT);
        String driver = null;
        Path driverLink = Path.of(fs.absolute(device + "/driver"));
        if (Files.isSymbolicLink(driverLink)) {
            try {
                driver = Files.readSymbolicLink(driverLink).getFileName().toString();
            } catch (IOException | SecurityException e) {
                driver = null;
            }
        }
        String pciId = (!vendorId.isEmpty() && !deviceId.isEmpty())
                ? vendorId.substring(Math.min(2, vendorId.length())) + ":" + deviceId.substring(Math.min(2, deviceId.length()))
                : null;

        String drm = device + "/drm/" + Path.of(card).getFileName();
        Double vramTotal = sensors.pluginVramTotalBytes(card);   // the vendor plugin (docs/03-SERVER.md)

        Map<String, Object> gpu = new LinkedHashMap<>();
        gpu.put("name", pciName(device));
        gpu.put("vendor", VENDORS.get(vendorId));
        gpu.put("pci_id", pciId);
        gpu.put("driver", driver);
        // Double on both sides: a double against a null Double would be unboxed and throw.
        gpu.put("vram_total_gb", vramTotal != null ? Double.valueOf(Values.round(vramTotal / 1073741824.0, 2)) : configuredVramTotalGb);
        gpu.put("vram_total_source", vramTotal != null ? "driver" : (configuredVramTotalGb != null ? "config" : null));
        return gpu;
    }

    Map<String, Object> network() {
        String defaultInterface = null;
        List<String> routes = fs.lines("/proc/net/route");
        for (int index = 1; index < routes.size(); index++) {
            String[] fields = routes.get(index).trim().split("\\s+");
            if (fields.length >= 4 && "00000000".equals(fields[1]) && (HostMetrics.parseHex(fields[3]) & 1) == 1) {
                defaultInterface = fields[0];
                break;
            }
        }

        List<Map<String, Object>> interfaces = new ArrayList<>();
        for (String path : fs.globRelative("/sys/class/net/*")) {
            String name = Path.of(path).getFileName().toString();
            if ("lo".equals(name)) {
                continue;
            }
            String real = realPath(fs.absolute(path));
            if (real != null && real.contains("/virtual/")) {
                continue; // docker0, veth*, tun*, br-* ...
            }
            Double speed = fs.signedNumeric(path + "/speed").orElse(null);
            String state = fs.trimmed(path + "/operstate");
            boolean wireless = Files.isDirectory(Path.of(fs.absolute(path + "/wireless")))
                    || Files.isDirectory(Path.of(fs.absolute(path + "/phy80211")));
            Map<String, Object> entry = new LinkedHashMap<>();
            entry.put("name", name);
            entry.put("type", wireless ? "wireless" : "ethernet");
            entry.put("state", state.isEmpty() ? null : state);
            entry.put("speed_mbps", (speed != null && speed > 0) ? (Object) speed.intValue() : null);
            entry.put("default_route", name.equals(defaultInterface));
            interfaces.add(entry);
        }
        interfaces.sort(java.util.Comparator
                .<Map<String, Object>, Boolean>comparing(e -> (Boolean) e.get("default_route")).reversed()
                .thenComparing(e -> (String) e.get("name")));
        Map<String, Object> network = new LinkedHashMap<>();
        network.put("interfaces", interfaces);
        return network;
    }

    Map<String, Object> disk() {
        Map<String, List<String>> mounts = new LinkedHashMap<>();
        for (String line : fs.lines("/proc/mounts")) {
            String[] fields = line.trim().split("\\s+");
            if (fields.length >= 3 && fields[0].startsWith("/dev/")) {
                String key = Path.of(fields[0]).getFileName().toString();
                mounts.computeIfAbsent(key, k -> new ArrayList<>()).add(unescapeMount(fields[1]));
            }
        }

        List<Map<String, Object>> disks = new ArrayList<>();
        for (String path : fs.globRelative("/sys/block/*")) {
            String name = Path.of(path).getFileName().toString();
            if (VIRTUAL_DISK.matcher(name).matches()) {
                continue;
            }
            Double sectors = fs.signedNumeric(path + "/size").orElse(null);
            if (sectors == null || sectors * 512 < 52428800) {
                continue; // empty or tiny virtual devices (< 50 MB)
            }
            List<String> mountPoints = new ArrayList<>(mounts.getOrDefault(name, List.of()));
            for (Map.Entry<String, List<String>> mounted : mounts.entrySet()) {
                String device = mounted.getKey();
                if (!device.equals(name) && device.startsWith(name)
                        && Files.isRegularFile(Path.of(fs.absolute(path + "/" + device + "/partition")))) {
                    mountPoints.addAll(mounted.getValue());
                }
            }
            String model = fs.trimmed(path + "/device/model");
            Double rotational = fs.signedNumeric(path + "/queue/rotational").orElse(null);

            String type;
            if (name.startsWith("nvme")) {
                type = "nvme";
            } else if (rotational == null) {
                type = null;
            } else {
                type = rotational > 0 ? "hdd" : "ssd";
            }

            Map<String, Object> entry = new LinkedHashMap<>();
            entry.put("name", name);
            entry.put("model", model.isEmpty() ? null : model);
            entry.put("size_gb", Values.round(sectors * 512 / 1073741824.0, 1));
            entry.put("type", type);
            entry.put("mount_points", new ArrayList<>(new LinkedHashSet<>(mountPoints)));
            entry.put("monitored", mountPoints.contains(diskPath));
            disks.add(entry);
        }
        disks.sort(java.util.Comparator
                .<Map<String, Object>, Boolean>comparing(e -> (Boolean) e.get("monitored")).reversed()
                .thenComparing(e -> (String) e.get("name")));

        Map<String, Object> disk = new LinkedHashMap<>();
        disk.put("monitored_path", diskPath);
        disk.put("disks", disks);
        return disk;
    }

    // -- helpers -----------------------------------------------------------------

    private boolean supported() {
        return !fs.root().isEmpty() || HostMetrics.platformSupportsLinux();
    }

    private static String osFamily() {
        String name = System.getProperty("os.name", "").toLowerCase(java.util.Locale.ROOT);
        if (name.contains("linux")) {
            return "Linux";
        }
        if (name.contains("windows")) {
            return "Windows";
        }
        if (name.contains("mac")) {
            return "Darwin";
        }
        return "Unknown";
    }

    /** Marketing name of the PCI device through lspci, when installed. Never guessed. */
    private String pciName(String devicePath) {
        if (!fs.root().isEmpty()) {
            return null;
        }
        String real = realPath(fs.absolute(devicePath));
        if (real == null) {
            return null;
        }
        Matcher m = BDF.matcher(real);
        if (!m.find()) {
            return null;
        }
        String binary = null;
        for (String candidate : LSPCI_CANDIDATES) {
            if (Files.isExecutable(Path.of(candidate))) {
                binary = candidate;
                break;
            }
        }
        if (binary == null) {
            return null;
        }
        try {
            Process process = new ProcessBuilder(binary, "-mm", "-s", m.group(1))
                    .redirectError(ProcessBuilder.Redirect.DISCARD)
                    .start();
            process.getOutputStream().close();
            String output = new String(process.getInputStream().readNBytes(MAX_LSPCI_OUTPUT_BYTES), java.nio.charset.StandardCharsets.UTF_8);
            if (!process.waitFor(2, TimeUnit.SECONDS)) {
                process.destroyForcibly();
                return null;
            }
            // "00:02.0" "VGA compatible controller" "Intel Corporation" "DG2 [Arc A770]" ...
            Matcher parts = LSPCI_LINE.matcher(output.trim());
            if (parts.find()) {
                return (parts.group(1) + " " + parts.group(2)).trim();
            }
        } catch (IOException e) {
            return null;
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return null;
        }
        return null;
    }

    private static String realPath(String absolute) {
        try {
            return Path.of(absolute).toRealPath().toString();
        } catch (IOException | SecurityException e) {
            return null;
        }
    }

    private static String unescapeMount(String raw) {
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

    @SuppressWarnings("unchecked")
    private static Map<String, Object> castMap(Object value) {
        return (Map<String, Object>) value;
    }
}
