package dev.controldeck.host.config;

import dev.controldeck.host.Defaults;
import dev.controldeck.host.util.Values;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * The default configuration, the migration of legacy flat keys and the merge of
 * stored values over the defaults. Pure data handling: no database access.
 */
final class ConfigDefaults {

    /** Default LibreHardwareMonitor data URL (its web server on this computer). */
    static final String LIBRE_HARDWARE_MONITOR_URL = "http://127.0.0.1:8085/data.json";

    private ConfigDefaults() {
    }

    static Map<String, Object> defaults() {
        Map<String, Object> config = new LinkedHashMap<>();
        config.put("config_version", ConfigService.CONFIG_VERSION);
        config.put("api", obj("bind", Defaults.LOOPBACK_BIND, "port", Defaults.API_PORT));
        config.put("telemetry", obj(
                "interval_seconds", 2.0,
                "disk_path", "auto",
                "history_minutes", 30,
                "gpu_vram_total_gb", null));
        config.put("transports", obj(
                "serial", obj("enabled", true, "port", "auto", "baud", 115200),
                "wifi", obj("enabled", false, "token_hash", null),
                "ble", obj("enabled", false)));
        config.put("logging", obj(
                "disk_logging", false,
                "level", "info",
                "file", "var/log/control-deck.log",
                "max_size_mb", 5));
        config.put("librehardwaremonitor", obj(
                "enabled", obj("windows", false, "linux", false, "macos", false),
                "url", LIBRE_HARDWARE_MONITOR_URL,
                "timeout_seconds", 0.5));
        config.put("macros", new ArrayList<Object>());
        // The language of the texts each Deck shows (docs/archive/design-notes/DESIGN.md): one for
        // all, and per Deck id where it differs.
        config.put("decks", obj("language", "en", "languages", obj()));
        return config;
    }

    /** Migrate legacy flat keys, then fill every missing key from the defaults. */
    static Map<String, Object> normalize(Map<String, Object> raw) {
        Map<String, Object> config = deepCopy(raw);

        if (!raw.containsKey("config_version")) {
            Map<String, Object> transports = section(config, "transports");
            Map<String, Object> serial = section(transports, "serial");
            Map<String, Object> telemetry = section(config, "telemetry");
            if (raw.containsKey("serial_port")) {
                serial.put("port", raw.get("serial_port"));
            }
            if (raw.containsKey("baud")) {
                Long baud = Values.validateInt(raw.get("baud"));
                serial.put("baud", baud != null ? baud.intValue() : 0);
            }
            if (raw.containsKey("interval_seconds")) {
                telemetry.put("interval_seconds", toDouble(raw.get("interval_seconds")));
            }
            if (raw.containsKey("disk_path")) {
                telemetry.put("disk_path", raw.get("disk_path"));
            }
            for (String legacy : List.of("serial_port", "baud", "interval_seconds", "disk_path", "log_file")) {
                config.remove(legacy);
            }
        }

        Map<String, Object> merged = mergeDefaults(defaults(), config);
        merged.put("config_version", ConfigService.CONFIG_VERSION);
        Object macros = merged.get("macros");
        merged.put("macros", macros instanceof List<?> ? new ArrayList<Object>((List<?>) macros) : new ArrayList<Object>());
        return merged;
    }

    @SuppressWarnings("unchecked")
    private static Map<String, Object> mergeDefaults(Map<String, Object> defaults, Map<String, Object> values) {
        Map<String, Object> result = new LinkedHashMap<>(defaults);
        for (Map.Entry<String, Object> entry : values.entrySet()) {
            Object fallback = defaults.get(entry.getKey());
            if (entry.getValue() instanceof Map<?, ?> valueMap && fallback instanceof Map<?, ?> fallbackMap
                    && !fallbackMap.isEmpty()) {
                result.put(entry.getKey(), mergeDefaults((Map<String, Object>) fallbackMap, (Map<String, Object>) valueMap));
            } else {
                result.put(entry.getKey(), entry.getValue());
            }
        }
        return result;
    }

    private static Map<String, Object> deepCopy(Map<String, Object> source) {
        Map<String, Object> copy = new LinkedHashMap<>();
        for (Map.Entry<String, Object> entry : source.entrySet()) {
            Object value = entry.getValue();
            if (value instanceof Map<?, ?> nested) {
                @SuppressWarnings("unchecked")
                Map<String, Object> typed = (Map<String, Object>) nested;
                copy.put(entry.getKey(), deepCopy(typed));
            } else if (value instanceof List<?> items) {
                copy.put(entry.getKey(), new ArrayList<Object>(items));
            } else {
                copy.put(entry.getKey(), value);
            }
        }
        return copy;
    }

    private static double toDouble(Object value) {
        if (value instanceof Number n) {
            return n.doubleValue();
        }
        try {
            return Double.parseDouble(String.valueOf(value).trim());
        } catch (NumberFormatException e) {
            return 0.0;
        }
    }

    /** Returns the nested map under {@code key}, creating and storing an empty one when missing. */
    @SuppressWarnings("unchecked")
    static Map<String, Object> section(Map<String, Object> config, String key) {
        Object value = config.get(key);
        if (!(value instanceof Map<?, ?>)) {
            Map<String, Object> created = new LinkedHashMap<>();
            config.put(key, created);
            return created;
        }
        return (Map<String, Object>) value;
    }

    /** Builds an insertion-ordered map from alternating key/value arguments. */
    static Map<String, Object> obj(Object... pairs) {
        Map<String, Object> map = new LinkedHashMap<>();
        for (int i = 0; i < pairs.length; i += 2) {
            map.put((String) pairs[i], pairs[i + 1]);
        }
        return map;
    }
}
