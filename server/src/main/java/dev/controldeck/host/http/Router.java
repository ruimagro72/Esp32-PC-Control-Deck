package dev.controldeck.host.http;

import dev.controldeck.host.Services;
import dev.controldeck.host.protocol.Codec4;
import dev.controldeck.host.protocol.Frame4;
import dev.controldeck.host.transport.serial.LineSplitter;
import dev.controldeck.host.firmware.FirmwareStore;
import dev.controldeck.host.pack.PackStore;
import dev.controldeck.host.macro.MacroService;
import dev.controldeck.host.protocol.Protocol;
import dev.controldeck.host.protocol.MessageHandler;
import dev.controldeck.host.telemetry.HistoryStore;
import dev.controldeck.host.telemetry.MetricRegistry;
import dev.controldeck.host.transport.openaction.OpenActionClient;
import dev.controldeck.host.util.Log;
import dev.controldeck.host.util.Values;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Maps requests to handlers and applies the access rules.
 *
 *  public  GET /                         health summary, nothing sensitive
 *  peer    telemetry, history, inventory, transports, POST /api/deck (a Deck's binary frames)
 *          loopback needs nothing; any other address needs the Bearer token
 *  peer    also: macro events (by id only) and the macro list (sanitized for LAN peers)
 *  local   Wi-Fi settings, token management and the macro definitions
 *          loopback only, even with a valid token: macros are executable
 *          configuration and must never be writable from the LAN
 *
 * Every request first passes {@link RequestGuard}, which keeps web pages out.
 * Not in the Java host yet: the configuration writers for serial, telemetry, disk,
 * hardware monitor and logs (/api/config/*).
 */
public final class Router {

    private enum Scope { PUBLIC, PEER, LOCAL }

    @FunctionalInterface
    interface Handler {
        Response apply(Request request, String[] groups);
    }

    private record Route(String method, Pattern pattern, Scope scope, Handler handler) {
    }

    private final Services services;
    private final dev.controldeck.host.display.VirtualMonitor vmonitor;
    private final boolean countLoopbackPeers;
    private final List<Route> routes;

    public Router(Services services) {
        this(services, "1".equals(System.getenv("CONTROL_DECK_COUNT_LOOPBACK_PEERS")));
    }

    /**
     * @param countLoopbackPeers true counts a loopback peer as a Wi-Fi peer (the emulator's traffic
     *                           is loopback): test runs only, production keeps false
     */
    public Router(Services services, boolean countLoopbackPeers) {
        this.services = services;
        this.countLoopbackPeers = countLoopbackPeers;
        this.vmonitor = new dev.controldeck.host.display.VirtualMonitor(
                () -> services.config().getWifiSettings().bind(),
                token -> services.auth().acceptsToken(token));
        this.routes = buildRoutes();
    }

    public Response handle(Request request) {
        try {
            return dispatch(request);
        } catch (HttpError error) {
            return Response.error(error.status(), error.getMessage());
        } catch (MacroService.NotFound error) {
            return Response.error(404, error.getMessage());
        } catch (MacroService.Conflict error) {
            return Response.error(409, error.getMessage());
        } catch (IllegalArgumentException error) {
            return Response.error(400, error.getMessage());
        } catch (RuntimeException error) {
            Log.error("Control Deck API: unhandled error: " + error);
            return Response.error(500, "Internal server error");
        }
    }

    private Response dispatch(Request request) {
        RequestGuard.checkSource(request);
        Route matched = null;
        String[] groups = new String[0];
        Set<String> methodsForPath = new LinkedHashSet<>();
        for (Route route : routes) {
            Matcher m = route.pattern().matcher(request.path());
            if (!m.matches()) {
                continue;
            }
            methodsForPath.add(route.method());
            if (route.method().equals(request.method())) {
                matched = route;
                groups = new String[m.groupCount()];
                for (int i = 0; i < groups.length; i++) {
                    groups[i] = m.group(i + 1);
                }
                break;
            }
        }

        // Everything except the health check requires the peer to be known, so an
        // unauthenticated LAN client cannot even enumerate the routes.
        Scope scope = matched == null ? Scope.PEER : matched.scope();
        if (scope != Scope.PUBLIC) {
            if (scope == Scope.LOCAL && !request.isLoopback()) {
                throw HttpError.forbidden("This endpoint is only available from the host itself");
            }
            if (scope != Scope.LOCAL) {
                try {
                    services.auth().authorize(request);
                } catch (HttpError refused) {
                    if (!request.isLoopback()) {
                        wifiLog(request.remoteAddress(), false,
                                "[wifi] Refused " + request.remoteAddress() + ": " + refused.getMessage());
                    }
                    throw refused;
                }
            }
            if (countLoopbackPeers || !request.isLoopback()) {
                services.transports().touchWifiPeer(request.remoteAddress());
            }
            if (!request.isLoopback()) {
                wifiLog(request.remoteAddress(), true, "[wifi] Peer " + request.remoteAddress() + " connected");
            }
        }

        if (matched == null) {
            if (!methodsForPath.isEmpty()) {
                return Response.error(405, "Method not allowed", Map.of("Allow", String.join(", ", methodsForPath)));
            }
            return Response.error(404, "Endpoint not found");
        }
        RequestGuard.checkBody(request);
        return matched.handler().apply(request, groups);
    }

    // A LAN peer on the console: when it starts talking (after a minute of silence) and when
    // it is refused (at most once a minute per address), so a Deck on Wi-Fi is visible.
    private static final long WIFI_LOG_QUIET_MS = 60_000;
    private final java.util.concurrent.ConcurrentHashMap<String, Long> wifiSeen = new java.util.concurrent.ConcurrentHashMap<>();

    private void wifiLog(String address, boolean accepted, String line) {
        long now = System.currentTimeMillis();
        String key = (accepted ? "ok " : "refused ") + address;
        Long last = wifiSeen.put(key, now);
        if (last == null || now - last > WIFI_LOG_QUIET_MS) {
            Log.info(line);
        } else if (!accepted) {
            wifiSeen.put(key, last);  // refusals: keep the time of the last line printed
        }
    }

    // -- routes ------------------------------------------------------------------

    private List<Route> buildRoutes() {
        List<Route> list = new ArrayList<>();
        // public
        list.add(new Route("GET", pattern("/"), Scope.PUBLIC, (r, g) -> Response.ok(root())));

        // telemetry (peer)
        list.add(new Route("GET", pattern("/api/stats"), Scope.PEER, (r, g) -> stats()));
        list.add(new Route("GET", pattern("/api/gpu"), Scope.PEER, (r, g) -> gpu()));
        list.add(new Route("GET", pattern("/api/metrics"), Scope.PEER, (r, g) -> metrics()));
        list.add(new Route("GET", pattern("/api/messages"), Scope.LOCAL,
                (r, g) -> Response.ok(MessageHandler.map("success", true, "data", services.messages().inbox().all()))));
        list.add(new Route("POST", pattern("/api/messages"), Scope.LOCAL, (r, g) -> leaveMessage(r.json())));
        list.add(new Route("DELETE", pattern("/api/messages/(\\d+)"), Scope.LOCAL, (r, g) -> deleteMessage(g[0])));
        list.add(new Route("GET", pattern("/api/messages/(\\d+)/audio"), Scope.LOCAL, (r, g) -> messageAudio(g[0])));
        list.add(new Route("PUT", pattern("/api/language"), Scope.LOCAL, (r, g) -> setLanguage(null, r.json())));
        list.add(new Route("PUT", pattern("/api/decks/([0-9A-Fa-f]{1,24})/language"), Scope.LOCAL,
                (r, g) -> setLanguage(g[0], r.json())));
        list.add(new Route("GET", pattern("/api/packs"), Scope.LOCAL, (r, g) -> packs()));
        list.add(new Route("PUT", pattern("/api/decks/([0-9A-Fa-f]{1,24})/pack"), Scope.LOCAL,
                (r, g) -> setPack(g[0], r.json())));
        list.add(new Route("GET", pattern("/api/catalog"), Scope.PEER,
                (r, g) -> Response.ok(services.messages().handle(MessageHandler.map("type", "catalog_request"), 1 << 20).get(0))));
        list.add(new Route("GET", pattern("/api/history"), Scope.PEER, (r, g) -> history(r.query())));
        list.add(new Route("GET", pattern("/api/inventory"), Scope.PEER, (r, g) -> inventory(null)));
        list.add(new Route("GET", pattern("/api/inventory/([a-z]+)"), Scope.PEER, (r, g) -> inventory(g[0])));
        list.add(new Route("GET", pattern("/api/firmware/([a-z0-9_]+)"), Scope.PEER, (r, g) -> firmware(g[0])));

        // transports (peer read, local write)
        list.add(new Route("GET", pattern("/api/decks"), Scope.PEER,
                (r, g) -> Response.ok(success(new LinkedHashMap<String, Object>(services.messages().boards())))));
        list.add(new Route("GET", pattern("/api/transports"), Scope.PEER,
                (r, g) -> Response.ok(success(services.transports().all()))));
        list.add(new Route("GET", pattern("/api/connection/status"), Scope.PEER,
                (r, g) -> Response.ok(success(services.transports().connection()))));
        list.add(new Route("GET", pattern("/api/wifi/status"), Scope.PEER,
                (r, g) -> Response.ok(success(castMap(services.transports().all().get("wifi"))))));
        list.add(new Route("PUT", pattern("/api/wifi"), Scope.LOCAL, (r, g) -> setWifi(r.json())));
        list.add(new Route("POST", pattern("/api/wifi/token"), Scope.LOCAL, (r, g) -> rotateToken()));
        list.add(new Route("DELETE", pattern("/api/wifi/token"), Scope.LOCAL, (r, g) -> clearToken()));

        // transport-independent protocol endpoint
        list.add(new Route("POST", pattern("/api/deck"), Scope.PEER, (r, g) -> deck(r)));
        // Virtual monitor for the Deck's VNC app (krfb-virtualmonitor + the Java WsTcpBridge), token required.
        list.add(new Route("POST", pattern("/api/vmonitor"), Scope.PEER, (r, g) -> vmonitor(() -> vmonitor.start(String.valueOf(r.json().getOrDefault("resolution", "320x240"))))));
        list.add(new Route("GET", pattern("/api/vmonitor"), Scope.PEER, (r, g) -> Response.ok(vmonitor.info())));
        list.add(new Route("DELETE", pattern("/api/vmonitor"), Scope.PEER, (r, g) -> Response.ok(vmonitor.stop())));
        // the Decks' consoles (docs/CONSOLE.md): host only, like the macro definitions
        list.add(new Route("POST", pattern("/api/console"), Scope.LOCAL, (r, g) -> consoleLine(r.json())));
        list.add(new Route("GET", pattern("/api/console/log"), Scope.LOCAL, (r, g) -> consoleLog(r.query())));
        list.add(new Route("GET", pattern("/api/console/(\\d{1,5})"), Scope.LOCAL,
                (r, g) -> consoleAnswer(Integer.parseInt(g[0]), intParam(r.query(), "wait_ms", 0, 0, CONSOLE_WAIT_MAX_MS))));
        list.add(new Route("POST", pattern("/api/openaction/attach"), Scope.LOCAL, (r, g) -> openActionAttach(r.json())));
        list.add(new Route("GET", pattern("/api/openaction"), Scope.LOCAL, (r, g) -> openActionStatus()));

        // macro events (id only) and the sanitized macro list (peer)
        list.add(new Route("POST", pattern("/api/event"), Scope.PEER, (r, g) -> event(r)));
        list.add(new Route("POST", pattern("/api/event/batch"), Scope.PEER, (r, g) -> eventBatch(r)));
        list.add(new Route("GET", pattern("/api/event/(\\d+)/config"), Scope.PEER,
                (r, g) -> Response.ok(services.macros().eventConfig(Integer.parseInt(g[0])))));
        list.add(new Route("GET", pattern("/api/macros"), Scope.PEER,
                (r, g) -> Response.ok(services.macros().list(!r.isLoopback()))));

        // macro definitions: executable configuration, host only
        list.add(new Route("POST", pattern("/api/macros"), Scope.LOCAL,
                (r, g) -> Response.ok(services.macros().create(r.json()))));
        list.add(new Route("POST", pattern("/api/macros/reorder"), Scope.LOCAL,
                (r, g) -> Response.ok(services.macros().reorder(r.jsonAny()))));
        list.add(new Route("GET", pattern("/api/macro/(\\d+)"), Scope.LOCAL,
                (r, g) -> Response.ok(services.macros().show(Integer.parseInt(g[0])))));
        list.add(new Route("PUT", pattern("/api/macro/(\\d+)"), Scope.LOCAL,
                (r, g) -> Response.ok(services.macros().update(Integer.parseInt(g[0]), r.json()))));
        list.add(new Route("DELETE", pattern("/api/macro/(\\d+)"), Scope.LOCAL,
                (r, g) -> Response.ok(services.macros().delete(Integer.parseInt(g[0])))));
        return list;
    }

    private static Pattern pattern(String regex) {
        return Pattern.compile(regex);
    }

    // -- handlers ----------------------------------------------------------------

    private Map<String, Object> root() {
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("success", true);
        body.put("message", "ESP32 PC Control Deck API");
        body.put("implementation", "java");
        body.put("api_version", 2);
        body.put("protocol_version", Protocol.VERSION);
        body.put("capabilities", Protocol.CAPABILITIES);
        return body;
    }

    private Response stats() {
        var sample = services.sampler().sample(1.0);
        Map<String, Object> body = success(sample.frame());
        body.put("timestamp", (long) sample.time());
        return Response.ok(body);
    }

    private Response gpu() {
        var sample = services.sampler().sample(1.0);
        List<String> fields = List.of("gpu", "vram", "vram_used_gb", "vram_total_gb", "gpu_temp", "gpu_clock_mhz", "gpu_power_watts");
        Map<String, Object> data = new LinkedHashMap<>();
        for (String field : fields) {
            data.put(field, sample.frame().get(field));
        }
        Map<String, Object> body = success(data);
        body.put("timestamp", (long) sample.time());
        body.put("note", "null means the driver does not expose the value; nothing is estimated.");
        return Response.ok(body);
    }

    private Response metrics() {
        HistoryStore history = services.sampler().history();
        List<Map<String, Object>> metrics = new ArrayList<>();
        for (MetricRegistry.Metric metric : MetricRegistry.all()) {
            Map<String, Object> entry = metric.toMap();
            entry.put("available", history.hasData(metric.id()));
            metrics.add(entry);
        }
        Map<String, Object> defaults = new LinkedHashMap<>();
        for (String component : MetricRegistry.COMPONENTS) {
            defaults.put(component, MetricRegistry.defaultFor(component));
        }
        Map<String, Object> data = new LinkedHashMap<>();
        data.put("metrics", metrics);
        data.put("components", MetricRegistry.COMPONENTS);
        data.put("default_metric", defaults);
        data.put("windows_minutes", MetricRegistry.WINDOWS);
        data.put("retention_minutes", history.retentionSeconds() / 60);
        return Response.ok(success(data));
    }

    /** GET /api/history?metric=cpu_usage&minutes=10&points=60 or ?component=network&minutes=5 */
    private Response history(Map<String, String> query) {
        HistoryStore history = services.sampler().history();
        int retentionMinutes = history.retentionSeconds() / 60;
        int minutes = intParam(query, "minutes", 5, 1, retentionMinutes);
        int points = intParam(query, "points", Protocol.DEFAULT_HISTORY_POINTS, 2, 240);

        // Record a sample now so the newest point is part of the answer.
        services.sampler().sample(1.0);

        try {
            if (query.containsKey("metric")) {
                String id = query.get("metric");
                if (!MetricRegistry.has(id)) {
                    throw HttpError.notFound("Unknown metric '" + id + "'");
                }
                return Response.ok(success(history.series(id, minutes, points, null).toMap()));
            }
            if (query.containsKey("component")) {
                String component = query.get("component");
                if (!MetricRegistry.COMPONENTS.contains(component)) {
                    throw HttpError.notFound("Unknown component '" + component + "'");
                }
                List<Map<String, Object>> series = new ArrayList<>();
                for (String id : MetricRegistry.forComponent(component)) {
                    series.add(history.series(id, minutes, points, null).toMap());
                }
                Map<String, Object> data = new LinkedHashMap<>();
                data.put("component", component);
                data.put("series", series);
                return Response.ok(success(data));
            }
        } catch (IllegalArgumentException error) {
            throw HttpError.badRequest(error.getMessage());
        }
        throw HttpError.badRequest("Specify ?metric=<id> or ?component=<name> (see GET /api/metrics)");
    }

    /**
     * POST /api/messages {"text": "...", "to": "<Deck id>" or "all", "from": "..."}; a voice
     * message: {"kind": "voice", "file": "<a WAV on this PC: 16 kHz, 16-bit, mono>", "to", "from"}.
     */
    private static Response vmonitor(java.util.function.Supplier<Map<String, Object>> action) {
        try {
            return Response.ok(action.get());
        } catch (IllegalStateException e) {
            return Response.error(503, e.getMessage());
        }
    }

    private Response leaveMessage(Map<String, Object> body) {
        try {
            if ("voice".equals(body.get("kind"))) {
                if (!(body.get("file") instanceof String file) || file.isBlank()) {
                    throw new IllegalArgumentException("a voice message needs \"file\": a WAV on this PC");
                }
                byte[] wav;
                try {
                    wav = java.nio.file.Files.readAllBytes(java.nio.file.Path.of(file));
                } catch (java.io.IOException e) {
                    throw new IllegalArgumentException("cannot read " + file + ": " + e.getMessage());
                }
                var voice = services.messages().inbox().addVoice(
                        body.get("to") instanceof String to ? to : null,
                        body.get("from") instanceof String from ? from : null,
                        dev.controldeck.host.inbox.Voice.fromWav(wav));
                return Response.ok(MessageHandler.map("success", true, "id", voice.id(), "length", voice.text()));
            }
            var message = services.messages().inbox().add(
                    body.get("to") instanceof String to ? to : null,
                    body.get("from") instanceof String from ? from : null,
                    body.get("kind") instanceof String kind ? kind : "text",
                    body.get("text") instanceof String text ? text : null);
            return Response.ok(MessageHandler.map("success", true, "id", message.id()));
        } catch (IllegalArgumentException error) {
            throw HttpError.badRequest(error.getMessage());
        }
    }

    /** GET /api/messages/{id}/audio: a voice message as WAV. */
    private Response messageAudio(String id) {
        byte[] pcm = services.messages().inbox().audio(Long.parseLong(id));
        if (pcm == null) {
            throw HttpError.notFound("No audio for message " + id);
        }
        return new Response(200, dev.controldeck.host.inbox.Voice.toWav(pcm), Map.of("Content-Type", "audio/wav"));
    }

    private Response deleteMessage(String id) {
        if (!services.messages().inbox().delete(Long.parseLong(id))) {
            throw HttpError.notFound("No message " + id);
        }
        return Response.ok(MessageHandler.map("success", true));
    }

    /** The language of one Deck's texts, or of all ({"language": "pt"}); the Deck gets it at its next connection. */
    private Response setLanguage(String deckId, Map<String, Object> body) {
        Object language = body == null ? null : body.get("language");
        services.config().setDeckLanguage(deckId, language instanceof String text ? text : null);
        return Response.ok(success(Map.of("language", language)));
    }

    /** The Decks' packs (docs/04-PROTOCOL.md, "The pack"): the packs there are, with their versions per board, and who has which. */
    private Response packs() {
        PackStore store = services.messages().packStore();
        if (store == null) {
            throw HttpError.notFound("No packs");
        }
        Map<String, Object> versions = new LinkedHashMap<>();
        for (String name : store.names()) {
            Map<String, Object> perBoard = new LinkedHashMap<>();
            for (String board : List.of("es3c28p", "e32r35t")) {
                store.packNamed(name, board).ifPresent(p -> perBoard.put(board, Map.of("version", p.version(), "files", p.files(),
                        "bytes", p.bytes().length)));
            }
            versions.put(name, perBoard);
        }
        return Response.ok(success(Map.of("packs", versions, "decks", store.assignments(), "default", PackStore.DEFAULT)));
    }

    /** Gives a Deck a pack ({"pack": "night"}; null or "default" for the default); a connected Deck gets it within a minute. */
    private Response setPack(String deckId, Map<String, Object> body) {
        PackStore store = services.messages().packStore();
        Object pack = body == null ? null : body.get("pack");
        try {
            store.assign(deckId, pack instanceof String text ? text : null);
        } catch (IllegalArgumentException e) {
            throw HttpError.badRequest(e.getMessage());
        }
        return Response.ok(success(Map.of("deck", deckId.toUpperCase(), "pack", store.assigned(deckId.toUpperCase()))));
    }

    /** A board's firmware image, for a Deck that updates itself over Wi-Fi (firmware_offer). */
    private Response firmware(String board) {
        FirmwareStore store = services.messages().firmwareStore();
        FirmwareStore.Image image = store == null ? null : store.find(board).orElse(null);
        if (image == null) {
            throw HttpError.notFound("No firmware for '" + board + "'");
        }
        return Response.bytes(image.bytes());
    }

    private Response inventory(String component) {
        if (component == null) {
            return Response.ok(success(services.inventory().all()));
        }
        Map<String, Object> data = services.inventory().component(component);
        if (data == null) {
            throw HttpError.notFound("Unknown component '" + component + "'");
        }
        Map<String, Object> wrapped = new LinkedHashMap<>();
        wrapped.put(component, data);
        return Response.ok(success(wrapped));
    }

    private Response setWifi(Map<String, Object> settings) {
        Map<String, Object> allowed = new LinkedHashMap<>();
        for (String key : List.of("enabled", "bind", "port")) {
            if (settings.containsKey(key)) {
                allowed.put(key, settings.get(key));
            }
        }
        if (allowed.isEmpty()) {
            throw HttpError.badRequest("Expected one of: enabled, bind, port");
        }
        try {
            services.config().setWifiSettings(allowed);
        } catch (IllegalArgumentException error) {
            throw HttpError.badRequest(error.getMessage());
        }
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("success", true);
        body.put("message", "Wi-Fi transport settings saved. Restart the host for a new bind address or port to take effect.");
        body.put("data", castMap(services.transports().all().get("wifi")));
        return Response.ok(body);
    }

    private Response rotateToken() {
        String token = services.config().rotateWifiToken();
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("success", true);
        body.put("message", "Store this token on the ESP32 now. It is not stored in clear text and cannot be shown again.");
        body.put("token", token);
        body.put("data", castMap(services.transports().all().get("wifi")));
        return Response.ok(body);
    }

    private Response clearToken() {
        services.config().clearWifiToken();
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("success", true);
        body.put("message", "Peer token removed; remote peers are rejected until a new token is created");
        body.put("data", castMap(services.transports().all().get("wifi")));
        return Response.ok(body);
    }

    /** OpenDeck started the plugin, which found this host running: the host takes over. */
    private Response openActionAttach(Map<String, Object> body) {
        if (!(body.get("port") instanceof Number port) || !(body.get("register_event") instanceof String event)
                || !(body.get("uuid") instanceof String uuid)) {
            throw HttpError.badRequest("Send port, register_event and uuid");
        }
        OpenActionClient.connect(services.messages().openAction(), port.intValue(), event, uuid);
        return openActionStatus();
    }

    private Response openActionStatus() {
        return Response.ok(MessageHandler.map("success", true, "attached", services.messages().openAction().attached()));
    }

    /** The longest a console request waits for the Deck's answer (a Wi-Fi Deck polls every 1-2 s). */
    private static final int CONSOLE_WAIT_MAX_MS = 10_000;
    private static final int CONSOLE_WAIT_DEFAULT_MS = 3_000;

    /** POST /api/console {"line": "info", "deck": "<id>" (optional), "wait_ms": 3000}: one line for a Deck. */
    private Response consoleLine(Map<String, Object> body) {
        String deck = body.get("deck") instanceof String d ? d : "";
        String line = body.get("line") instanceof String l ? l : "";
        Long wait = Values.validateInt(body.getOrDefault("wait_ms", CONSOLE_WAIT_DEFAULT_MS));
        if (wait == null || wait < 0 || wait > CONSOLE_WAIT_MAX_MS) {
            throw HttpError.badRequest("wait_ms must be an integer between 0 and " + CONSOLE_WAIT_MAX_MS);
        }
        int id = services.messages().console().submit(deck, line);
        Log.info("[console] " + (deck.isEmpty() ? "any Deck" : Values.truncateCodePoints(deck, 32)) + " > "
                + Values.truncateCodePoints(withoutSecret(line.strip()), 80));
        return consoleAnswer(id, wait.intValue());
    }

    /** A line for the server's log: what follows login and passwd is a password, never written. */
    public static String withoutSecret(String line) {
        String word = line.split("\\s+", 2)[0];
        return (word.equals("login") || word.equals("passwd")) && !line.equals(word) ? word + " ***" : line;
    }

    /** GET /api/console/<id>?wait_ms=N: the answer to a line so far, or all of it once done. */
    private Response consoleAnswer(int id, int waitMs) {
        var answer = awaitConsole(id, waitMs);
        if (answer == null) {
            throw HttpError.notFound("No console request " + id);
        }
        return Response.ok(success(MessageHandler.map("id", answer.id(), "deck", answer.deck(), "done", answer.done(),
                "text", answer.text())));
    }

    private dev.controldeck.host.console.ConsoleHub.Answer awaitConsole(int id, int waitMs) {
        try {
            return services.messages().console().await(id, waitMs);
        } catch (InterruptedException stopped) {
            Thread.currentThread().interrupt();
            throw new HttpError(503, "The server is stopping");
        }
    }

    /** GET /api/console/log?deck=<id>&after=N: the Decks' live log lines after number N. */
    private Response consoleLog(Map<String, String> query) {
        long after = intParam(query, "after", 0, 0, Integer.MAX_VALUE);
        String deck = query.getOrDefault("deck", "");
        var console = services.messages().console();
        List<Map<String, Object>> lines = new ArrayList<>();
        for (var line : console.logs(deck, after)) {
            lines.add(MessageHandler.map("seq", line.seq(), "deck", line.deck(), "time", line.time(), "text", line.text()));
        }
        return Response.ok(success(MessageHandler.map("last", console.lastLogSeq(), "lines", lines)));
    }

    /** The most frames a Deck sends in one request (its stats_request, hello, a key press...). */
    private static final int DECK_FRAMES_PER_REQUEST = 8;

    /**
     * POST /api/deck: a Deck over Wi-Fi. The body is its protocol 4 frames, COBS-encoded and
     * each ended by 0x00 (docs/04-PROTOCOL.md); the answer is the server's frames, the same way.
     */
    private Response deck(Request request) {
        if (request.raw().length > Request.MAX_BODY_BYTES) {
            throw new HttpError(413, "Request body too large");
        }
        // Text clients (KryonOS JavaScript's HTTP is text only): the same COBS frames in base64,
        // sent as text/plain; the answer comes back the same way.
        String type = request.header("content-type");
        if (type != null && type.toLowerCase(java.util.Locale.ROOT).startsWith("text/plain")) {
            byte[] body;
            try {
                body = java.util.Base64.getMimeDecoder().decode(
                        new String(request.raw(), java.nio.charset.StandardCharsets.US_ASCII).trim());
            } catch (IllegalArgumentException e) {
                throw HttpError.badRequest("text/plain bodies must be base64");
            }
            byte[] frames = deckFrames(request, body);
            return Response.text(java.util.Base64.getEncoder().encodeToString(frames));
        }
        return Response.bytes(deckFrames(request, request.raw()));
    }

    /** Handles the Deck's frames in body; returns the server's frames (COBS, 0x00 ended). */
    private byte[] deckFrames(Request request, byte[] body) {
        LineSplitter splitter = new LineSplitter(Request.MAX_BODY_BYTES);
        var handler = services.messages();
        java.io.ByteArrayOutputStream answer = new java.io.ByteArrayOutputStream();
        int frames = 0;
        int sequence = 0;
        String deckId = null;  // who asks: the console lines waiting for it travel in this answer
        List<Map<String, Object>> decoded = new java.util.ArrayList<>();
        for (LineSplitter.Item item : splitter.feed(body, body.length)) {
            if (!(item instanceof LineSplitter.Frame frame)) {
                continue;
            }
            if (++frames > DECK_FRAMES_PER_REQUEST) {
                throw HttpError.badRequest("At most " + DECK_FRAMES_PER_REQUEST + " frames per request");
            }
            decoded.add(Codec4.decode(frame.frame()));
        }
        if (frames == 0) {
            throw HttpError.badRequest("The body holds no protocol 4 frame");
        }
        // A Deck's own token is refused for the whole request, before any frame acts.
        for (Map<String, Object> message : decoded) {
            services.auth().requireOwnDeck(request, deckOf(message));
        }
        for (Map<String, Object> message : decoded) {
            String sender = deckOf(message);
            deckId = sender != null ? sender : deckId;
            if (message != null && "console_output".equals(message.get("type"))) {
                handler.console().output(sender, ((Number) message.get("id")).intValue(),
                        ((Number) message.get("flags")).intValue(), String.valueOf(message.get("text")));
                continue;
            }
            List<Map<String, Object>> replies = message == null
                    ? List.of(MessageHandler.error("unknown_type", "Unsupported message type"))
                    : handler.handle(message, Protocol.HTTP_MAX_FRAME_BYTES);
            for (Map<String, Object> reply : replies) {
                Codec4.Out out = Codec4.encode(reply);
                if (out != null) {
                    answer.writeBytes(Frame4.encodeForLine(out.type(), 0, sequence++, out.payload()));
                }
            }
        }
        if (deckId != null) {
            for (Map<String, Object> input : handler.console().take(deckId)) {
                Codec4.Out out = Codec4.encode(input);
                answer.writeBytes(Frame4.encodeForLine(out.type(), 0, sequence++, out.payload()));
            }
        }
        return answer.toByteArray();
    }

    private static final Pattern DECK_ID = Pattern.compile("^[0-9A-Za-z:_-]{1,32}$");

    /** The Deck id a message carries (hello, board: "id"; stats_request, macro...: "deck"), or null. */
    private static String deckOf(Map<String, Object> message) {
        if (message == null) {
            return null;
        }
        Object type = message.get("type");
        Object id = "hello".equals(type) || "board".equals(type) || "wifi_token_request".equals(type)
                ? message.get("id") : message.get("deck");
        return id instanceof String text && DECK_ID.matcher(text).matches() ? text : null;
    }

    /** POST /api/event {"id": 3}: run macro 3. */
    private Response event(Request request) {
        Map<String, Object> data = request.json();
        Long id = Values.isNumeric(data.get("id")) ? Values.validateInt(data.get("id")) : null;
        if (id == null || id > Integer.MAX_VALUE || id < Integer.MIN_VALUE) {
            throw HttpError.badRequest("Invalid event format. Expected {\"id\": <numeric_id>}");
        }
        Map<String, Object> result = services.macros().handleEvent(id.intValue());
        if (Boolean.TRUE.equals(result.get("success"))) {
            return Response.ok(result);
        }
        boolean missing = String.valueOf(result.get("error")).contains("not found");
        return Response.of(missing ? 404 : 400, result);
    }

    /** POST /api/event/batch: {"events": [1, 2]}, {"events": [{"id": 1}]} or a bare list. */
    private Response eventBatch(Request request) {
        Object data = request.jsonAny();
        Object events = data instanceof Map<?, ?> map ? map.get("events") : data;
        if (!(events instanceof List<?> list)) {
            throw HttpError.badRequest("Invalid batch format. Expected {\"events\": [<id>, ...]}");
        }
        Map<String, Object> result = services.macros().batch(list);
        return Response.of(Boolean.TRUE.equals(result.get("success")) ? 200 : 400, result);
    }

    // -- helpers -----------------------------------------------------------------

    private static int intParam(Map<String, String> query, String name, int fallback, int min, int max) {
        String raw = query.get(name);
        if (raw == null) {
            return fallback;
        }
        Long value = dev.controldeck.host.util.Values.validateInt(raw);
        if (value == null || value < min || value > max) {
            throw HttpError.badRequest(name + " must be an integer between " + min + " and " + max);
        }
        return value.intValue();
    }

    private static Map<String, Object> success(Map<String, Object> data) {
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("success", true);
        body.put("data", data);
        return body;
    }

    @SuppressWarnings("unchecked")
    private static Map<String, Object> castMap(Object value) {
        return value instanceof Map<?, ?> ? (Map<String, Object>) value : new LinkedHashMap<>();
    }
}
