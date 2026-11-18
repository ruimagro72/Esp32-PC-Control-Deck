package dev.controldeck.host.selftest;

import static dev.controldeck.host.selftest.Runner.check;
import static dev.controldeck.host.selftest.Runner.equal;

import dev.controldeck.host.Services;
import dev.controldeck.host.config.ConfigService;
import dev.controldeck.host.console.ConsoleHub;
import dev.controldeck.host.http.Request;
import dev.controldeck.host.http.Response;
import dev.controldeck.host.http.Router;
import dev.controldeck.host.protocol.Codec4;
import dev.controldeck.host.protocol.Frame4;
import dev.controldeck.host.protocol.MessageHandler;
import dev.controldeck.host.protocol.Wire;
import dev.controldeck.host.telemetry.Inventory;
import dev.controldeck.host.transport.serial.LineSplitter;
import dev.controldeck.host.transport.serial.SerialLog;
import dev.controldeck.host.transport.serial.SerialSession;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.sql.DriverManager;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicLong;

/** The Deck's console on the server (docs/CONSOLE.md): the hub, the frames, the links, the API. */
final class ConsoleTests {

    private static final String DECK = "84C7BB78E0C0";

    private ConsoleTests() {
    }

    static void run(Runner r) {
        r.test("console: a line waits for its Deck, and its answer comes back in parts", () -> {
            ConsoleHub hub = new ConsoleHub(false);
            int id = hub.submit(DECK, "  info ");
            check(id > 0, "an id that is not the log's 0");
            equal(List.of(), hub.take("OTHERDECK000"), "another Deck does not take it");
            List<Map<String, Object>> frames = hub.take(DECK);
            equal(1, frames.size(), "its Deck takes it");
            equal("info", frames.get(0).get("line"), "trimmed");
            equal(List.of(), hub.take(DECK), "only once");
            hub.output(DECK, id, 0, "board: ES3C28P\n");
            ConsoleHub.Answer partial = hub.await(id, 0);
            equal(false, partial.done(), "not done before the last part");
            hub.output(DECK, id, ConsoleHub.FLAG_LAST, "heap: 200 KB\n");
            ConsoleHub.Answer whole = hub.await(id, 1000);
            equal(true, whole.done(), "done");
            equal("board: ES3C28P\nheap: 200 KB\n", whole.text(), "both parts");
            equal(null, hub.await(id, 0), "forgotten once read whole");
            hub.output(DECK, 999, ConsoleHub.FLAG_LAST, "nobody asked");
            equal(null, hub.await(999, 0), "an answer to an id never given is dropped");
        });
        r.test("console: any Deck, bad lines refused, a gone Deck cannot fill the queue", () -> {
            ConsoleHub hub = new ConsoleHub(false);
            int id = hub.submit(ConsoleHub.ANY, "help");
            equal(id, hub.take(DECK).get(0).get("id"), "the first Deck that asks takes a line for any");
            for (String bad : new String[] {"", "   ", "a\u0001b", "x".repeat(ConsoleHub.MAX_LINE_BYTES + 1)}) {
                expectRefusal(() -> hub.submit(DECK, bad), "refused: " + bad.length() + " chars");
            }
            expectRefusal(() -> hub.submit("not a deck id!", "help"), "a deck id is checked");
            for (int i = 0; i < 8; i++) {
                hub.submit(DECK, "info");
            }
            expectRefusal(() -> hub.submit(DECK, "info"), "at most eight lines wait");
        });
        r.test("console: live log lines are numbered and read after a number", () -> {
            ConsoleHub hub = new ConsoleHub(false);
            hub.output(DECK, 0, 0, "touch: points=1\nble: PC connected\n");
            equal(2L, hub.lastLogSeq(), "two lines");
            equal(1, hub.logs(DECK, 1).size(), "after the first");
            equal("ble: PC connected", hub.logs(ConsoleHub.ANY, 1).get(0).text(), "of every Deck");
            equal(0, hub.logs("OTHERDECK000", 0).size(), "of one Deck");
        });
        r.test("console: console_input and console_output in binary", () -> {
            Codec4.Out input = Codec4.encode(Map.of("type", "console_input", "id", 7, "line", "net"));
            equal(Codec4.CONSOLE_INPUT, input.type(), "input type 0x52");
            equal(0x52, Codec4.CONSOLE_INPUT, "the number in docs/04-PROTOCOL.md");
            equal(2 + 1 + 3, input.payload().length, "u16 id, str line");
            byte[] text = "uptime 5 s\n".getBytes(StandardCharsets.UTF_8);
            byte[] payload = new Wire.Writer().str(DECK).u16(7).u8(1).u16(text.length).bytes(text).toBytes();
            Map<String, Object> m = Codec4.decode(new Frame4.Frame(Codec4.CONSOLE_OUTPUT, 0, 0, payload));
            equal(0x17, Codec4.CONSOLE_OUTPUT, "the number in docs/04-PROTOCOL.md");
            equal("console_output", m.get("type"), "decoded");
            equal(DECK, m.get("deck"), "deck");
            equal(7, m.get("id"), "id");
            equal(1, m.get("flags"), "flags");
            equal("uptime 5 s\n", m.get("text"), "text");
        });
        r.test("console: the cable carries the console even while the Deck is on Wi-Fi (needs sqlite-jdbc)", () -> {
            requireSqlite();
            Path dir = Files.createTempDirectory("cd-console-serial");
            MessageHandler messages = new MessageHandler(new ConfigService(dir.resolve("control-deck.db")), null,
                    new Inventory(dir.resolve("root").toString(), "/", null));
            List<Map<String, Object>> sent = Collections.synchronizedList(new ArrayList<>());
            AtomicLong clock = new AtomicLong(1_000_000_000_000L);
            SerialSession session = new SerialSession(messages, age -> MessageHandler.map("type", "stats"), 1.0,
                    sent::add, fields -> { }, quietLog(), clock::get);
            session.onMessage(MessageHandler.map("type", "hello", "firmware", "0.5.6-es3c28p", "protocol", 4, "id", DECK));
            session.onMessage(MessageHandler.map("type", "transport", "to", "wifi"));
            sent.clear();
            int id = messages.console().submit(DECK, "info");
            session.tick();
            equal(1, sent.size(), "only the console line goes on the quiet cable");
            equal("console_input", sent.get(0).get("type"), "console_input");
            session.onMessage(MessageHandler.map("type", "console_output", "deck", DECK, "id", id, "flags", 1,
                    "text", "board: ES3C28P\n"));
            equal("board: ES3C28P\n", messages.console().await(id, 0).text(), "the answer reached the hub");
        });
        r.test("console: over Wi-Fi the line travels in the answer, and the API is host only (needs sqlite-jdbc)", () -> {
            requireSqlite();
            Path dir = Files.createTempDirectory("cd-console-wifi");
            Services services = new Services(new ConfigService(dir.resolve("control-deck.db")), "", dir.resolve("var"));
            Router router = new Router(services);
            equal(403, router.handle(json("POST", "/api/console", "10.0.0.5", "{\"line\":\"info\"}")).status(),
                    "the LAN cannot type on a Deck's console");
            Response queued = router.handle(json("POST", "/api/console", "127.0.0.1",
                    "{\"deck\":\"" + DECK + "\",\"line\":\"info\",\"wait_ms\":0}"));
            equal(200, queued.status(), "host can");
            Map<?, ?> data = (Map<?, ?>) ((Map<?, ?>) queued.body()).get("data");
            equal(false, data.get("done"), "no answer yet");
            int id = ((Number) data.get("id")).intValue();

            // The Deck's next request: its stats_request names it; the answer carries the line.
            byte[] statsRequest = new Wire.Writer().str(DECK).toBytes();
            Response answer = router.handle(new Request("POST", "/api/deck", Map.of(), Map.of(), "", "127.0.0.1",
                    Frame4.encodeForLine(Codec4.STATS_REQUEST, 0, 0, statsRequest)));
            List<Frame4.Frame> frames = framesOf((byte[]) answer.body());
            Frame4.Frame input = frames.stream().filter(f -> f.type() == Codec4.CONSOLE_INPUT).findFirst()
                    .orElseThrow(() -> new AssertionError("no console_input in the answer"));
            Wire.Reader in = new Wire.Reader(input.payload());
            equal(id, in.u16(), "its id");
            equal("info", in.str(), "its line");

            // Its answer comes in the request after.
            byte[] text = "uptime 1 s\n".getBytes(StandardCharsets.UTF_8);
            byte[] output = new Wire.Writer().str(DECK).u16(id).u8(1).u16(text.length).bytes(text).toBytes();
            router.handle(new Request("POST", "/api/deck", Map.of(), Map.of(), "", "127.0.0.1",
                    Frame4.encodeForLine(Codec4.CONSOLE_OUTPUT, 0, 1, output)));
            Response done = router.handle(new Request("GET", "/api/console/" + id, Map.of("wait_ms", "100"), Map.of(),
                    "", "127.0.0.1"));
            Map<?, ?> result = (Map<?, ?>) ((Map<?, ?>) done.body()).get("data");
            equal(true, result.get("done"), "done");
            equal("uptime 1 s\n", result.get("text"), "the Deck's words");
            Response log = router.handle(new Request("GET", "/api/console/log", Map.of("after", "0"), Map.of(), "",
                    "127.0.0.1"));
            equal(200, log.status(), "the log can be read");
            equal("login ***", Router.withoutSecret("login s3cret"), "a password never reaches the server's log");
            equal("passwd ***", Router.withoutSecret("passwd new one"), "nor a new one");
            equal("info", Router.withoutSecret("info"), "other lines are logged as typed");
        });
    }

    private static Request json(String method, String path, String address, String body) {
        return new Request(method, path, Map.of(), Map.of("Content-Type", "application/json"), body, address);
    }

    private static List<Frame4.Frame> framesOf(byte[] body) {
        List<Frame4.Frame> frames = new ArrayList<>();
        for (LineSplitter.Item item : new LineSplitter(1 << 16).feed(body, body.length)) {
            if (item instanceof LineSplitter.Frame frame) {
                frames.add(frame.frame());
            }
        }
        return frames;
    }

    private static SerialLog quietLog() {
        return new SerialLog() {
            @Override
            public void info(String message) {
            }

            @Override
            public void warn(String message) {
            }

            @Override
            public void debug(String message) {
            }
        };
    }

    private static void expectRefusal(Runner.Body body, String what) {
        try {
            body.run();
        } catch (IllegalArgumentException expected) {
            return;
        } catch (Exception other) {
            throw new AssertionError(what + ": " + other);
        }
        throw new AssertionError(what + ": not refused");
    }

    private static void requireSqlite() {
        if (DriverManager.drivers().noneMatch(d -> d.getClass().getName().contains("sqlite"))) {
            throw new Runner.Skip("sqlite-jdbc not on the class path; run with the shaded jar");
        }
    }
}
