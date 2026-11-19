package dev.controldeck.host.http;

import static dev.controldeck.host.http.RouteSupport.castMap;
import static dev.controldeck.host.http.RouteSupport.intParam;
import static dev.controldeck.host.http.RouteSupport.success;

import dev.controldeck.host.Services;
import dev.controldeck.host.protocol.Protocol;
import dev.controldeck.host.protocol.MessageHandler;
import dev.controldeck.host.macro.MacroService;
import dev.controldeck.host.util.Log;

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
 *
 * The handlers live in the domain classes of this package (TelemetryRoutes, MessageRoutes,
 * DeckConfigRoutes, WifiRoutes, ConsoleRoutes, OpenActionRoutes, VirtualMonitorRoutes,
 * MacroRoutes, DeckRoutes); this class keeps the route table, the access rules and dispatch.
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

    private final TelemetryRoutes telemetry;
    private final MessageRoutes messages;
    private final DeckConfigRoutes deckConfig;
    private final WifiRoutes wifi;
    private final ConsoleRoutes console;
    private final OpenActionRoutes openAction;
    private final VirtualMonitorRoutes monitor;
    private final MacroRoutes macros;
    private final DeckRoutes deckRoutes;

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
        this.telemetry = new TelemetryRoutes(services);
        this.messages = new MessageRoutes(services);
        this.deckConfig = new DeckConfigRoutes(services);
        this.wifi = new WifiRoutes(services);
        this.console = new ConsoleRoutes(services);
        this.openAction = new OpenActionRoutes(services);
        this.monitor = new VirtualMonitorRoutes(vmonitor);
        this.macros = new MacroRoutes(services);
        this.deckRoutes = new DeckRoutes(services);
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
        list.add(new Route("GET", pattern("/api/stats"), Scope.PEER, (r, g) -> telemetry.stats()));
        list.add(new Route("GET", pattern("/api/gpu"), Scope.PEER, (r, g) -> telemetry.gpu()));
        list.add(new Route("GET", pattern("/api/metrics"), Scope.PEER, (r, g) -> telemetry.metrics()));
        list.add(new Route("GET", pattern("/api/messages"), Scope.LOCAL,
                (r, g) -> Response.ok(MessageHandler.map("success", true, "data", services.messages().inbox().all()))));
        list.add(new Route("POST", pattern("/api/messages"), Scope.LOCAL, (r, g) -> messages.leave(r.json())));
        list.add(new Route("DELETE", pattern("/api/messages/(\\d+)"), Scope.LOCAL, (r, g) -> messages.delete(g[0])));
        list.add(new Route("GET", pattern("/api/messages/(\\d+)/audio"), Scope.LOCAL, (r, g) -> messages.audio(g[0])));
        list.add(new Route("PUT", pattern("/api/language"), Scope.LOCAL, (r, g) -> deckConfig.setLanguage(null, r.json())));
        list.add(new Route("PUT", pattern("/api/decks/([0-9A-Fa-f]{1,24})/language"), Scope.LOCAL,
                (r, g) -> deckConfig.setLanguage(g[0], r.json())));
        list.add(new Route("GET", pattern("/api/packs"), Scope.LOCAL, (r, g) -> deckConfig.packs()));
        list.add(new Route("PUT", pattern("/api/decks/([0-9A-Fa-f]{1,24})/pack"), Scope.LOCAL,
                (r, g) -> deckConfig.setPack(g[0], r.json())));
        list.add(new Route("GET", pattern("/api/catalog"), Scope.PEER,
                (r, g) -> Response.ok(services.messages().handle(MessageHandler.map("type", "catalog_request"), 1 << 20).get(0))));
        list.add(new Route("GET", pattern("/api/history"), Scope.PEER, (r, g) -> telemetry.history(r.query())));
        list.add(new Route("GET", pattern("/api/inventory"), Scope.PEER, (r, g) -> telemetry.inventory(null)));
        list.add(new Route("GET", pattern("/api/inventory/([a-z]+)"), Scope.PEER, (r, g) -> telemetry.inventory(g[0])));
        list.add(new Route("GET", pattern("/api/firmware/([a-z0-9_]+)"), Scope.PEER, (r, g) -> deckRoutes.firmware(g[0])));

        // transports (peer read, local write)
        list.add(new Route("GET", pattern("/api/decks"), Scope.PEER,
                (r, g) -> Response.ok(success(new LinkedHashMap<String, Object>(services.messages().boards())))));
        list.add(new Route("GET", pattern("/api/transports"), Scope.PEER,
                (r, g) -> Response.ok(success(services.transports().all()))));
        list.add(new Route("GET", pattern("/api/connection/status"), Scope.PEER,
                (r, g) -> Response.ok(success(services.transports().connection()))));
        list.add(new Route("GET", pattern("/api/wifi/status"), Scope.PEER,
                (r, g) -> Response.ok(success(castMap(services.transports().all().get("wifi"))))));
        list.add(new Route("PUT", pattern("/api/wifi"), Scope.LOCAL, (r, g) -> wifi.setWifi(r.json())));
        list.add(new Route("POST", pattern("/api/wifi/token"), Scope.LOCAL, (r, g) -> wifi.rotateToken()));
        list.add(new Route("DELETE", pattern("/api/wifi/token"), Scope.LOCAL, (r, g) -> wifi.clearToken()));

        // transport-independent protocol endpoint
        list.add(new Route("POST", pattern("/api/deck"), Scope.PEER, (r, g) -> deckRoutes.deck(r)));
        // Virtual monitor for the Deck's VNC app, token required.
        list.add(new Route("POST", pattern("/api/vmonitor"), Scope.PEER, (r, g) -> monitor.start(r.json())));
        list.add(new Route("GET", pattern("/api/vmonitor"), Scope.PEER, (r, g) -> monitor.info()));
        list.add(new Route("DELETE", pattern("/api/vmonitor"), Scope.PEER, (r, g) -> monitor.stop()));
        // the Decks' consoles (docs/CONSOLE.md): host only, like the macro definitions
        list.add(new Route("POST", pattern("/api/console"), Scope.LOCAL, (r, g) -> console.line(r.json())));
        list.add(new Route("GET", pattern("/api/console/log"), Scope.LOCAL, (r, g) -> console.log(r.query())));
        list.add(new Route("GET", pattern("/api/console/(\\d{1,5})"), Scope.LOCAL,
                (r, g) -> console.answer(Integer.parseInt(g[0]),
                        intParam(r.query(), "wait_ms", 0, 0, ConsoleRoutes.CONSOLE_WAIT_MAX_MS))));
        list.add(new Route("POST", pattern("/api/openaction/attach"), Scope.LOCAL, (r, g) -> openAction.attach(r.json())));
        list.add(new Route("GET", pattern("/api/openaction"), Scope.LOCAL, (r, g) -> openAction.status()));

        // macro events (id only) and the sanitized macro list (peer)
        list.add(new Route("POST", pattern("/api/event"), Scope.PEER, (r, g) -> macros.event(r)));
        list.add(new Route("POST", pattern("/api/event/batch"), Scope.PEER, (r, g) -> macros.eventBatch(r)));
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

    /** A line for the server's log: what follows login and passwd is a password, never written. */
    public static String withoutSecret(String line) {
        return ConsoleRoutes.withoutSecret(line);
    }
}
