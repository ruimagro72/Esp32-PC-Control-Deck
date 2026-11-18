package dev.controldeck.host.selftest;

import dev.controldeck.host.Services;
import dev.controldeck.host.config.ConfigService;
import dev.controldeck.host.http.HttpError;
import dev.controldeck.host.http.Request;
import dev.controldeck.host.http.RequestGuard;
import dev.controldeck.host.http.Response;
import dev.controldeck.host.http.Router;
import dev.controldeck.host.macro.Macro;
import dev.controldeck.host.macro.MacroExecutor;
import dev.controldeck.host.macro.MacroService;
import dev.controldeck.host.protocol.MessageHandler;
import dev.controldeck.host.util.Json;

import java.nio.file.Files;
import java.nio.file.Path;
import java.sql.DriverManager;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import static dev.controldeck.host.selftest.Runner.check;
import static dev.controldeck.host.selftest.Runner.equal;

/** Phase J6: the macro executor, the macro definitions and events, and the guard that keeps web pages out. */
final class MacroTests {

    private MacroTests() {
    }

    static void run(Runner r) {
        executor(r);
        realLaunch(r);
        definitions(r);
        protocol(r);
        guard(r);
        routes(r);
    }

    // -- executor ---------------------------------------------------------------------

    /** An executor on "Linux" whose PATH holds only the given fake programs; launches are recorded. */
    private static final class Fake {
        final List<List<String>> launched = Collections.synchronizedList(new ArrayList<>());
        final List<Path> directories = Collections.synchronizedList(new ArrayList<>());
        final MacroExecutor executor;

        Fake(Path dir, boolean display, boolean healthy, String... programs) throws Exception {
            Path bin = Files.createDirectories(dir.resolve("bin"));
            for (String program : programs) {
                Path file = bin.resolve(program);
                Files.writeString(file, "#!/bin/sh\n");
                file.toFile().setExecutable(true);
            }
            Map<String, String> env = new LinkedHashMap<>();
            env.put("PATH", bin.toString());
            if (display) {
                env.put("DISPLAY", ":0");
            }
            executor = new MacroExecutor(MacroExecutor.Platform.LINUX, env, (command, directory) -> {
                launched.add(List.copyOf(command));
                directories.add(directory);
                return true;
            }, url -> healthy);
        }
    }

    private static Macro macro(Object... pairs) {
        Map<String, Object> data = MessageHandler.map(pairs);
        data.putIfAbsent("id", 1);
        data.putIfAbsent("label", "TEST");
        return Macro.fromMap(data);
    }

