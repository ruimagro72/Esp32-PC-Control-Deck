package dev.controldeck.host.config;

import dev.controldeck.host.util.Values;

import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Reads, normalizes and writes the configuration (config_version 2), which lives in
 * the SQLite database, not in a file.
 *
 * The first time the host runs there is nothing stored yet: {@link #initialize()}
 * writes the defaults, so a missing configuration is never an error.
 *
 * Writers: the Wi-Fi transport settings and the peer token, the serial port and
 * baud rate, the history retention and the macros. The logging writer comes later.
 */
public final class ConfigService {

    public static final int CONFIG_VERSION = 2;
    /** How many Decks can hold a token of their own. */
    public static final int MAX_DECK_TOKENS = 32;
    private static final java.util.regex.Pattern DECK_ID = java.util.regex.Pattern.compile("^[0-9A-Za-z:_-]{1,32}$");

    public static final int MAX_MACRO_ID = 6;
    public static final int MAX_LABEL_LENGTH = 22;

    private static final SecureRandom RANDOM = new SecureRandom();

    private final ConfigStore store;
    private boolean initialized;

    /** Configuration kept in the default database (var/control-deck.db or CONTROL_DECK_DB). */
    public ConfigService() {
        this(HostPaths.databaseFile());
    }

    /** Configuration kept in the given SQLite database. Nothing is opened until first use. */
    public ConfigService(Path databaseFile) {
        this.store = new ConfigStore(databaseFile);
    }

    /** The SQLite database that holds the configuration. */
    public Path path() {
        return store.file();
    }

    /** Release the database connection. */
    public void close() {
        store.close();
    }

    // -- defaults and normalization -------------------------------------------

    public static Map<String, Object> defaults() {
        Map<String, Object> config = new LinkedHashMap<>();
        config.put("config_version", CONFIG_VERSION);
        config.put("api", obj("bind", "127.0.0.1", "port", 8080));
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
                "url", "http://127.0.0.1:8085/data.json",
                "timeout_seconds", 0.5));
        config.put("macros", new ArrayList<Object>());
        // The language of the texts each Deck shows (docs/archive/design-notes/DESIGN.md): one for
        // all, and per Deck id where it differs.
        config.put("decks", obj("language", "en", "languages", obj()));
        return config;
    }

    /** Migrate legacy flat keys, then fill every missing key from the defaults. */
    public static Map<String, Object> normalize(Map<String, Object> raw) {
        Map<String, Object> config = deepCopy(raw);

        if (!raw.containsKey("config_version")) {
            Map<String, Object> transports = section(config, "transports");
            Map<String, Object> serial = sectionOf(transports, "serial");
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
        merged.put("config_version", CONFIG_VERSION);
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

    // -- load / save ----------------------------------------------------------

    /**
     * Make sure the database holds a configuration: when it holds none (first run),
     * the defaults are written.
     *
     * @return true when the defaults were created by this call
     * @throws IllegalStateException when the database cannot be opened or written
     */
    public synchronized boolean initialize() {
        if (initialized) {
            return false;
        }
        boolean created = false;
        if (store.isEmpty()) {
            store.write(normalize(defaults()));
            created = true;
        }
        initialized = true;
        return created;
    }

    /** Configuration as used at run time: the stored settings plus environment overrides (never written back). */
    public Map<String, Object> load() {
        return applyEnvironment(loadRaw());
    }

    /** The stored settings, normalized, without environment overrides. Used by every writer. */
    public Map<String, Object> loadRaw() {
        initialize();
        return normalize(store.read());
    }

    public synchronized void save(Map<String, Object> config) {
        store.write(normalize(config));
        initialized = true;
    }

    private static Map<String, Object> applyEnvironment(Map<String, Object> config) {
        Map<String, Object> transports = section(config, "transports");
        Boolean serial = flag("CONTROL_DECK_SERIAL");
        if (serial != null) {
            sectionOf(transports, "serial").put("enabled", serial);
        }
        Boolean wifi = flag("CONTROL_DECK_WIFI");
        if (wifi != null) {
            sectionOf(transports, "wifi").put("enabled", wifi);
        }
        Boolean ble = flag("CONTROL_DECK_BLE");
        if (ble != null) {
            sectionOf(transports, "ble").put("enabled", ble);
        }
        String bind = System.getenv("CONTROL_DECK_API_BIND");
        if (bind != null && !bind.isBlank()) {
            section(config, "api").put("bind", bind);
        }
        String port = System.getenv("CONTROL_DECK_API_PORT");
        if (port != null) {
            Long value = Values.validateInt(port);
            if (value != null && value >= 1 && value <= 65535) {
                section(config, "api").put("port", value.intValue());
            }
        }
        return config;
    }

    private static Boolean flag(String name) {
        String value = System.getenv(name);
        if ("1".equals(value)) {
            return Boolean.TRUE;
        }
        if ("0".equals(value)) {
            return Boolean.FALSE;
        }
        return null;
    }

    // -- macros ---------------------------------------------------------------

    @SuppressWarnings("unchecked")
    public List<Map<String, Object>> getMacros() {
        return (List<Map<String, Object>>) (List<?>) load().get("macros");
    }

    /**
     * Replace the macro list: numeric ids from 1 to
     * {@link #MAX_MACRO_ID}, no duplicates, a non-blank label of at most
     * {@link #MAX_LABEL_LENGTH} characters. The action is checked by the macro model.
     */
    public synchronized void setMacros(List<Map<String, Object>> macros) {
        java.util.Set<Long> seen = new java.util.HashSet<>();
        for (Map<String, Object> macro : macros) {
            Long id = macro == null ? null : Values.validateInt(macro.get("id"));
            if (id == null) {
                throw new IllegalArgumentException("Macro ID must be numeric");
            }
            if (id < 1 || id > MAX_MACRO_ID) {
                throw new IllegalArgumentException("Macro ID must be between 1 and " + MAX_MACRO_ID);
            }
            if (!seen.add(id)) {
                throw new IllegalArgumentException("Duplicate macro ID " + id);
            }
            if (!(macro.get("label") instanceof String label) || label.isBlank()) {
                throw new IllegalArgumentException("Macro label is required");
            }
            if (Values.codePointLength(label) > MAX_LABEL_LENGTH) {
                throw new IllegalArgumentException("Label exceeds " + MAX_LABEL_LENGTH + " characters");
            }
        }
        Map<String, Object> config = loadRaw();
        config.put("macros", new ArrayList<Object>(macros));
        save(config);
    }

    // -- decks ----------------------------------------------------------------

    /** The language of a Deck's texts: its own if set, else the one for all ("en"). */
    public String getDeckLanguage(String deckId) {
        Map<String, Object> decks = section(load(), "decks");
        if (deckId != null && decks.get("languages") instanceof Map<?, ?> own
                && own.get(deckId) instanceof String language && !language.isBlank()) {
            return language;
        }
        return decks.get("language") instanceof String language && !language.isBlank() ? language : "en";
    }

    /** Sets a Deck's language, or the one for all when {@code deckId} is null. */
    public synchronized void setDeckLanguage(String deckId, String language) {
        if (language == null || !language.matches("[a-z]{2}")) {
            throw new IllegalArgumentException("language must be two lower-case letters (en, pt, es ...)");
        }
        Map<String, Object> config = loadRaw();
        Map<String, Object> decks = section(config, "decks");
        if (deckId == null) {
            decks.put("language", language);
        } else {
            sectionOf(decks, "languages").put(deckId, language);
        }
        save(config);
    }

    // -- telemetry ------------------------------------------------------------

    public Map<String, Object> getTelemetrySettings() {
        return section(load(), "telemetry");
    }

    /** How many minutes of telemetry history are kept (20 to 240). */
    public synchronized void setHistoryMinutes(int minutes) {
        if (minutes < 20 || minutes > 240) {
            throw new IllegalArgumentException("history_minutes must be an integer between 20 and 240");
        }
        Map<String, Object> config = loadRaw();
        section(config, "telemetry").put("history_minutes", minutes);
        save(config);
    }

    // -- serial ---------------------------------------------------------------

    /** Serial transport view: {@code enabled}, {@code port} ("auto" or a device) and {@code baud}. */
    public Map<String, Object> getSerialSettings() {
        return new LinkedHashMap<>(sectionOf(section(load(), "transports"), "serial"));
    }

    /**
     * Change the serial port and/or the baud rate. {@code port} is "auto" or a device
     * name (/dev/ttyACM0, COM4); {@code baud} a positive integer.
     */
    public synchronized void setSerialSettings(Map<String, Object> settings) {
        Map<String, Object> config = loadRaw();
        Map<String, Object> serial = sectionOf(section(config, "transports"), "serial");
        if (settings.containsKey("port")) {
            Object port = settings.get("port");
            if (!(port instanceof String text) || text.isBlank() || text.length() > 255) {
                throw new IllegalArgumentException("port must be \"auto\" or a device name such as /dev/ttyACM0 or COM4");
            }
            serial.put("port", text.trim());
        }
        if (settings.containsKey("baud")) {
            Long baud = Values.validateInt(settings.get("baud"));
            if (baud == null || baud <= 0 || baud > 4_000_000) {
                throw new IllegalArgumentException("baud must be a positive integer");
            }
            serial.put("baud", baud.intValue());
        }
        if (settings.containsKey("enabled")) {
            serial.put("enabled", truthy(settings.get("enabled")));
        }
        save(config);
    }

    // -- api and wifi ---------------------------------------------------------

    /** Wi-Fi transport state needed by the peer check: enabled, bind, port and the token hash. */
    public WifiSettings getWifiSettings() {
        Map<String, Object> config = load();
        Map<String, Object> wifi = sectionOf(section(config, "transports"), "wifi");
        Map<String, Object> api = section(config, "api");
        Object hash = wifi.get("token_hash");
        Long port = Values.validateInt(api.get("port"));
        List<String> deckHashes = new ArrayList<>();
        if (wifi.get("deck_tokens") instanceof Map<?, ?> decks) {
            for (Object value : decks.values()) {
                if (value instanceof String text && !text.isEmpty()) {
                    deckHashes.add(text);
                }
            }
        }
        return new WifiSettings(
                truthy(wifi.get("enabled")),
                String.valueOf(api.get("bind")),
                port == null ? 8080 : port.intValue(),
                hash == null ? null : String.valueOf(hash),
                List.copyOf(deckHashes));
    }

    /** Full transport section including the token hash. Internal use. */
    public Map<String, Object> getTransportSettings() {
        return section(load(), "transports");
    }

    public void setWifiSettings(Map<String, Object> settings) {
        Map<String, Object> config = loadRaw();
        Map<String, Object> wifi = sectionOf(section(config, "transports"), "wifi");
        if (settings.containsKey("enabled")) {
            wifi.put("enabled", truthy(settings.get("enabled")));
        }
        if (settings.containsKey("bind")) {
            Object bind = settings.get("bind");
            if (!(bind instanceof String text) || !(isIpAddress(text) || "localhost".equals(text))) {
                throw new IllegalArgumentException(
                        "bind must be an IP address (for example 127.0.0.1, 0.0.0.0 or a LAN address)");
            }
            section(config, "api").put("bind", text);
        }
        if (settings.containsKey("port")) {
            Long port = Values.validateInt(settings.get("port"));
            if (port == null || port < 1 || port > 65535) {
                throw new IllegalArgumentException("port must be an integer between 1 and 65535");
            }
            section(config, "api").put("port", port.intValue());
        }
        save(config);
    }

    /**
     * Create a new Wi-Fi peer token. Only its SHA-256 is stored; the plain value
     * is returned once and cannot be recovered.
     */
    public String rotateWifiToken() {
        byte[] bytes = new byte[24];
        RANDOM.nextBytes(bytes);
        String token = HexFormat.of().formatHex(bytes);
        Map<String, Object> config = loadRaw();
        sectionOf(section(config, "transports"), "wifi").put("token_hash", sha256Hex(token));
        save(config);
        return token;
    }

    /**
     * A token of its own for the Deck {@code deckId} (its chip id), replacing the one it had.
     * Given to a Deck over its USB cable, so it can use Wi-Fi without anyone typing a token.
     * Only the SHA-256 is stored; at most {@link #MAX_DECK_TOKENS} Decks, the oldest is
     * dropped first.
     */
    @SuppressWarnings("unchecked")
    public synchronized String issueDeckToken(String deckId) {
        if (deckId == null || !DECK_ID.matcher(deckId).matches()) {
            throw new IllegalArgumentException("Invalid Deck id");
        }
        byte[] bytes = new byte[24];
        RANDOM.nextBytes(bytes);
        String token = HexFormat.of().formatHex(bytes);
        Map<String, Object> config = loadRaw();
        Map<String, Object> wifi = sectionOf(section(config, "transports"), "wifi");
        Map<String, Object> decks = wifi.get("deck_tokens") instanceof Map<?, ?> existing
                ? new LinkedHashMap<>((Map<String, Object>) existing) : new LinkedHashMap<>();
        decks.remove(deckId);
        while (decks.size() >= MAX_DECK_TOKENS) {
            decks.remove(decks.keySet().iterator().next());
        }
        decks.put(deckId, sha256Hex(token));
        wifi.put("deck_tokens", decks);
        save(config);
        return token;
    }

    /** Clears the shared token only; the Decks keep their own tokens. */
    public void clearWifiToken() {
        Map<String, Object> config = loadRaw();
        sectionOf(section(config, "transports"), "wifi").put("token_hash", null);
        save(config);
    }

    // -- helpers --------------------------------------------------------------

    /** A setting read as a switch: false for null, false, 0, "" and "0"; true otherwise. */
    public static boolean truthy(Object value) {
        if (value == null) {
            return false;
        }
        if (value instanceof Boolean b) {
            return b;
        }
        if (value instanceof Number n) {
            return n.doubleValue() != 0.0;
        }
        String text = String.valueOf(value);
        return !text.isEmpty() && !"0".equals(text);
    }

    public static String sha256Hex(String text) {
        try {
            byte[] digest = MessageDigest.getInstance("SHA-256").digest(text.getBytes(StandardCharsets.UTF_8));
            return HexFormat.of().formatHex(digest);
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException("SHA-256 is not available", e);
        }
    }

    static boolean isIpAddress(String text) {
        if (text.matches("^(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})$")) {
            for (String octet : text.split("\\.")) {
                if (Integer.parseInt(octet) > 255) {
                    return false;
                }
            }
            return true;
        }
        // IPv6 literal (loose check: hex digits, colons and an optional embedded IPv4).
        return text.contains(":") && text.matches("^[0-9A-Fa-f:.]+$");
    }

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

    private static Map<String, Object> sectionOf(Map<String, Object> parent, String key) {
        return section(parent, key);
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

    /** Builds an insertion-ordered map from alternating key/value arguments. */
    static Map<String, Object> obj(Object... pairs) {
        Map<String, Object> map = new LinkedHashMap<>();
        for (int i = 0; i < pairs.length; i += 2) {
            map.put((String) pairs[i], pairs[i + 1]);
        }
        return map;
    }

    /**
     * Wi-Fi transport view used by the peer check and the transport registry.
     *
     * @param tokenHash       the shared token (scripts, curl), or null
     * @param deckTokenHashes one token per Deck, handed over USB
     */
    public record WifiSettings(boolean enabled, String bind, int port, String tokenHash, List<String> deckTokenHashes) {

        /** True when any token, shared or of a Deck, exists. */
        public boolean anyToken() {
            return (tokenHash != null && !tokenHash.isEmpty()) || !deckTokenHashes.isEmpty();
        }
    }
}
