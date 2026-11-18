package dev.controldeck.host.selftest;

import static dev.controldeck.host.selftest.Runner.check;
import static dev.controldeck.host.selftest.Runner.equal;

import dev.controldeck.host.config.ConfigService;
import dev.controldeck.host.protocol.MessageHandler;
import dev.controldeck.host.protocol.Protocol;
import dev.controldeck.host.texts.TextTables;
import dev.controldeck.host.util.Json;
import java.nio.file.Files;
import java.nio.file.Path;
import java.sql.DriverManager;
import java.util.List;
import java.util.Map;

/** The texts the Decks get from the server (docs/archive/design-notes/DESIGN.md). */
final class TextTests {

    private TextTests() {
    }

    static void run(Runner r) {
        r.test("texts: the English table loads, pages fit the frame and cover it once", () -> {
            TextTables tables = new TextTables();
            TextTables.Table en = tables.table("en");
            check(en.texts().size() > 50, "the table has the server's texts: " + en.texts().size());
            equal(8, en.ids().length(), "the ids' check");
            int from = 0;
            int pages = 0;
            while (from < en.texts().size()) {
                Map<String, Object> frame = tables.frame("en", from, Protocol.SERIAL_MAX_FRAME_BYTES);
                check(dev.controldeck.host.protocol.Codec4.encode(frame).payload().length <= Protocol.SERIAL_MAX_FRAME_BYTES,
                        "page fits a frame");
                List<?> page = (List<?>) frame.get("t");
                check(!page.isEmpty(), "progress");
                from += page.size();
                pages++;
            }
            check(pages > 1, "the help needs more than one page: " + pages);
            equal("en", tables.table("xx").language(), "a language without a table gets English");
        });
        r.test("texts: a Deck gets its own language, the others the one for all (needs sqlite-jdbc)", () -> {
            if (DriverManager.drivers().noneMatch(d -> d.getClass().getName().contains("sqlite"))) {
                throw new Runner.Skip("sqlite-jdbc not on the class path; run with the shaded jar");
            }
            Path dir = Files.createTempDirectory("cd-texts");
            ConfigService config = new ConfigService(dir.resolve("control-deck.db"));
            equal("en", config.getDeckLanguage("AA"), "English by default");
            config.setDeckLanguage("AA", "pt");
            equal("pt", config.getDeckLanguage("AA"), "its own");
            equal("en", config.getDeckLanguage("BB"), "the others unchanged");
            MessageHandler messages = new MessageHandler(config, null, null);
            Map<String, Object> frame = messages.handle(MessageHandler.map("type", "texts_request", "deck", "BB"), 1800).get(0);
            equal("texts", frame.get("type"), "answered");
            check(Protocol.REQUEST_TYPES.contains("texts_request"), "the serial line passes the request on");
            config.close();
        });
    }
}