    private static void executor(Runner r) {
        r.test("macro command: the argument list runs as is, never through a shell string", () -> {
            Fake fake = new Fake(Files.createTempDirectory("cd-mx-cmd"), false, false, "ollama");
            MacroExecutor.Result result = fake.executor.execute(macro("action", "command",
                    "value", List.of("ollama", "run", "llama3; rm -rf ~")));
            check(result.executed(), "executed: " + result);
            equal("Started: ollama", result.message(), "message");
            equal(List.of(List.of("ollama", "run", "llama3; rm -rf ~")), fake.launched, "exact arguments");
        });
        r.test("macro command: per-platform values pick linux, then default; numbers become text", () -> {
            Fake fake = new Fake(Files.createTempDirectory("cd-mx-plat"), false, false, "lin", "dflt");
            fake.executor.execute(macro("action", "command",
                    "value", Map.of("linux", List.of("lin", 5), "windows", List.of("win"))));
            fake.executor.execute(macro("action", "command",
                    "value", Map.of("default", List.of("dflt", 1.5), "macos", List.of("mac"))));
            equal(List.of(List.of("lin", "5"), List.of("dflt", "1.5")), fake.launched, "chosen commands");

            MacroExecutor.Result none = fake.executor.execute(macro("action", "command",
                    "value", Map.of("windows", List.of("win"))));
            equal("No command configured for this platform", none.message(), "no linux entry");
            MacroExecutor.Result mixed = fake.executor.execute(macro("action", "command",
                    "value", List.of("lin", Map.of("x", 1))));
            check(!mixed.executed(), "an argument must be text or a number");
        });
        r.test("macro command: missing program and missing working directory are reported, nothing runs", () -> {
            Path dir = Files.createTempDirectory("cd-mx-miss");
            Fake fake = new Fake(dir, false, false, "tool");
            equal("Command not found: nope", fake.executor.execute(macro("action", "command",
                    "value", List.of("nope"))).message(), "missing program");
            equal("Working directory does not exist", fake.executor.execute(macro("action", "command",
                    "value", List.of("tool"), "working_directory", dir.resolve("absent").toString())).message(),
                    "missing directory");
            fake.executor.execute(macro("action", "command", "value", List.of("tool"),
                    "working_directory", Map.of("linux", dir.toString())));
            equal(1, fake.launched.size(), "only the valid macro ran");
            equal(dir, fake.directories.get(0), "per-platform working directory");
        });
        r.test("macro command: new_console wraps it in a terminal and needs a graphical session", () -> {
            Path dir = Files.createTempDirectory("cd-mx-term");
            Fake noDisplay = new Fake(dir.resolve("a"), false, false, "htop", "xterm");
            check(noDisplay.executor.execute(macro("action", "command", "value", List.of("htop"), "new_console", true))
                    .message().contains("graphical session"), "no display");
            Fake fake = new Fake(dir.resolve("b"), true, false, "htop", "xterm");
            fake.executor.execute(macro("action", "command", "value", List.of("htop"), "new_console", true));
            equal(List.of(List.of("xterm", "-e", "htop")), fake.launched, "terminal + command");
        });
        r.test("macro url: only http(s), only with a graphical session, opened with xdg-open", () -> {
            Path dir = Files.createTempDirectory("cd-mx-url");
            Fake noDisplay = new Fake(dir.resolve("a"), false, false, "xdg-open");
            check(noDisplay.executor.execute(macro("action", "url", "value", "https://chat.openai.com"))
                    .message().contains("No graphical session"), "no display");
            Fake fake = new Fake(dir.resolve("b"), true, false, "xdg-open");
            check(!fake.executor.execute(macro("action", "url", "value", "file:///etc/passwd")).executed(), "file: refused");
            check(!fake.executor.execute(macro("action", "url", "value", "javascript:alert(1)")).executed(), "javascript: refused");
            MacroExecutor.Result ok = fake.executor.execute(macro("action", "url", "value", "https://chat.openai.com"));
            equal("Opened URL: https://chat.openai.com", ok.message(), "message");
            equal(List.of(List.of("xdg-open", "https://chat.openai.com")), fake.launched, "launched");
        });
        r.test("macro service: a healthy service is not started again; otherwise the command starts it", () -> {
            Path dir = Files.createTempDirectory("cd-mx-svc");
            Fake healthy = new Fake(dir.resolve("a"), false, true, "ollama");
            MacroExecutor.Result running = healthy.executor.execute(macro("action", "service",
                    "value", List.of("ollama", "serve"), "health_url", "http://127.0.0.1:11434/"));
            equal("already_running", running.status(), "status");
            equal(0, healthy.launched.size(), "nothing started");

            Fake down = new Fake(dir.resolve("b"), false, false, "ollama");
            MacroExecutor.Result started = down.executor.execute(macro("action", "service",
                    "value", List.of("ollama", "serve"), "health_url", "http://127.0.0.1:11434/"));
            equal("started", started.status(), "status");
            equal(List.of(List.of("ollama", "serve")), down.launched, "start command");

            MacroExecutor.Result noHealth = down.executor.execute(macro("action", "service", "value", List.of("ollama")));
            check(!noHealth.executed() && noHealth.message().contains("Health URL"), "health URL required");
        });
        r.test("macro hotkey/text: reported as unsupported, unknown actions refused", () -> {
            Fake fake = new Fake(Files.createTempDirectory("cd-mx-hot"), true, false);
            MacroExecutor.Result hotkey = fake.executor.execute(macro("action", "hotkey", "value", "ctrl+alt+t"));
            check(hotkey.unsupported() && !hotkey.executed(), "hotkey unsupported");
            try {
                macro("action", "shell", "value", "ls");
                throw new AssertionError("an unknown action must be refused");
            } catch (IllegalArgumentException expected) {
                check(expected.getMessage().contains("Unknown macro action"), "message");
            }
            try {
                macro("action", "url", "label", "x".repeat(23));
                throw new AssertionError("a label above 22 characters must be refused");
            } catch (IllegalArgumentException expected) {
                check(expected.getMessage().contains("22"), "message");
            }
        });
    }

