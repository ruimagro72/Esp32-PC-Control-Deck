package dev.controldeck.host.config;

import dev.controldeck.host.Defaults;
import dev.controldeck.host.util.Values;

import java.nio.file.Path;
import java.util.ArrayList;
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
 *
 * Defaults and migration live in {@link ConfigDefaults}, value checks in
 * {@link ConfigValidator}, token generation and hashing in {@link ConfigTokens}.
 */
public final class ConfigService {

    public static final int CONFIG_VERSION = 2;
    /** How many Decks can hold a token of their own. */
    public static final int MAX_DECK_TOKENS = 32;

    public static final int MAX_MACRO_ID = 6;
    public static final int MAX_LABEL_LENGTH = 22;

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
        return ConfigDefaults.defaults();
    }

    /** Migrate legacy flat keys, then fill every missing key from the defaults. */
    public static Map<String, Object> normalize(Map<String, Object> raw) {
        return ConfigDefaults.normalize(raw);
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
        ConfigValidator.macros(macros);
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
        ConfigValidator.language(language);
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
        ConfigValidator.historyMinutes(minutes);
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
            serial.put("port", ConfigValidator.serialPort(settings.get("port")));
        }
        if (settings.containsKey("baud")) {
            serial.put("baud", ConfigValidator.baud(settings.get("baud")));
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
                port == null ? Defaults.API_PORT : port.intValue(),
                hash == null ? null : String.valueOf(hash),
                List.copyOf(deckHashes));
    }

    /** Deck id -> token hash, one entry per Deck token handed over USB. Internal use. */
    public Map<String, String> deckTokenHashes() {
        Map<String, Object> wifi = sectionOf(section(load(), "transports"), "wifi");
        Map<String, String> owners = new LinkedHashMap<>();
        if (wifi.get("deck_tokens") instanceof Map<?, ?> decks) {
            for (Map.Entry<?, ?> entry : decks.entrySet()) {
                if (entry.getKey() instanceof String id && entry.getValue() instanceof String hash && !hash.isEmpty()) {
                    owners.put(id, hash);
                }
            }
        }
        return owners;
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
            section(config, "api").put("bind", ConfigValidator.bind(settings.get("bind")));
        }
        if (settings.containsKey("port")) {
            section(config, "api").put("port", ConfigValidator.port(settings.get("port")));
        }
        save(config);
    }

    /**
     * Create a new Wi-Fi peer token. Only its SHA-256 is stored; the plain value
     * is returned once and cannot be recovered.
     */
    public String rotateWifiToken() {
        String token = ConfigTokens.newToken();
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
        ConfigValidator.deckId(deckId);
        String token = ConfigTokens.newToken();
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
        return ConfigTokens.sha256Hex(text);
    }

    static boolean isIpAddress(String text) {
        return ConfigValidator.isIpAddress(text);
    }

    static Map<String, Object> section(Map<String, Object> config, String key) {
        return ConfigDefaults.section(config, key);
    }

    private static Map<String, Object> sectionOf(Map<String, Object> parent, String key) {
        return section(parent, key);
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
