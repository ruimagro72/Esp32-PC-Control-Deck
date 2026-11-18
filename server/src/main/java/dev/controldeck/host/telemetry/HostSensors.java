package dev.controldeck.host.telemetry;

import dev.controldeck.host.telemetry.spi.GpuContext;
import dev.controldeck.host.telemetry.spi.GpuPlugin;
import dev.controldeck.host.telemetry.spi.GpuValues;
import dev.controldeck.host.util.Values;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Stream;

/**
 * Temperatures and the GPU, read from hwmon, thermal zones and DRM. Every path is
 * relative to the {@link SysFs} root. Whatever the driver does not expose stays
 * null.
 */
final class HostSensors {

    private static final List<String> CPU_HWMON = List.of("coretemp", "k10temp", "zenpower", "cpu_thermal", "soc_thermal");
    private static final List<String> CPU_ZONES = List.of("x86_pkg_temp", "cpu-thermal", "cpu_thermal", "soc_thermal");
    private static final List<String> SYSTEM_ZONES = List.of("acpitz", "pch_cannonlake", "pch_skylake");
    private static final Pattern CPU_LABEL = Pattern.compile("^(Package|Tctl|Tdie|CPU)", Pattern.CASE_INSENSITIVE);
    private static final Pattern CARD = Pattern.compile(".*/card\\d+$");
    private static final Pattern NVME_CONTROLLER = Pattern.compile("^(nvme\\d+)n\\d+$");

    private final SysFs fs;
    private final Double configuredVramTotalGb;
    private final DrmClients drmClients;

    HostSensors(SysFs fs, Double configuredVramTotalGb, java.util.function.LongSupplier nanoClock) {
        this.fs = fs;
        this.configuredVramTotalGb = configuredVramTotalGb;
        this.drmClients = new DrmClients(fs, nanoClock);
    }

    // -- temperatures ------------------------------------------------------------

    HostMetrics.Temps temperatures(Double gpuTemp, String diskDevice) {
        return new HostMetrics.Temps(
                cpuTemperature(),
                systemTemperature(),
                diskDevice != null ? diskTemperature(diskDevice) : null,
                gpuTemp);
    }

    private Double cpuTemperature() {
        Double best = null;
        for (String hwmon : fs.globRelative("/sys/class/hwmon/hwmon*")) {
            if (!CPU_HWMON.contains(fs.trimmed(hwmon + "/name"))) {
                continue;
            }
            // Prefer the package / die sensor over individual cores.
            for (String input : fs.globRelative(hwmon + "/temp*_input")) {
                String label = fs.trimmed(input.replaceFirst("_input$", "_label"));
                Optional<Double> value = fs.milliCelsius(input);
                if (value.isEmpty()) {
                    continue;
                }
                if (CPU_LABEL.matcher(label).lookingAt()) {
                    return value.get();
                }
                if (best == null) {
                    best = value.get();
                }
            }
        }
        if (best != null) {
            return best;
        }
        return thermalZone(CPU_ZONES).orElse(null);
    }

    private Double systemTemperature() {
        for (String hwmon : fs.globRelative("/sys/class/hwmon/hwmon*")) {
            if ("acpitz".equals(fs.trimmed(hwmon + "/name"))) {
                Optional<Double> value = fs.milliCelsius(hwmon + "/temp1_input");
                if (value.isPresent()) {
                    return value.get();
                }
            }
        }
        return thermalZone(SYSTEM_ZONES).orElse(null);
    }

    private Optional<Double> thermalZone(List<String> types) {
        for (String zone : fs.globRelative("/sys/class/thermal/thermal_zone*")) {
            if (types.contains(fs.trimmed(zone + "/type"))) {
                Optional<Double> value = fs.milliCelsius(zone + "/temp");
                if (value.isPresent()) {
                    return value;
                }
            }
        }
        return Optional.empty();
    }

    private Double diskTemperature(String device) {
        List<String> patterns = List.of(
                "/sys/class/block/" + device + "/device/hwmon/hwmon*/temp1_input",
                "/sys/class/block/" + device + "/device/device/hwmon/hwmon*/temp1_input");
        for (String pattern : patterns) {
            for (String path : fs.globRelative(pattern)) {
                Optional<Double> value = fs.milliCelsius(path);
                if (value.isPresent()) {
                    return value.get();
                }
            }
        }
        // NVMe: the controller (nvme0) owns the hwmon node, the block device is nvme0n1.
        Matcher m = NVME_CONTROLLER.matcher(device);
        if (m.matches()) {
            for (String path : fs.globRelative("/sys/class/nvme/" + m.group(1) + "/hwmon*/temp1_input")) {
                Optional<Double> value = fs.milliCelsius(path);
                if (value.isPresent()) {
                    return value.get();
                }
            }
        }
        return null;
    }