    private static void realLaunch(Runner r) {
        r.test("macro command: a real detached start runs the program and returns at once (Unix only)", () -> {
            if (MacroExecutor.Platform.detect(System.getProperty("os.name")) == MacroExecutor.Platform.WINDOWS) {
                throw new Runner.Skip("Windows");
            }
            Path dir = Files.createTempDirectory("cd-mx-real");
            Path marker = dir.resolve("ran.txt");
            MacroExecutor executor = new MacroExecutor();
            long start = System.nanoTime();
            MacroExecutor.Result result = executor.execute(macro("action", "command",
                    "value", List.of("sh", "-c", "sleep 0.3; echo ok > \"$0\"", marker.toString())));
            long tookMs = (System.nanoTime() - start) / 1_000_000;
            check(result.executed(), "executed: " + result);
            check(tookMs < 300, "the host did not wait for the command (" + tookMs + " ms)");
            for (int i = 0; i < 40 && !Files.exists(marker); i++) {
                Thread.sleep(50);
            }
            check(Files.exists(marker), "the detached command ran");
        });
    }

    // -- definitions (needs sqlite-jdbc) ----------------------------------------------------

    private static void requireSqlite() {
        if (DriverManager.drivers().noneMatch(d -> d.getClass().getName().contains("sqlite"))) {
            throw new Runner.Skip("sqlite-jdbc not on the class path; run with the shaded jar");
        }
    }

    private static MacroService service(Path dir, Fake fake) {
        return new MacroService(new ConfigService(dir.resolve("control-deck.db")), fake.executor);
    }

