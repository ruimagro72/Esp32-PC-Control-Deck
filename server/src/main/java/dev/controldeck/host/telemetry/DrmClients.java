package dev.controldeck.host.telemetry;

import java.io.IOException;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.function.LongSupplier;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * GPU use measured from the DRM clients' fdinfo (the kernel's documented
 * "drm-usage-stats", Documentation/gpu/drm-usage-stats.rst): per open GPU file, the busy time
 * of each engine class and the memory in the card's local memory (VRAM). Needs no privilege,
 * but only the processes of the user the host runs as are readable, so it is the GPU use of
 * that user's programs (in practice the desktop and everything on it). Used where the driver
 * has no global counter, such as i915 and xe on an Intel Arc (docs/03-SERVER.md).
 *
 * <p>Two forms of busy time exist: {@code drm-engine-<class>: <ns> ns} (i915, amdgpu, ...),
 * a time, and {@code drm-cycles-<class>} with {@code drm-total-cycles-<class>} (xe), busy GPU
 * cycles against the GPU cycles that passed. {@code drm-engine-capacity-<class>} is how many
 * engines of the class the figures are summed over (two video engines on an Arc A770).
 *
 * <p>Each engine class is in percent of its capacity over the time since the previous call
 * (null on the first call); the GPU's utilization is the busiest class.
 */
final class DrmClients {

    private static final long RESCAN_NS = 10_000_000_000L;
    private static final Pattern ENGINE_NS = Pattern.compile("^drm-engine-([a-z0-9-]+):[ \\t]+(\\d+)[ \\t]+ns", Pattern.MULTILINE);
    private static final Pattern CAPACITY = Pattern.compile("^drm-engine-capacity-([a-z0-9-]+):[ \\t]+(\\d+)", Pattern.MULTILINE);
    private static final Pattern CYCLES = Pattern.compile("^drm-cycles-([a-z0-9-]+):[ \\t]+(\\d+)", Pattern.MULTILINE);
    private static final Pattern TOTAL_CYCLES = Pattern.compile("^drm-total-cycles-([a-z0-9-]+):[ \\t]+(\\d+)", Pattern.MULTILINE);
    private static final Pattern LOCAL = Pattern.compile(
            "^drm-(?:resident-local0|resident-vram0|total-lmem):[ \\t]+(\\d+)[ \\t]*(KiB|MiB|GiB)?", Pattern.MULTILINE);
    private static final Pattern CLIENT = Pattern.compile("^drm-client-id:[ \\t]+(\\d+)", Pattern.MULTILINE);
    private static final Pattern PDEV = Pattern.compile("^drm-pdev:[ \\t]+(\\S+)", Pattern.MULTILINE);

    /** The engine classes under one name for both Intel drivers: i915 names them, xe abbreviates. */
    private static final Map<String, String> CLASS_NAMES = Map.of(
            "rcs", "render", "bcs", "copy", "vcs", "video", "vecs", "video-enhance", "ccs", "compute");

    /**
     * @param busyPercent the busiest engine class, in percent (null on the first call)
     * @param localBytes  the clients' memory in VRAM (null when no client reports it)
     * @param engines     engine class ("render", "video", ...) to percent; empty on the first call
     */
    record Reading(Double busyPercent, Double localBytes, Map<String, Double> engines) {
    }

    /** One client's counters at one call: busy ns, or busy cycles with total cycles, per engine class. */
    private record Counters(Map<String, Long> busyNs, Map<String, Long> cycles, Map<String, Long> totalCycles) {
    }

    private final SysFs fs;
    private final LongSupplier nanoClock;
    private List<String> fdinfos = new ArrayList<>();  // /proc/<pid>/fdinfo/<fd> of GPU files
    private boolean scanned;
    private long scannedAtNs;
    private Map<String, Counters> last = new HashMap<>();  // by drm-client-id
    private long lastAtNs;
    private boolean haveLast;

    DrmClients(SysFs fs, LongSupplier nanoClock) {
        this.fs = fs;
        this.nanoClock = nanoClock;
    }

