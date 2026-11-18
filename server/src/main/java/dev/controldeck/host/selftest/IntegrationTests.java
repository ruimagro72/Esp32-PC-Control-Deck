package dev.controldeck.host.selftest;

import dev.controldeck.host.Services;
import dev.controldeck.host.config.ConfigService;
import dev.controldeck.host.http.ApiServer;
import dev.controldeck.host.http.HttpError;
import dev.controldeck.host.http.Request;
import dev.controldeck.host.protocol.Codec4;
import dev.controldeck.host.protocol.Frame4;
import dev.controldeck.host.protocol.Wire;
import dev.controldeck.host.transport.serial.LineSplitter;
import dev.controldeck.host.http.Response;
import dev.controldeck.host.http.Router;
import dev.controldeck.host.telemetry.HistoryStore;
import dev.controldeck.host.telemetry.HostMetrics;
import dev.controldeck.host.telemetry.Sampler;

import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.sql.DriverManager;
import java.time.Duration;
import java.util.List;
import java.util.Map;

import static dev.controldeck.host.selftest.Runner.check;
import static dev.controldeck.host.selftest.Runner.close;
import static dev.controldeck.host.selftest.Runner.equal;

/** Auth, router, sampler, history and a live HTTP server, all in one process. */
final class IntegrationTests {

    private IntegrationTests() {
    }

    static void run(Runner r) {
        authRules(r);
        deckTokenRules(r);
        vncBridgeRules(r);
        historyInventoryRules(r);
        routerRules(r);
        samplerLive(r);
        historySeries(r);
        historyTruncation(r);
        liveServer(r);
    }

    private static Services services(Path dir) throws Exception {
        // No settings are written up front: the first use creates the defaults in the database.
        return new Services(new ConfigService(dir.resolve("control-deck.db")), "", dir.resolve("var"));
    }

    /** A request as the board or curl sends it: a body is always declared as JSON. */
    private static Request request(String method, String path, String address, Map<String, String> headers, String body) {
        Map<String, String> all = new java.util.LinkedHashMap<>(headers);
        if (!body.isBlank()) {
            all.putIfAbsent("Content-Type", "application/json");
        }
        return new Request(method, path, Map.of(), all, body, address);
    }

    private static void authRules(Runner r) {
        r.test("LAN peer is refused while the Wi-Fi transport is disabled", () -> {
            Services services = services(Files.createTempDirectory("cd-auth-off"));
            try {
                services.auth().authorize(request("GET", "/api/stats", "10.0.0.5", Map.of(), ""));
                throw new AssertionError("disabled transport must refuse LAN peers");
            } catch (HttpError error) {
                equal(403, error.status(), "status");
            }
        });
        r.test("LAN peer needs the bearer token; wrong tokens are refused and throttled", () -> {
            Services services = services(Files.createTempDirectory("cd-auth-on"));
            services.config().setWifiSettings(Map.of("enabled", true, "bind", "0.0.0.0"));
            String token = services.config().rotateWifiToken();

            services.auth().authorize(request("GET", "/", "10.0.0.5", Map.of("Authorization", "Bearer " + token), ""));

            for (int i = 0; i < 10; i++) {
                expectStatus(401, () -> services.auth().authorize(
                        request("GET", "/", "10.0.0.6", Map.of("Authorization", "Bearer wrong"), "")));
            }
            expectStatus(429, () -> services.auth().authorize(
                    request("GET", "/", "10.0.0.6", Map.of("Authorization", "Bearer " + token), "")));
        });
        r.test("LAN peer: a Deck's own token (given over USB) is accepted, and survives the shared token", () -> {
            Services services = services(Files.createTempDirectory("cd-auth-deck"));
            services.config().setWifiSettings(Map.of("enabled", true, "bind", "0.0.0.0"));
            expectStatus(403, () -> services.auth().authorize(request("GET", "/", "10.0.0.5", Map.of(), "")));
            String first = services.config().issueDeckToken("84C7BB78E0C0");
            String other = services.config().issueDeckToken("A1B2C3D4E5F6");
            services.auth().authorize(request("GET", "/", "10.0.0.5", Map.of("Authorization", "Bearer " + first), ""));
            services.auth().authorize(request("GET", "/", "10.0.0.7", Map.of("Authorization", "Bearer " + other), ""));
            String again = services.config().issueDeckToken("84C7BB78E0C0");
            expectStatus(401, () -> services.auth().authorize(
                    request("GET", "/", "10.0.0.8", Map.of("Authorization", "Bearer " + first), "")));
            services.auth().authorize(request("GET", "/", "10.0.0.5", Map.of("Authorization", "Bearer " + again), ""));
            services.config().rotateWifiToken();
            services.config().clearWifiToken();
            services.auth().authorize(request("GET", "/", "10.0.0.7", Map.of("Authorization", "Bearer " + other), ""));
            try {
                services.config().issueDeckToken("bad id with spaces");
                throw new AssertionError("a malformed Deck id must be refused");
            } catch (IllegalArgumentException expected) {
                // refused
            }
            String stored = String.valueOf(services.config().getTransportSettings());
            check(!stored.contains(other) && stored.contains(ConfigService.sha256Hex(other)),
                    "only the hash of a Deck token is stored");
        });
    }

