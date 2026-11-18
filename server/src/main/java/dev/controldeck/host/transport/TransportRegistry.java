package dev.controldeck.host.transport;

import dev.controldeck.host.config.ConfigService;
import dev.controldeck.host.config.HostPaths;
import dev.controldeck.host.util.Json;
import dev.controldeck.host.util.Values;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * What each transport is doing right now. Wrappers report dynamic state with
 * {@link #update}; static facts (enabled, bind address, whether a token exists)
 * come from the configuration. Nothing returned here contains a secret.
 *
 * USB is reported from what the serial bridge records: connecting, connected, error or
 * stopped; "connected" turns into "stale" when the bridge has not heard of any traffic
 * for 30 seconds, and the state is "disabled" when the transport is switched off in the
 * settings.
 */
public final class TransportRegistry {

    private static final long USB_STALE_AFTER_SECONDS = 30;
    private static final long WIFI_CONNECTED_WITHIN_SECONDS = 15;

    private final ConfigService config;
    private final Path file;

    public TransportRegistry(ConfigService config, Path varDir) {
        this.config = config;
        this.file = varDir.resolve("transports.json");
    }

    public TransportRegistry(ConfigService config) {
        this(config, HostPaths.varDir());
    }

    /** Merge dynamic fields into one transport's state. Unknown keys are kept. */
    public synchronized void update(String transport, Map<String, Object> fields) {
        Map<String, Object> state = readState();
        Map<String, Object> current = castMap(state.get(transport));
        Map<String, Object> merged = current == null ? new LinkedHashMap<>() : new LinkedHashMap<>(current);
        merged.putAll(fields);
        merged.put("updated_at", System.currentTimeMillis() / 1000L);
        state.put(transport, merged);
        try {
            Json.writeAtomic(file, Json.encode(state));
        } catch (IOException e) {
            // State is informational; a failed write must not fail the request that triggered it.
        }
    }

    public synchronized Map<String, Object> state() {
        return readState();
    }

    /** Record that an authenticated remote peer talked to the API. */
    public void touchWifiPeer(String address) {
        update("wifi", Map.of("last_peer_seen", now(), "last_peer_address", address));
    }

    public Map<String, Object> all() {
        Map<String, Object> transports = config.getTransportSettings();
        ConfigService.WifiSettings wifi = config.getWifiSettings();
        Map<String, Object> state = state();

        Map<String, Object> result = new LinkedHashMap<>();
        result.put("usb", usb(castMap(transports.get("serial")), castMap(state.get("usb"))));
        result.put("wifi", wifi(wifi, castMap(state.get("wifi"))));

        // One entry per BLE Deck ("ble:<name>", written by its serial bridge); connected when one is.
        Map<String, Object> ble = new LinkedHashMap<>();
        Map<String, Object> decks = new LinkedHashMap<>();
        String bleState = "waiting";
        for (Map.Entry<String, Object> entry : state.entrySet()) {
            if (entry.getKey().startsWith("ble:") && entry.getValue() instanceof Map<?, ?> deck) {
                Map<String, Object> shown = new LinkedHashMap<>();
                shown.put("state", deck.get("state"));
                shown.put("port", deck.get("port"));
                shown.put("firmware", deck.get("firmware"));
                shown.put("last_activity", deck.get("last_activity"));
                decks.put(entry.getKey().substring(4), shown);
                if ("connected".equals(deck.get("state"))) {
                    bleState = "connected";
                }
            }
        }
        ble.put("id", "ble");
        ble.put("label", "Bluetooth Low Energy");
        ble.put("implemented", true);
        ble.put("enabled", true);
        ble.put("state", bleState);
        ble.put("decks", decks);
        ble.put("note", "Paired Decks are reached through server/ble-bridge (Linux); see docs/04-PROTOCOL.md.");
        ble.put("contract", "docs/04-PROTOCOL.md");
        result.put("ble", ble);

        Map<String, Object> classic = new LinkedHashMap<>();
        classic.put("id", "bluetooth_classic");
        classic.put("label", "Bluetooth Classic");
        classic.put("implemented", false);
        classic.put("enabled", false);
        classic.put("supported", false);
        classic.put("state", "unsupported");
        classic.put("reason", "The ESP32-S3 (LCDWiki ES3C28P) has no Bluetooth Classic radio; only BLE is available.");
        result.put("bluetooth_classic", classic);
        return result;
    }

    /** Summary of which transport carries telemetry right now. */
    @SuppressWarnings("unchecked")
    public Map<String, Object> connection() {
        Map<String, Object> transports = all();
        String active = null;
        for (String id : List.of("usb", "wifi", "ble")) {
            Map<String, Object> entry = (Map<String, Object>) transports.get(id);
            if ("connected".equals(entry.get("state"))) {
                active = id;
                break;
            }
        }

        Map<String, Object> states = new LinkedHashMap<>();
        for (Map.Entry<String, Object> entry : transports.entrySet()) {
            states.put(entry.getKey(), String.valueOf(((Map<String, Object>) entry.getValue()).get("state")));
        }

        Map<String, Object> connection = new LinkedHashMap<>();
        connection.put("active_transport", active);
        connection.put("state", active != null ? "connected" : "disconnected");
        connection.put("transports", states);
        connection.put("last_error", ((Map<String, Object>) transports.get("usb")).get("last_error"));
        connection.put("server_time", now());
        return connection;
    }

    private Map<String, Object> usb(Map<String, Object> serial, Map<String, Object> state) {
        boolean enabled = serial != null && ConfigService.truthy(serial.get("enabled"));
        String dynamic = state.get("state") instanceof String text ? text : "stopped";
        long lastActivity = state.get("last_activity") instanceof Number n ? n.longValue() : 0L;
        // A bridge that stopped reporting for a while is not "connected" any more.
        if ("connected".equals(dynamic) && lastActivity > 0 && now() - lastActivity > USB_STALE_AFTER_SECONDS) {
            dynamic = "stale";
        }

        Map<String, Object> usb = new LinkedHashMap<>();
        usb.put("id", "usb");
        usb.put("label", "USB serial");
        usb.put("implemented", true);
        usb.put("enabled", enabled);
        usb.put("state", enabled ? dynamic : "disabled");
        usb.put("configured_port", serial == null ? "auto" : String.valueOf(serial.get("port")));
        usb.put("port", state.get("port"));
        usb.put("baud", serial == null ? null : Values.validateInt(serial.get("baud")));
        usb.put("firmware", state.get("firmware"));
        usb.put("peer_protocol", state.get("peer_protocol"));
        usb.put("peer_capabilities", state.get("peer_capabilities") instanceof List<?> caps ? caps : List.of());
        usb.put("last_activity", state.get("last_activity"));
        usb.put("last_error", state.get("last_error"));
        return usb;
    }

    private Map<String, Object> wifi(ConfigService.WifiSettings settings, Map<String, Object> state) {
        boolean loopbackOnly = List.of("127.0.0.1", "localhost", "::1").contains(settings.bind());
        boolean tokenConfigured = settings.anyToken();

        List<String> problems = new java.util.ArrayList<>();
        if (settings.enabled()) {
            if (loopbackOnly) {
                problems.add("api.bind is a loopback address: the ESP32 cannot reach the API (use 0.0.0.0 or a LAN address)");
            }
            if (!tokenConfigured) {
                problems.add("no peer token yet: connect a Deck once by USB (it receives its own), or create one with POST /api/wifi/token");
            }
        }

        String current;
        if (!settings.enabled()) {
            current = "disabled";
        } else if (!problems.isEmpty()) {
            current = "misconfigured";
        } else {
            long seen = state.get("last_peer_seen") instanceof Number n ? n.longValue() : 0L;
            current = (seen > 0 && now() - seen <= WIFI_CONNECTED_WITHIN_SECONDS) ? "connected" : "listening";
        }

        Map<String, Object> wifi = new LinkedHashMap<>();
        wifi.put("id", "wifi");
        wifi.put("label", "Wi-Fi (LAN)");
        wifi.put("implemented", true);
        wifi.put("enabled", settings.enabled());
        wifi.put("state", current);
        wifi.put("bind", settings.bind());
        wifi.put("port", settings.port());
        wifi.put("lan_reachable", !loopbackOnly);
        wifi.put("auth", "bearer");
        wifi.put("token_configured", tokenConfigured);
        wifi.put("problems", problems);
        wifi.put("last_peer_seen", state.get("last_peer_seen"));
        wifi.put("last_peer_address", state.get("last_peer_address"));
        return wifi;
    }

    private Map<String, Object> readState() {
        try {
            if (!Files.isRegularFile(file)) {
                return new LinkedHashMap<>();
            }
            Map<String, Object> parsed = Json.parseObject(Files.readString(file));
            return parsed == null ? new LinkedHashMap<>() : new LinkedHashMap<>(parsed);
        } catch (IOException e) {
            return new LinkedHashMap<>();
        }
    }

    private static long now() {
        return System.currentTimeMillis() / 1000L;
    }

    @SuppressWarnings("unchecked")
    private static Map<String, Object> castMap(Object value) {
        return value instanceof Map<?, ?> ? (Map<String, Object>) value : new LinkedHashMap<>();
    }
}