    // -- GPU ---------------------------------------------------------------------

    /**
     * Pick the most capable DRM card (VRAM counters first, then Intel Arc A770, then
     * any other) and read what the driver exposes. Returns null without a DRM card.
     */
    HostMetrics.Gpu gpu() {
        List<String> cards = new ArrayList<>();
        for (String path : fs.globRelative("/sys/class/drm/card*")) {
            if (CARD.matcher(path).matches() && isDirectory(path + "/device")) {
                cards.add(path);
            }
        }
        if (cards.isEmpty()) {
            return null;
        }
        cards.sort(Comparator.<String>comparingInt(this::rank).reversed().thenComparing(Comparator.naturalOrder()));

        String card = cards.get(0);
        String device = card + "/device";
        String drm = device + "/drm/" + Path.of(card).getFileName();

        // The card's own hwmon (docs/03-SERVER.md). i915 on an Arc has no temperature
        // there; xe names its sensors temp2 (package) and temp3 (VRAM), and its energy
        // counters energy1 (card) and energy2 (package).
        Double temp = null;
        Double powerWatts = null;
        Double energy = null;
        Integer fan = null;
        for (String hwmon : fs.globRelative(device + "/hwmon/hwmon*")) {
            if (temp == null) {
                temp = fs.milliCelsius(hwmon + "/temp1_input")
                        .or(() -> fs.milliCelsius(hwmon + "/temp2_input"))
                        .orElse(null);
            }
            Optional<Double> power = fs.numeric(hwmon + "/power1_input");
            if (powerWatts == null && power.isPresent() && power.get() > 0) {
                powerWatts = Values.round(power.get() / 1000000.0, 1);
            }
            if (energy == null) {
                energy = fs.numeric(hwmon + "/energy1_input")
                        .or(() -> fs.numeric(hwmon + "/energy2_input"))
                        .orElse(null);
            }
            if (fan == null) {
                fan = fs.numeric(hwmon + "/fan1_input").map(v -> (int) Math.round(v)).orElse(null);
            }
        }

        // Vendor plugins first (docs/03-SERVER.md): what only their driver tells (i915/xe in
        // gpu-intel, amdgpu in gpu-amd). Then what every Linux GPU driver offers: the DRM
        // clients' fdinfo (busy time per engine class, local memory) and a configured VRAM size.
        String driver = driver(device);
        GpuValues values = new GpuValues();
        GpuContext context = context(card, device, drm, driver);
        for (GpuPlugin plugin : Plugins.gpu()) {
            if (driver != null && plugin.supports(driver)) {
                try {
                    plugin.read(context, values);
                } catch (RuntimeException e) {
                    // A plugin never breaks a sample: its values stay unknown.
                }
            }
        }
        DrmClients.Reading clients = null;
        if (values.vramUsedBytes == null || values.busyPercent == null) {
            clients = drmClients.read(pciAddress(device));
            if (values.vramUsedBytes == null) values.vramUsedBytes = clients.localBytes();
            if (values.busyPercent == null) values.busyPercent = clients.busyPercent();
        }
        Double vramUsed = values.vramUsedBytes;
        Double busy = values.busyPercent;
        Double clock = values.clockMhz;
        Double requested = values.requestedMhz;
        Double idleMs = values.idleResidencyMs;
        // Both branches must be Double: mixing a double with a Double that is null would unbox it
        // and throw a NullPointerException (a GPU whose driver does not expose its memory size).
        Double vramTotalGb = values.vramTotalBytes != null
                ? Double.valueOf(Values.round(values.vramTotalBytes / 1073741824.0, 2))
                : configuredVramTotalGb;

        Map<String, Double> engines = new LinkedHashMap<>();
        if (clients != null && clients.engines() != null) {
            clients.engines().forEach((name, percent) -> engines.put(name, Values.round(percent, 1)));
        }
        String pci = pciAddress(device);
        return new HostMetrics.Gpu(
                busy != null ? Values.round(busy, 1) : null,
                vramUsed != null ? Values.round(vramUsed / 1073741824.0, 2) : null,
                vramTotalGb,
                clock != null ? (int) Math.round(clock) : null,
                temp,
                powerWatts,
                energy,
                driver,
                requested != null ? (int) Math.round(requested) : null,
                idleMs,
                fan,
                engines.isEmpty() ? null : engines,
                interrupts(pci, driver));
    }