    private static void deckTokenRules(Runner r) {
        r.test("a Deck's token speaks only for its own Deck; the shared token and loopback name any", () -> {
            Services services = services(Files.createTempDirectory("cd-auth-owner"));
            services.config().setWifiSettings(Map.of("enabled", true, "bind", "0.0.0.0"));
            String mine = services.config().issueDeckToken("84C7BB78E0C0");
            String shared = services.config().rotateWifiToken();

            Request asDeck = request("POST", "/api/deck", "10.0.0.5", Map.of("Authorization", "Bearer " + mine), "");
            services.auth().authorize(asDeck);
            services.auth().requireOwnDeck(asDeck, "84C7BB78E0C0");
            services.auth().requireOwnDeck(asDeck, null);
            expectStatus(403, () -> services.auth().requireOwnDeck(asDeck, "A1B2C3D4E5F6"));

            Request asShared = request("POST", "/api/deck", "10.0.0.6", Map.of("Authorization", "Bearer " + shared), "");
            services.auth().authorize(asShared);
            services.auth().requireOwnDeck(asShared, "A1B2C3D4E5F6");
            services.auth().requireOwnDeck(request("POST", "/api/deck", "127.0.0.1", Map.of(), ""), "A1B2C3D4E5F6");

            check(services.auth().acceptsToken(mine) && services.auth().acceptsToken(shared), "both tokens accepted");
            check(!services.auth().acceptsToken("wrong") && !services.auth().acceptsToken(null), "other tokens refused");
        });
        r.test("router: a Deck's frames that name another Deck are refused before any frame acts", () -> {
            Services services = services(Files.createTempDirectory("cd-deck-owner"));
            services.config().setWifiSettings(Map.of("enabled", true, "bind", "0.0.0.0"));
            String mine = services.config().issueDeckToken("84C7BB78E0C0");
            Router router = new Router(services, false);
            Map<String, String> auth = Map.of("Authorization", "Bearer " + mine);

            byte[] stats = frame(Codec4.STATS_REQUEST, new Wire.Writer().str("A1B2C3D4E5F6").toBytes());
            equal(403, router.handle(new Request("POST", "/api/deck", Map.of(), auth, "", "10.0.0.5", stats)).status(),
                    "a Deck token cannot speak for another Deck");
            byte[] own = frame(Codec4.STATS_REQUEST, new Wire.Writer().str("84C7BB78E0C0").toBytes());
            equal(200, router.handle(new Request("POST", "/api/deck", Map.of(), auth, "", "10.0.0.5", own)).status(),
                    "its own Deck is answered");
        });
    }

    /** The VNC WebSocket bridge: refused without the Deck's token, loopback included. */
    private static void vncBridgeRules(Runner r) {
        r.test("VNC bridge: the upgrade needs the Deck's Bearer token, loopback included", () -> {
            Services services = services(Files.createTempDirectory("cd-vnc-auth"));
            services.config().setWifiSettings(Map.of("enabled", true, "bind", "127.0.0.1"));
            String token = services.config().issueDeckToken("84C7BB78E0C0");
            int free;
            try (java.net.ServerSocket probe = new java.net.ServerSocket(0)) {
                free = probe.getLocalPort();
            }
            try (java.net.ServerSocket target = new java.net.ServerSocket(0);
                 dev.controldeck.host.display.WsTcpBridge bridge = new dev.controldeck.host.display.WsTcpBridge(
                         free, "127.0.0.1", "127.0.0.1", target.getLocalPort(), services.auth()::acceptsToken)) {
                String none = upgrade(free, null);
                check(none != null && none.startsWith("HTTP/1.1 401"), "no token: 401, got " + none);
                String wrong = upgrade(free, "Bearer not-a-token");
                check(wrong != null && wrong.startsWith("HTTP/1.1 401"), "wrong token: 401, got " + wrong);
                String basic = upgrade(free, "Basic " + token);
                check(basic != null && basic.startsWith("HTTP/1.1 401"), "not a Bearer header: 401, got " + basic);
                equal("HTTP/1.1 101 Switching Protocols", upgrade(free, "Bearer " + token), "the Deck's token opens it");
            }
        });
    }