    /** @param pciAddress the card's PCI address ("0000:03:00.0"), to keep other cards' clients out */
    Reading read(String pciAddress) {
        long startNs = nanoClock.getAsLong();
        if (!scanned || startNs - scannedAtNs >= RESCAN_NS) {
            fdinfos = scan();
            scannedAtNs = startNs;
            scanned = true;
        }

        Set<String> seenClients = new HashSet<>();
        Map<String, Counters> clients = new HashMap<>();
        Map<String, Integer> capacity = new HashMap<>();
        long localBytes = 0;
        boolean anyLocal = false;
        for (String fdinfo : fdinfos) {
            String text = fs.read(fdinfo).orElse(null);
            if (text == null) {
                continue;  // the file was closed since the scan
            }
            Matcher pdev = PDEV.matcher(text);
            if (pciAddress != null && pdev.find() && !pdev.group(1).equals(pciAddress)) {
                continue;
            }
            Matcher client = CLIENT.matcher(text);
            if (!client.find() || !seenClients.add(client.group(1))) {
                continue;  // the same client open through several descriptors counts once
            }
            clients.put(client.group(1), new Counters(values(ENGINE_NS, text), values(CYCLES, text),
                    values(TOTAL_CYCLES, text)));
            for (Map.Entry<String, Long> entry : values(CAPACITY, text).entrySet()) {
                capacity.merge(entry.getKey(), (int) Math.max(1, Math.min(64, entry.getValue())), Math::max);
            }
            Matcher local = LOCAL.matcher(text);
            if (local.find()) {
                anyLocal = true;
                long value = Long.parseLong(local.group(1));
                String unit = local.group(2) == null ? "" : local.group(2);
                localBytes += switch (unit) {
                    case "KiB" -> value * 1024L;
                    case "MiB" -> value * 1024L * 1024L;
                    case "GiB" -> value * 1024L * 1024L * 1024L;
                    default -> value;
                };
            }
        }

        long atNs = nanoClock.getAsLong();
        Map<String, Double> engines = new TreeMap<>();
        if (haveLast && atNs > lastAtNs) {
            Map<String, Double> fractions = new HashMap<>();  // engine class (raw name) to engines busy
            Set<String> classes = new HashSet<>();
            for (Map.Entry<String, Counters> entry : clients.entrySet()) {
                Counters now = entry.getValue();
                classes.addAll(now.busyNs().keySet());
                classes.addAll(now.cycles().keySet());
                Counters before = last.get(entry.getKey());
                if (before == null) {
                    continue;  // a client that is new since the last call: its history is not this interval
                }
                for (Map.Entry<String, Long> busy : now.busyNs().entrySet()) {
                    Long was = before.busyNs().get(busy.getKey());
                    if (was != null && busy.getValue() >= was) {
                        fractions.merge(busy.getKey(), (busy.getValue() - was) / (double) (atNs - lastAtNs), Double::sum);
                    }
                }
                for (Map.Entry<String, Long> busy : now.cycles().entrySet()) {
                    Long was = before.cycles().get(busy.getKey());
                    Long total = now.totalCycles().get(busy.getKey());
                    Long totalWas = before.totalCycles().get(busy.getKey());
                    if (was != null && total != null && totalWas != null && busy.getValue() >= was && total > totalWas) {
                        fractions.merge(busy.getKey(), (busy.getValue() - was) / (double) (total - totalWas), Double::sum);
                    }
                }
            }
            for (String raw : classes) {
                double fraction = fractions.getOrDefault(raw, 0.0) / capacity.getOrDefault(raw, 1);
                engines.merge(className(raw), Math.min(100.0, 100.0 * fraction), Math::max);
            }
        }
        last = clients;
        lastAtNs = atNs;
        haveLast = true;
        Double busy = engines.values().stream().max(Double::compare).orElse(null);
        return new Reading(busy, anyLocal ? (double) localBytes : null, new LinkedHashMap<>(engines));
    }

    static String className(String raw) {
        return CLASS_NAMES.getOrDefault(raw, raw);
    }

    private static Map<String, Long> values(Pattern pattern, String text) {
        Map<String, Long> values = new HashMap<>();
        Matcher m = pattern.matcher(text);
        while (m.find()) {
            try {
                values.put(m.group(1), Long.parseLong(m.group(2)));
            } catch (NumberFormatException e) {
                // A counter past the range of a long: left out.
            }
        }
        return values;
    }

    /** The descriptors of every readable process that point at a DRM device. */
    private List<String> scan() {
        List<String> found = new ArrayList<>();
        Path proc = Path.of(fs.absolute("/proc"));
        try (DirectoryStream<Path> pids = Files.newDirectoryStream(proc, entry -> entry.getFileName().toString().matches("\\d+"))) {
            for (Path pid : pids) {
                try (DirectoryStream<Path> fds = Files.newDirectoryStream(pid.resolve("fd"))) {
                    for (Path fd : fds) {
                        try {
                            if (Files.readSymbolicLink(fd).toString().startsWith("/dev/dri/")) {
                                found.add("/proc/" + pid.getFileName() + "/fdinfo/" + fd.getFileName());
                            }
                        } catch (IOException | UnsupportedOperationException | SecurityException e) {
                            // Gone, or not a link.
                        }
                    }
                } catch (IOException | SecurityException e) {
                    // Another user's process: not readable.
                }
            }
        } catch (IOException e) {
            // No /proc.
        }
        return found;
    }
}