    /**
     * The device's interrupts so far, from /proc/interrupts (readable by any user; the same
     * count as the i915 PMU's "interrupts" event): the lines that name its PCI address and its
     * driver, summed over the CPU columns. Null when no line is found (a line with the driver's
     * name alone could be another card on the same driver, so it is not taken).
     */
    Long interrupts(String pci, String driver) {
        List<String> lines = fs.lines("/proc/interrupts");
        if (pci == null || driver == null || lines.isEmpty()) {
            return null;
        }
        int cpus = lines.get(0).trim().split("\\s+").length;
        long total = 0;
        boolean found = false;
        for (int i = 1; i < lines.size(); i++) {
            String[] fields = lines.get(i).trim().split("\\s+");
            if (fields.length < 2 || !fields[0].endsWith(":")) {
                continue;
            }
            long count = 0;
            int column = 1;
            for (; column < fields.length && column <= cpus && fields[column].matches("\\d+"); column++) {
                count += Long.parseLong(fields[column]);
            }
            String rest = String.join(" ", java.util.Arrays.asList(fields).subList(column, fields.length));
            if (rest.contains(pci) && fields[fields.length - 1].equals(driver)) {
                total += count;
                found = true;
            }
        }
        return found ? Long.valueOf(total) : null;
    }

    /** The kernel driver bound to the device ("i915", "xe"), or null. */
    private String driver(String device) {
        try {
            Path link = Path.of(fs.absolute(device + "/driver"));
            return Files.isSymbolicLink(link) ? Files.readSymbolicLink(link).getFileName().toString() : null;
        } catch (IOException | SecurityException e) {
            return null;
        }
    }

    /** The VRAM size the card's vendor plugin reports, in bytes, or null (no plugin, or it does not know). */
    Double pluginVramTotalBytes(String card) {
        String device = card + "/device";
        String drm = device + "/drm/" + Path.of(card).getFileName();
        String driver = driver(device);
        GpuValues values = new GpuValues();
        GpuContext context = context(card, device, drm, driver);
        for (GpuPlugin plugin : Plugins.gpu()) {
            if (driver != null && plugin.supports(driver)) {
                try {
                    plugin.read(context, values);
                } catch (RuntimeException e) {
                    // a plugin never breaks the inventory
                }
            }
        }
        return values.vramTotalBytes;
    }

    /**
     * Which card to show when there are several (higher wins). Common rules only: a card with its
     * own memory (a prefetchable PCI window of 2 GiB or more) over an integrated one;
     * a known GPU vendor over others. Vendor plugins may add their own preference.
     */
    int rank(String card) {
        String device = card + "/device";
        int score = 0;
        if (largestPrefetchWindow(device) >= 2.0 * 1073741824.0) {
            score += 2;
        }
        if (List.of("0x8086", "0x1002", "0x10de").contains(fs.trimmed(device + "/vendor"))) {
            score += 1;
        }
        String driver = driver(device);
        for (GpuPlugin plugin : Plugins.gpu()) {
            if (driver != null && plugin.supports(driver)) {
                score += plugin.preference(context(card, device, device + "/drm/" + Path.of(card).getFileName(), driver));
            }
        }
        return score;
    }

    /** Size in bytes of the device's largest prefetchable memory BAR (sysfs "resource"), 0 if none. */
    private double largestPrefetchWindow(String device) {
        double best = 0;
        for (String line : fs.lines(device + "/resource")) {
            String[] f = line.trim().split("\\s+");
            if (f.length < 3) continue;
            try {
                long start = Long.decode(f[0]), end = Long.decode(f[1]), flags = Long.decode(f[2]);
                if ((flags & 0x200) != 0 && (flags & 0x2000) != 0 && end > start) best = Math.max(best, (double) (end - start + 1));
            } catch (NumberFormatException e) {
                // a malformed line: skipped
            }
        }
        return best;
    }

    /** The plugins' view of one card, over SysFs (a fixture tree in the tests). */
    private GpuContext context(String card, String device, String drm, String driver) {
        return new GpuContext() {
            public String card() { return card; }
            public String device() { return device; }
            public String drm() { return drm; }
            public String driver() { return driver; }
            public String pciAddress() { return HostSensors.this.pciAddress(device); }
            public Optional<Double> number(String path) { return fs.numeric(path); }
            public Optional<String> text(String path) { return fs.read(path); }
            public List<String> lines(String path) { return fs.lines(path); }
            public List<String> glob(String pattern) { return fs.globRelative(pattern); }
            public List<String> list(String directory) {
                Path dir = Path.of(fs.absolute(directory));
                if (!Files.isDirectory(dir)) return List.of();
                try (Stream<Path> entries = Files.list(dir)) {
                    return entries.map(p -> p.getFileName().toString()).sorted().toList();
                } catch (IOException | SecurityException e) {
                    return List.of();
                }
            }
        };
    }

    /** "0000:03:00.0": the PCI device the card's device link points at; null if unknown. */
    private String pciAddress(String device) {
        try {
            return Path.of(fs.absolute(device)).toRealPath().getFileName().toString();
        } catch (IOException | SecurityException e) {
            return null;
        }
    }

    private boolean isDirectory(String relative) {
        return Files.isDirectory(Path.of(fs.absolute(relative)));
    }
}
