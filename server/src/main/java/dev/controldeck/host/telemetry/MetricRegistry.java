package dev.controldeck.host.telemetry;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;

/**
 * Metrics that can be shown as history graphs. Each maps a stable id to a field of
 * the protocol-v3 {@code stats} frame, its unit, the component page and a scale.
 * Only fields the host really sends are listed.
 */
public final class MetricRegistry {

    private MetricRegistry() {
    }

    public static final List<String> COMPONENTS = List.of("cpu", "ram", "gpu", "network", "disk");

    /** Time windows the firmware offers, in minutes. */
    public static final List<Integer> WINDOWS = List.of(5, 10, 15, 20);

    /** One registry entry. {@code max} is null when the metric has no fixed upper bound. */
    public record Metric(String id, String field, String unit, String component, String label,
                         double min, Double max) {

        /** The JSON shape of a metric in GET /api/metrics. */
        public Map<String, Object> toMap() {
            Map<String, Object> map = new LinkedHashMap<>();
            map.put("id", id);
            map.put("field", field);
            map.put("unit", unit);
            map.put("component", component);
            map.put("label", label);
            map.put("min", min);
            map.put("max", max);
            return map;
        }
    }

    private static final Map<String, Metric> METRICS = new LinkedHashMap<>();

    /** Every stats field a registry metric reads: the whitelist for SQL column names. */
    private static final Set<String> FIELDS = new HashSet<>();

    private static void add(String id, String field, String unit, String component, String label,
                            double min, Double max) {
        METRICS.put(id, new Metric(id, field, unit, component, label, min, max));
    }

    static {
        add("cpu_usage", "cpu", "%", "cpu", "CPU usage", 0, 100.0);
        add("cpu_temp", "cpu_temp", "°C", "cpu", "CPU temperature", 0, 110.0);
        add("system_temp", "system_temp", "°C", "cpu", "System temperature", 0, 110.0);
        add("ram_usage", "ram", "%", "ram", "RAM usage", 0, 100.0);
        add("gpu_usage", "gpu", "%", "gpu", "GPU usage", 0, 100.0);
        add("vram_usage", "vram", "%", "gpu", "VRAM usage", 0, 100.0);
        add("gpu_temp", "gpu_temp", "°C", "gpu", "GPU temperature", 0, 110.0);
        add("gpu_clock", "gpu_clock_mhz", "MHz", "gpu", "GPU clock", 0, null);
        add("gpu_power", "gpu_power_watts", "W", "gpu", "GPU power", 0, null);
        add("net_down", "net_down_mbps", "Mbit/s", "network", "Download", 0, null);
        add("net_up", "net_up_mbps", "Mbit/s", "network", "Upload", 0, null);
        add("disk_used", "disk_used", "%", "disk", "Disk used", 0, 100.0);
        add("disk_read", "disk_read_mbs", "MB/s", "disk", "Disk read", 0, null);
        add("disk_write", "disk_write_mbs", "MB/s", "disk", "Disk write", 0, null);
        add("disk_temp", "disk_temp", "°C", "disk", "Disk temperature", 0, 110.0);
        for (Metric metric : METRICS.values()) {
            FIELDS.add(metric.field());
        }
    }

    private static final Map<String, String> DEFAULTS = Map.of(
            "cpu", "cpu_usage",
            "ram", "ram_usage",
            "gpu", "gpu_usage",
            "network", "net_down",
            "disk", "disk_read");

    public static List<Metric> all() {
        return new ArrayList<>(METRICS.values());
    }

    public static boolean has(String id) {
        return id != null && METRICS.containsKey(id);
    }

    public static Optional<Metric> get(String id) {
        return Optional.ofNullable(id == null ? null : METRICS.get(id));
    }

    /** Stats field of a metric, or null for an unknown id. */
    public static String fieldOf(String id) {
        Metric metric = id == null ? null : METRICS.get(id);
        return metric == null ? null : metric.field();
    }

    /**
     * The quoted SQL column name of a stats field. Only registry fields pass; anything else
     * (the only input that could reach SQL text) throws.
     *
     * @throws IllegalArgumentException for a field the registry does not define
     */
    public static String sqlColumn(String field) {
        if (field == null || !FIELDS.contains(field)) {
            throw new IllegalArgumentException("Unknown stats field '" + field + "'");
        }
        return "\"" + field + "\"";
    }

    /** Metric ids of one component, in display order. */
    public static List<String> forComponent(String component) {
        List<String> ids = new ArrayList<>();
        for (Metric metric : METRICS.values()) {
            if (metric.component().equals(component)) {
                ids.add(metric.id());
            }
        }
        return ids;
    }

    public static String defaultFor(String component) {
        return DEFAULTS.get(component);
    }

    public static boolean isValidWindow(long minutes) {
        return WINDOWS.stream().anyMatch(w -> w == minutes);
    }
}
