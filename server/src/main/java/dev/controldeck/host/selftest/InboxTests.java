package dev.controldeck.host.selftest;

import static dev.controldeck.host.selftest.Runner.check;
import static dev.controldeck.host.selftest.Runner.equal;

import dev.controldeck.host.inbox.MessageStore;
import dev.controldeck.host.protocol.Codec4;
import dev.controldeck.host.protocol.Wire;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

/** The Decks' inbox: messages left on the server, read per Deck, sent in binary. */
final class InboxTests {

    private InboxTests() {
    }

    static void run(Runner r) {
        r.test("inbox: a message for all or one Deck, read per Deck, kept across a restart", () -> {
            Path dir = Files.createTempDirectory("cd-inbox");
            MessageStore store = new MessageStore(dir.resolve("messages.json"));
            long all = store.add("all", "Alex", "text", "Dinner is ready").id();
            long one = store.add("84C7BB78E0C0", "Alex", "text", "Only for the ES3C28P").id();
            store.addVoice("all", "server", new byte[12 * 32000]);
            equal(3, store.forDeck("84C7BB78E0C0", 0, 10).size(), "the first Deck: all three");
            equal(2, store.forDeck("020000000001", 0, 10).size(), "another Deck: the two for all");
            equal(1, store.forDeck("84C7BB78E0C0", one, 10).size(), "only those after the newest it has");
            check(store.markRead("84C7BB78E0C0", all), "read");
            equal(true, store.forDeck("84C7BB78E0C0", 0, 10).get(0).get("read"), "read for that Deck");
            equal(false, store.forDeck("020000000001", 0, 10).get(0).get("read"), "still unread for the other");
            check(!store.markRead("020000000001", one), "a message for another Deck is not its to read");

            MessageStore again = new MessageStore(dir.resolve("messages.json"));
            equal(3, again.all().size(), "kept across a restart");
            long next = again.add("all", "x", "text", "after the restart").id();
            check(next > one, "ids keep growing");
            try {
                again.add("not-a-deck", "x", "text", "y");
                check(false, "a bad target is refused");
            } catch (IllegalArgumentException expected) {
                check(true, "a bad target is refused");
            }
        });
        r.test("inbox: the messages frame in binary", () -> {
            Map<String, Object> frame = Map.of("type", "messages", "messages", List.of(
                    Map.of("id", 7L, "time", 1000L, "kind", "voice", "read", true, "from", "Alex", "text", "12 s")));
            Codec4.Out out = Codec4.encode(frame);
            equal(Codec4.MESSAGES, out.type(), "type");
            Wire.Reader in = new Wire.Reader(out.payload());
            equal(1, in.u8(), "one message");
            equal(7L, in.u32(), "id");
            equal(1000L, in.u32(), "time");
            equal(1, in.u8(), "voice");
            equal(1, in.u8(), "read");
            equal("Alex", in.str(), "from");
            equal(4, in.u16(), "text length (str16)");
        });
    }
}