    /** The Deck's history (0x07) and inventory (0x08) requests get their answer frames (0x45, 0x44). */
    private static void historyInventoryRules(Runner r) {
        r.test("history_request 0x07 and inventory_request 0x08 are answered over /api/deck", () -> {
            Services services = services(Files.createTempDirectory("cd-hist-inv"));
            Router router = new Router(services, false);
            byte[] history = frame(Codec4.HISTORY_REQUEST, new Wire.Writer().str("cpu_usage").u8(5).u16(20).toBytes());
            Response answer = router.handle(new Request("POST", "/api/deck", Map.of(), Map.of(), "", "127.0.0.1", history));
            equal(200, answer.status(), "history status");
            equal(List.of(Codec4.HISTORY), answerTypes((byte[]) answer.body()), "history is answered with 0x45");

            byte[] inventory = frame(Codec4.INVENTORY_REQUEST, new Wire.Writer().u8(0).toBytes());
            Response listed = router.handle(new Request("POST", "/api/deck", Map.of(), Map.of(), "", "127.0.0.1", inventory));
            equal(200, listed.status(), "inventory status");
            equal(List.of(Codec4.INVENTORY), answerTypes((byte[]) listed.body()), "inventory is answered with 0x44");
        });
    }

    /** One protocol 4 frame in a /api/deck body. */
    private static byte[] frame(int type, byte[] payload) {
        return Frame4.encodeForLine(type, 0, 0, payload);
    }