    private static void definitions(Runner r) {
        r.test("macro definitions: create, conflict, validation, update, reorder, delete (needs sqlite-jdbc)", () -> {
            requireSqlite();
            Path dir = Files.createTempDirectory("cd-mx-defs");
            MacroService macros = service(dir, new Fake(dir, false, false, "tool"));

            macros.create(MessageHandler.map("id", 1, "label", "ONE", "action", "command", "value", List.of("tool")));
            macros.create(MessageHandler.map("id", 2, "label", "TWO", "action", "url", "value", "https://a.example"));
            equal(2, macros.list(false).get("count"), "two macros");

            expect(MacroService.Conflict.class, () -> macros.create(MessageHandler.map("id", 1, "label", "AGAIN")));
            expect(IllegalArgumentException.class, () -> macros.create(MessageHandler.map("id", 7, "label", "SEVEN")));
            expect(IllegalArgumentException.class, () -> macros.create(MessageHandler.map("id", 3, "label", "")));
            expect(IllegalArgumentException.class, () -> macros.create(MessageHandler.map("id", 3, "label", "X",
                    "action", "shell")));
            equal(2, macros.list(false).get("count"), "refused macros were not stored");

            macros.update(2, MessageHandler.map("label", "OPEN A", "hidden", "1", "id", 5));
            Map<?, ?> two = (Map<?, ?>) macros.show(2).get("data");
            equal("OPEN A", two.get("label"), "label changed");
            equal(true, two.get("hidden"), "hidden cast to a boolean");
            equal(2, two.get("id"), "the id cannot be changed by an update");
            expect(IllegalArgumentException.class, () -> macros.update(2, MessageHandler.map("label", "y".repeat(30))));
            expect(MacroService.NotFound.class, () -> macros.update(6, MessageHandler.map("label", "Z")));

            expect(IllegalArgumentException.class, () -> macros.reorder(List.of(2)));
            macros.reorder(MessageHandler.map("ids", List.of(2, 1)));
            List<?> ordered = (List<?>) macros.list(false).get("data");
            equal(2, ((Map<?, ?>) ordered.get(0)).get("id"), "new order");

            List<?> sanitized = (List<?>) macros.list(true).get("data");
            check(!((Map<?, ?>) sanitized.get(0)).containsKey("value"), "a remote peer does not see the value");

            macros.delete(2);
            expect(MacroService.NotFound.class, () -> macros.delete(2));
            equal(1, macros.list(false).get("count"), "one left");
        });
        r.test("macro events: run, not found, out of range, unsupported, batch (needs sqlite-jdbc)", () -> {
            requireSqlite();
            Path dir = Files.createTempDirectory("cd-mx-events");
            Fake fake = new Fake(dir, false, false, "tool");
            MacroService macros = service(dir, fake);
            macros.create(MessageHandler.map("id", 1, "label", "RUN", "action", "command", "value", List.of("tool", "x")));
            macros.create(MessageHandler.map("id", 2, "label", "KEYS", "action", "hotkey", "value", "ctrl+c"));

            Map<String, Object> run = macros.handleEvent(1);
            equal(true, run.get("success"), "success");
            equal(true, run.get("action_executed"), "executed");
            equal(List.of(List.of("tool", "x")), fake.launched, "launched");

            check(String.valueOf(macros.handleEvent(4).get("error")).contains("not found"), "not configured");
            check(String.valueOf(macros.handleEvent(9).get("error")).contains("between 1 and 6"), "out of range");
            Map<String, Object> keys = macros.handleEvent(2);
            equal(false, keys.get("success"), "unsupported is a failure");
            equal(false, keys.get("action_executed"), "nothing ran");

            Map<String, Object> batch = macros.batch(List.of(1, Map.of("id", 1), "abc", 5));
            equal(2, batch.get("processed"), "two ran");
            equal(4, batch.get("total"), "four events");
            equal(false, macros.batch(List.of(1, 1, 1, 1, 1, 1, 1)).get("success"), "at most six events");
        });
    }

    private static void protocol(Runner r) {
        r.test("protocol: a macro event is answered with macro_result, ok only when it really ran (needs sqlite-jdbc)", () -> {
            requireSqlite();
            Path dir = Files.createTempDirectory("cd-mx-proto");
            Fake fake = new Fake(dir, false, false, "tool");
            ConfigService config = new ConfigService(dir.resolve("control-deck.db"));
            MacroService macros = new MacroService(config, fake.executor);
            macros.create(MessageHandler.map("id", 1, "label", "RUN", "action", "command", "value", List.of("tool")));
            macros.create(MessageHandler.map("id", 2, "label", "GONE", "action", "command", "value", List.of("absent")));
            MessageHandler handler = new MessageHandler(config, null, null, macros);

            Map<String, Object> ok = handler.handle(Map.of("type", "macro", "id", 1), 1800).get(0);
            equal(Map.of("type", "macro_result", "id", 1, "ok", true, "executed", true), ok, "ran");
            Map<String, Object> failed = handler.handle(Map.of("type", "macro", "id", 2), 1800).get(0);
            equal(false, failed.get("ok"), "not ok");
            equal("Command not found: absent", failed.get("detail"), "detail says why");
            equal("invalid_request", handler.handle(Map.of("type", "macro", "id", 0), 1800).get(0).get("code"), "id 0");

            List<?> list = (List<?>) handler.configFrame().get("macros");
            equal(2, list.size(), "config frame lists the macros");
            equal(Map.of("id", 1, "label", "RUN"), list.get(0), "id and label only");
        });
    }

    // -- guard and routes ---------------------------------------------------------------------

    private static Request request(String method, String path, String address, Map<String, String> headers, String body) {
        return new Request(method, path, Map.of(), headers, body, address);
    }

