package dev.controldeck.host.selftest;

import static dev.controldeck.host.selftest.Runner.equal;

import dev.controldeck.host.Services;
import dev.controldeck.host.config.ConfigService;
import dev.controldeck.host.http.Request;
import dev.controldeck.host.http.Response;
import dev.controldeck.host.http.Router;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;

/** Route scopes (PUBLIC, PEER, LOCAL) and the body size limit, checked through the Router. */
final class ScopeTests {

    private ScopeTests() {
    }

    static void run(Runner r) {
        r.test("scope: a PEER route refuses a source that is not a peer; a LOCAL route refuses a LAN source, token or not", () -> {
            Services services = services(Files.createTempDirectory("cd-scope"));
            Router router = new Router(services);

            // PEER without a token from a LAN address: refused, Wi-Fi settings off by default
            equal(403, router.handle(request("GET", "/api/decks", "10.0.0.5", Map.of(), "")).status(),
                    "a LAN source is not a peer while Wi-Fi is off");

            // Wi-Fi on with a token: the token makes the LAN address a peer, but not a host
            services.config().setWifiSettings(Map.of("enabled", true, "bind", "0.0.0.0"));
            String token = services.config().rotateWifiToken();
            Map<String, String> peer = Map.of("Authorization", "Bearer " + token);
            equal(401, router.handle(request("GET", "/api/decks", "10.0.0.5", Map.of(), "")).status(),
                    "a PEER route needs the token from a LAN address");
            equal(200, router.handle(request("GET", "/api/decks", "10.0.0.5", peer, "")).status(),
                    "a PEER route accepts the token from a LAN address");
            equal(403, router.handle(request("GET", "/api/messages", "10.0.0.5", peer, "")).status(),
                    "a LOCAL route refuses a valid token from a LAN address");
            equal(403, router.handle(request("PUT", "/api/wifi", "10.0.0.5", peer, "{\"enabled\":false}")).status(),
                    "Wi-Fi settings stay host-only even with the token");
            equal(200, router.handle(request("GET", "/api/messages", "127.0.0.1", Map.of(), "")).status(),
                    "the host itself reads a LOCAL route");
            services.close();
        });

        r.test("body limit: a body over the limit is refused with 413 before the handler runs", () -> {
            Services services = services(Files.createTempDirectory("cd-body"));
            Router router = new Router(services);
            String pad = "x".repeat(Request.MAX_BODY_BYTES);
            String oversize = "{\"label\":\"" + pad + "\"}";
            equal(413, router.handle(request("PUT", "/api/wifi", "127.0.0.1", Map.of(), oversize)).status(),
                    "oversize body on a host route");
            services.close();
        });
    }

    private static Services services(Path dir) throws Exception {
        return new Services(new ConfigService(dir.resolve("control-deck.db")), "", dir.resolve("var"));
    }

    private static Request request(String method, String path, String address, Map<String, String> headers, String body) {
        Map<String, String> all = new java.util.LinkedHashMap<>(headers);
        if (!body.isBlank()) {
            all.putIfAbsent("Content-Type", "application/json");
        }
        return new Request(method, path, Map.of(), all, body, address);
    }
}