    /** Sends an upgrade request and returns the status line the bridge answers (null when none). */
    private static String upgrade(int port, String authorization) throws java.io.IOException {
        try (java.net.Socket socket = new java.net.Socket("127.0.0.1", port)) {
            socket.setSoTimeout(3000);
            String auth = authorization == null ? "" : "Authorization: " + authorization + "\r\n";
            socket.getOutputStream().write(("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n"
                    + "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                    + "Sec-WebSocket-Version: 13\r\n" + auth + "\r\n").getBytes(StandardCharsets.US_ASCII));
            return new java.io.BufferedReader(new java.io.InputStreamReader(socket.getInputStream(),
                    StandardCharsets.US_ASCII)).readLine();
        }
    }

    private static void routerRules(Runner r) {
        r.test("router: public root, local-only Wi-Fi writes, peer message endpoint", () -> {
            Services services = services(Files.createTempDirectory("cd-router"));
            Router router = new Router(services);

            Response root = router.handle(request("GET", "/", "10.0.0.5", Map.of(), ""));
            equal(200, root.status(), "public root needs no token");

            Response statsFromLan = router.handle(request("GET", "/api/stats", "10.0.0.5", Map.of(), ""));
            equal(403, statsFromLan.status(), "telemetry needs a peer token");

            Response wifiFromLan = router.handle(request("PUT", "/api/wifi", "10.0.0.5", Map.of(), "{\"enabled\":true}"));
            equal(403, wifiFromLan.status(), "Wi-Fi settings are host-only");

            Response enable = router.handle(request("PUT", "/api/wifi", "127.0.0.1", Map.of(),
                    "{\"enabled\":true,\"bind\":\"0.0.0.0\"}"));
            equal(200, enable.status(), "host can change Wi-Fi settings");

            Response token = router.handle(request("POST", "/api/wifi/token", "127.0.0.1", Map.of(), ""));
            equal(200, token.status(), "host can create a token");
            String secret = (String) ((Map<?, ?>) token.body()).get("token");

            byte[] askConfig = deckFrames(Codec4.CONFIG_REQUEST, 0x22);  // a known type, then an unknown one
            Response config = router.handle(new Request("POST", "/api/deck", Map.of(),
                    Map.of("Authorization", "Bearer " + secret), "", "10.0.0.5", askConfig));
            equal(200, config.status(), "authenticated Deck frames");
            List<Integer> types = answerTypes((byte[]) config.body());
            equal(List.of(Codec4.CONFIG, Codec4.ERROR), types, "config, then an error for the unknown type");
            equal(400, router.handle(new Request("POST", "/api/deck", Map.of(), Map.of(), "", "127.0.0.1",
                    new byte[] {'x'})).status(), "no frame in the body");
            equal(401, router.handle(new Request("POST", "/api/deck", Map.of(), Map.of(), "", "10.0.0.5",
                    askConfig)).status(), "a Deck needs its token");

            Response wrongMethod = router.handle(request("DELETE", "/api/deck", "127.0.0.1", Map.of(), ""));
            equal(405, wrongMethod.status(), "wrong method");
            equal("POST", wrongMethod.headers().get("Allow"), "Allow header");

            equal(404, router.handle(request("GET", "/api/unknown", "127.0.0.1", Map.of(), "")).status(), "unknown path");
        });
    }

    private static void samplerLive(Runner r) {
        r.test("sampler produces a stats frame from the real /proc (Linux only)", () -> {
            if (!HostMetrics.platformSupportsLinux()) {
                throw new Runner.Skip("not Linux");
            }
            Path dir = Files.createTempDirectory("cd-sampler");
            Sampler sampler = new Sampler(new HostMetrics("", "auto", null),
                    new HistoryStore(dir.resolve("history.sqlite"), 30), dir);
            Sampler.Result result = sampler.sample(0.0);
            equal("stats", result.frame().get("type"), "frame type");
            check(result.frame().get("ram") instanceof Number, "ram usage is measured on Linux");
            sampler.history().close();
        });
    }

    private static void historySeries(Runner r) {
        r.test("history buckets samples and keeps gaps as null (needs sqlite-jdbc)", () -> {
            boolean driver = DriverManager.drivers().anyMatch(d -> d.getClass().getName().contains("sqlite"));
            if (!driver) {
                throw new Runner.Skip("sqlite-jdbc not on the class path; run with the shaded jar");
            }
            Path dir = Files.createTempDirectory("cd-history");
            HistoryStore history = new HistoryStore(dir.resolve("history.sqlite"), 30);
            double base = 1_700_000_000.0;
            history.append(base - 60, Map.of("ram", 40.0));
            history.append(base - 30, Map.of("ram", 50.0));
            history.append(base, Map.of("ram", 60.0));

            HistoryStore.Series series = history.series("ram_usage", 5, 10, base);
            equal(3, series.samplesInWindow(), "samples in window");
            equal(40.0, series.values().get(8), "bucket 8");
            equal(55.0, series.values().get(9), "last bucket averages the two newest samples");
            equal(null, series.values().get(0), "empty bucket is a gap");
            equal(60.0, series.latest(), "latest value");
            history.close();
        });
        r.test("history shows only the current run: data before a silence over 3 min is left out (needs sqlite-jdbc)", () -> {
            boolean driver = DriverManager.drivers().anyMatch(d -> d.getClass().getName().contains("sqlite"));
            if (!driver) {
                throw new Runner.Skip("sqlite-jdbc not on the class path; run with the shaded jar");
            }
            Path dir = Files.createTempDirectory("cd-history-runs");
            HistoryStore history = new HistoryStore(dir.resolve("history.sqlite"), 30);
            double now = 1_700_000_000.0;
            // An earlier run 18 to 16 minutes ago, then the host was stopped for 10 minutes.
            for (int s = 18 * 60; s >= 16 * 60; s -= 30) {
                history.append(now - s, Map.of("cpu", 90.0));
            }
            // The current run: the last 6 minutes, with a 2-minute pause inside it.
            for (int s = 6 * 60; s >= 0; s -= 10) {
                if (s < 4 * 60 && s > 2 * 60) {
                    continue;
                }
                history.append(now - s, Map.of("cpu", 20.0));
            }

            HistoryStore.Series series = history.series("cpu_usage", 20, 20, now);
            for (int i = 0; i < 14; i++) {
                equal(null, series.values().get(i), "minute " + i + " is before the current run");
            }
            equal(20.0, series.values().get(14), "the current run starts 6 minutes ago");
            equal(null, series.values().get(17), "a short pause stays a gap");
            equal(20.0, series.values().get(19), "newest minute");
            check(series.samplesInWindow() < 40, "the earlier run is not counted: " + series.samplesInWindow());

            HistoryStore.Series oneRun = history.series("cpu_usage", 5, 10, now);
            equal(20.0, oneRun.values().get(0), "a window inside the current run is unchanged");
            history.close();
        });
    }

    private static void historyTruncation(Runner r) {
        r.test("setup truncation removes the old history and sampling state but keeps the settings (needs sqlite-jdbc)", () -> {
            boolean driver = DriverManager.drivers().anyMatch(d -> d.getClass().getName().contains("sqlite"));
            if (!driver) {
                throw new Runner.Skip("sqlite-jdbc not on the class path; run with the shaded jar");
            }
            Path dir = Files.createTempDirectory("cd-truncate");
            Services services = services(dir);
            try {
                services.config().setWifiSettings(Map.of("port", 9191));
                Files.createDirectories(services.varDir());
                Files.writeString(services.varDir().resolve("sample_state.json"), "{}");
                Files.writeString(services.varDir().resolve("latest.json"), "{}");

                double now = System.currentTimeMillis() / 1000.0;
                HistoryStore history = services.sampler().history();
                history.append(now - 2, Map.of("ram", 40.0));
                history.append(now - 1, Map.of("ram", 50.0));
                equal(2, history.series("ram_usage", 5, 10, null).samplesInWindow(), "samples before");

                equal(2, services.truncateHistory(), "samples removed");
                equal(0, history.series("ram_usage", 5, 10, null).samplesInWindow(), "samples after");
                check(!Files.exists(services.varDir().resolve("sample_state.json")), "sampling state removed");
                check(!Files.exists(services.varDir().resolve("latest.json")), "cached frame removed");
                equal(9191, services.config().getWifiSettings().port(), "settings kept");
            } finally {
                services.close();
            }
        });
    }

    private static void liveServer(Runner r) {
        r.test("HTTP server answers over a real socket", () -> {
            Services services = services(Files.createTempDirectory("cd-live"));
            ApiServer server = new ApiServer(services, "127.0.0.1", 0);
            server.start();
            try {
                HttpClient client = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(5)).build();
                int port = server.port();

                HttpResponse<String> root = client.send(HttpRequest.newBuilder(URI.create("http://127.0.0.1:" + port + "/"))
                        .GET().build(), HttpResponse.BodyHandlers.ofString(StandardCharsets.UTF_8));
                equal(200, root.statusCode(), "GET / status");
                check(root.body().contains("ESP32 PC Control Deck API"), "GET / body");
                equal("application/json; charset=utf-8", root.headers().firstValue("Content-Type").orElse(""), "content type");

                HttpResponse<byte[]> deck = client.send(HttpRequest.newBuilder(URI.create("http://127.0.0.1:" + port + "/api/deck"))
                        .header("Content-Type", "application/octet-stream")
                        .POST(HttpRequest.BodyPublishers.ofByteArray(deckFrames(Codec4.CONFIG_REQUEST))).build(),
                        HttpResponse.BodyHandlers.ofByteArray());
                equal(200, deck.statusCode(), "POST /api/deck status");
                equal("application/octet-stream", deck.headers().firstValue("Content-Type").orElse(""), "binary answer");
                equal(List.of(Codec4.CONFIG), answerTypes(deck.body()), "the config frame comes back");
            } finally {
                server.stop();
                services.close();
            }
        });
    }

    /** A Deck's request body: one empty frame per type, as the Deck sends them. */
    private static byte[] deckFrames(int... types) {
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        int seq = 0;
        for (int type : types) {
            out.writeBytes(Frame4.encodeForLine(type, 0, seq++, new byte[0]));
        }
        return out.toByteArray();
    }

    /** The types of the frames in the server's answer. */
    private static List<Integer> answerTypes(byte[] body) {
        List<Integer> types = new java.util.ArrayList<>();
        for (LineSplitter.Item item : new LineSplitter(65536).feed(body, body.length)) {
            if (item instanceof LineSplitter.Frame frame) {
                types.add(frame.frame().type());
            }
        }
        return types;
    }

    private static void expectStatus(int status, Runnable action) {
        try {
            action.run();
        } catch (HttpError error) {
            equal(status, error.status(), "HTTP status");
            return;
        }
        throw new AssertionError("expected HTTP " + status + " but the call succeeded");
    }
}