    private static void guard(Runner r) {
        r.test("guard: only IP addresses or localhost as Host (no DNS rebinding)", () -> {
            for (String host : List.of("127.0.0.1", "127.0.0.1:8080", "10.0.1.2:8080", "localhost", "LOCALHOST:8080",
                    "[::1]", "[::1]:8080")) {
                RequestGuard.checkSource(request("GET", "/api/stats", "127.0.0.1", Map.of("Host", host), ""));
            }
            for (String host : List.of("evil.example", "evil.example:8080", "127.0.0.1.nip.io", "localhost.evil.example",
                    "127.0.0.1:80x", "[::1", "")) {
                expectStatus(403, () -> RequestGuard.checkSource(
                        request("GET", "/api/stats", "127.0.0.1", Map.of("Host", host), "")));
            }
        });
        r.test("guard: requests made by web pages are refused; the board and curl pass", () -> {
            expectStatus(403, () -> RequestGuard.checkSource(request("POST", "/api/event", "127.0.0.1",
                    Map.of("Host", "127.0.0.1:8080", "Origin", "https://evil.example"), "{\"id\":1}")));
            expectStatus(403, () -> RequestGuard.checkSource(request("POST", "/api/event", "127.0.0.1",
                    Map.of("Host", "127.0.0.1:8080", "Origin", "null"), "{\"id\":1}")));
            expectStatus(403, () -> RequestGuard.checkSource(request("GET", "/api/macros", "127.0.0.1",
                    Map.of("Host", "127.0.0.1:8080", "Sec-Fetch-Site", "cross-site"), "")));
            expectStatus(403, () -> RequestGuard.checkSource(request("GET", "/api/macros", "127.0.0.1",
                    Map.of("Host", "127.0.0.1:8080", "Sec-Fetch-Site", "same-site"), "")));
            RequestGuard.checkSource(request("GET", "/api/stats", "127.0.0.1",
                    Map.of("Host", "127.0.0.1:8080", "Origin", "http://127.0.0.1:8080", "Sec-Fetch-Site", "same-origin"), ""));
            RequestGuard.checkSource(request("GET", "/api/stats", "127.0.0.1",
                    Map.of("Host", "127.0.0.1:8080", "Sec-Fetch-Site", "none"), ""));
            RequestGuard.checkSource(request("POST", "/api/deck", "10.0.1.50", Map.of("Host", "10.0.1.2:8080"), "{}"));
        });
        r.test("guard: a body must be declared as JSON (a text/plain body needs no preflight)", () -> {
            expectStatus(415, () -> RequestGuard.checkBody(request("POST", "/api/macros", "127.0.0.1",
                    Map.of("Content-Type", "text/plain"), "{\"id\":1}")));
            expectStatus(415, () -> RequestGuard.checkBody(request("POST", "/api/macros", "127.0.0.1", Map.of(), "{\"id\":1}")));
            RequestGuard.checkBody(request("POST", "/api/macros", "127.0.0.1",
                    Map.of("Content-Type", "application/json; charset=utf-8"), "{\"id\":1}"));
            RequestGuard.checkBody(request("POST", "/api/wifi/token", "127.0.0.1", Map.of(), ""));
        });
    }

    private static void routes(Runner r) {
        r.test("routes: macro definitions are host-only, events and the sanitized list are peer routes (needs sqlite-jdbc)", () -> {
            requireSqlite();
            Path dir = Files.createTempDirectory("cd-mx-routes");
            Services services = new Services(new ConfigService(dir.resolve("control-deck.db")), "", dir.resolve("var"));
            Router router = new Router(services);
            Map<String, String> json = Map.of("Content-Type", "application/json", "Host", "127.0.0.1:8080");
            String body = Json.encode(MessageHandler.map("id", 1, "label", "TRUE", "action", "command",
                    "value", List.of("true")));

            equal(403, router.handle(request("POST", "/api/macros", "10.0.0.5", json, body)).status(), "LAN cannot define");
            Response fromPage = router.handle(request("POST", "/api/macros", "127.0.0.1",
                    Map.of("Host", "127.0.0.1:8080", "Content-Type", "text/plain", "Origin", "https://evil.example"), body));
            equal(403, fromPage.status(), "a web page cannot define");
            equal(415, router.handle(request("POST", "/api/macros", "127.0.0.1",
                    Map.of("Host", "127.0.0.1:8080", "Content-Type", "text/plain"), body)).status(), "text/plain refused");
            equal(0, services.macros().list(false).get("count"), "nothing was created by the refused requests");

            equal(200, router.handle(request("POST", "/api/macros", "127.0.0.1", json, body)).status(), "host defines");
            equal(409, router.handle(request("POST", "/api/macros", "127.0.0.1", json, body)).status(), "duplicate id");
            equal(400, router.handle(request("POST", "/api/macros", "127.0.0.1", json,
                    "{\"id\":2,\"label\":\"X\",\"action\":\"shell\"}")).status(), "bad action");
            equal(404, router.handle(request("PUT", "/api/macro/5", "127.0.0.1", json, "{\"label\":\"Y\"}")).status(),
                    "unknown macro");

            services.config().setWifiSettings(Map.of("enabled", true, "bind", "0.0.0.0"));
            String token = services.config().rotateWifiToken();
            Map<String, String> peer = Map.of("Authorization", "Bearer " + token, "Host", "10.0.1.2:8080",
                    "Content-Type", "application/json");
            String other = "{\"id\":2,\"label\":\"LAN\",\"action\":\"command\",\"value\":[\"true\"]}";
            equal(403, router.handle(request("POST", "/api/macros", "10.0.0.5", peer, other)).status(),
                    "a valid token still cannot define macros");
            equal(403, router.handle(request("PUT", "/api/macro/1", "10.0.0.5", peer, "{\"label\":\"Z\"}")).status(),
                    "nor change them");
            equal(403, router.handle(request("DELETE", "/api/macro/1", "10.0.0.5", peer, "")).status(), "nor delete them");
            equal(403, router.handle(request("POST", "/api/macros/reorder", "10.0.0.5", peer, "[1]")).status(),
                    "nor reorder them");
            equal(403, router.handle(request("GET", "/api/macro/1", "10.0.0.5", peer, "")).status(),
                    "nor read the full definition");
            Response list = router.handle(request("GET", "/api/macros", "10.0.0.5", peer, ""));
            equal(200, list.status(), "peer reads the list");
            check(!Json.encode(list.body()).contains("\"value\""), "sanitized for the LAN: " + Json.encode(list.body()));

            Response event = router.handle(request("POST", "/api/event", "10.0.0.5", peer, "{\"id\":1}"));
            equal(200, event.status(), "peer runs macro 1");
            equal(true, ((Map<?, ?>) event.body()).get("action_executed"), "it ran");
            equal(404, router.handle(request("POST", "/api/event", "10.0.0.5", peer, "{\"id\":3}")).status(), "not configured");
            equal(400, router.handle(request("POST", "/api/event", "10.0.0.5", peer, "{\"id\":\"x\"}")).status(), "bad id");
            Response batch = router.handle(request("POST", "/api/event/batch", "10.0.0.5", peer, "[1, {\"id\":1}]"));
            equal(2, ((Map<?, ?>) batch.body()).get("processed"), "batch ran twice");
            equal(200, router.handle(request("DELETE", "/api/macro/1", "127.0.0.1", json, "")).status(), "host deletes");
            services.close();
        });
    }

    // -- helpers ----------------------------------------------------------------------------------

    private static void expect(Class<? extends RuntimeException> type, Runnable body) {
        try {
            body.run();
        } catch (RuntimeException error) {
            if (type.isInstance(error)) {
                return;
            }
            throw new AssertionError("expected " + type.getSimpleName() + " but got " + error);
        }
        throw new AssertionError("expected " + type.getSimpleName());
    }

    private static void expectStatus(int status, Runnable body) {
        try {
            body.run();
        } catch (HttpError error) {
            equal(status, error.status(), "status");
            return;
        }
        throw new AssertionError("expected HTTP " + status);
    }
}
