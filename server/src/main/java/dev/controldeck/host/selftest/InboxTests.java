package dev.controldeck.host.selftest;

import static dev.controldeck.host.selftest.Runner.check;
import static dev.controldeck.host.selftest.Runner.equal;

import dev.controldeck.host.Services;
import dev.controldeck.host.config.ConfigService;
import dev.controldeck.host.http.Request;
import dev.controldeck.host.http.Response;
import dev.controldeck.host.http.Router;
import dev.controldeck.host.inbox.MessageStore;
import dev.controldeck.host.protocol.Codec4;
import dev.controldeck.host.protocol.Frame4;
import dev.controldeck.host.protocol.MessageHandler;
import dev.controldeck.host.protocol.Wire;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
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
        r.test("messages: 0x10 lists a Deck's messages, 0x11 marks them read, the other Deck and the PC see only theirs", () -> {
            Services s = services(Files.createTempDirectory("cd-msg-round"));
            Router router = new Router(s, false);
            MessageStore store = s.messages().inbox();
            long all = store.add("all", "Alex", "text", "Dinner is ready").id();
            long toA = store.add(DECK_A, "Alex", "text", "Only for A").id();
            long toB = store.add(DECK_B, "Alex", "text", "Only for B").id();
            store.add("pc", "Deck A", "text", "For the people at the PC");

            byte[] request = Frame4.build(Codec4.MESSAGES_REQUEST, 0, 0, new Wire.Writer().str(DECK_A).u32(0).toBytes());
            equal(DECK_A, Codec4.decode(Frame4.parse(request)).get("deck"), "0x10 layout: deck id first");
            byte[] read = Frame4.build(Codec4.MESSAGE_READ, 0, 0, new Wire.Writer().str(DECK_A).u32(all).toBytes());
            equal(all, Codec4.decode(Frame4.parse(read)).get("id"), "0x11 layout: message id last");

            equal(List.of(all, toA), ids(messages(ask(router, DECK_A, 0))), "A: broadcast and its own, oldest first");
            equal(List.of(false, false), flags(messages(ask(router, DECK_A, 0))), "A: unread at first");
            equal(List.of(all, toB), ids(messages(ask(router, DECK_B, 0))), "B: broadcast and its own");
            equal(0, frames(ask(router, DECK_A, Codec4.MESSAGE_READ, all)).size(), "0x11 gets no answer frame");
            equal(List.of(true, false), flags(messages(ask(router, DECK_A, 0))), "A: the read one is flagged read");
            equal(List.of(), ids(messages(ask(router, DECK_A, toA))), "an empty reply is valid");
            equal(List.of(false, false), flags(messages(ask(router, DECK_B, 0))), "B: still unread, not affected by A");

            ask(router, DECK_A, Codec4.MESSAGE_READ, 999_999L);
            ask(router, DECK_A, Codec4.MESSAGE_READ, toB);
            equal(List.of(false, false), flags(messages(ask(router, DECK_B, 0))), "an unknown id, or another Deck's, changes nothing");
            ask(router, "all", Codec4.MESSAGE_READ, all);
            ask(router, "pc", Codec4.MESSAGE_READ, all);
            equal(List.of(false, false), flags(messages(ask(router, DECK_B, 0))), "a name that is no Deck (all, pc) reads nothing");
            equal(List.of(), ids(messages(ask(router, "pc", 0))), "the PC's messages are not a Deck's");
            equal(List.of(), ids(messages(ask(router, "all", 0))), "\"all\" is not a Deck id");
            equal(List.of(), ids(messages(ask(router, "bad id", 0))), "a malformed id: empty reply, no error");
        });
        r.test("messages: POST /api/messages reaches only its Deck; a Deck's own token speaks only for it", () -> {
            Services s = services(Files.createTempDirectory("cd-msg-web"));
            s.config().setWifiSettings(Map.of("enabled", true, "bind", "0.0.0.0"));
            String tokenA = s.config().issueDeckToken(DECK_A);
            Router router = new Router(s, false);
            Response posted = router.handle(new Request("POST", "/api/messages", Map.of(),
                    Map.of("Content-Type", "application/json"),
                    "{\"text\":\"Hi A\",\"to\":\"" + DECK_A + "\",\"from\":\"Web\"}", "127.0.0.1"));
            equal(200, posted.status(), "POST /api/messages answers 200");
            equal(List.of("Hi A"), texts(messages(ask(router, DECK_A, 0))), "A gets the web message");
            equal(List.of(), texts(messages(ask(router, DECK_B, 0))), "B does not");

            Map<String, String> asA = Map.of("Authorization", "Bearer " + tokenA);
            equal(403, ask(router, "10.0.0.5", asA, Codec4.MESSAGES_REQUEST, new Wire.Writer().str(DECK_B).u32(0)).status(),
                    "A's token cannot ask for B's messages");
            equal(List.of("Hi A"), texts(messages(ask(router, "10.0.0.5", asA, Codec4.MESSAGES_REQUEST,
                    new Wire.Writer().str(DECK_A).u32(0)))), "A's token gets A's messages");
        });
        r.test("messages: text at most 500 characters and 1500 bytes, from 24, a reply holds only what fits one frame", () -> {
            Services s = services(Files.createTempDirectory("cd-msg-size"));
            MessageStore store = s.messages().inbox();
            String emoji = "😀";  // 4 bytes of UTF-8 each
            MessageStore.Message first = store.add("all", emoji.repeat(30), "text", emoji.repeat(600));
            equal(375, first.text().codePointCount(0, first.text().length()), "text cut to 1500 bytes (375 emoji)");
            equal(24, first.from().codePointCount(0, first.from().length()), "from cut to 24 characters");
            check(first.text().getBytes(StandardCharsets.UTF_8).length <= MessageStore.MAX_TEXT_BYTES, "text within 1500 bytes");
            for (int i = 0; i < 9; i++) {
                store.add("all", "x", "text", emoji.repeat(500));
            }
            Map<String, Object> reply = s.messages().handle(MessageHandler.map("type", "messages_request",
                    "deck", DECK_A, "after", 0L), Frame4.MAX_PAYLOAD).get(0);
            equal(1, ((List<?>) reply.get("messages")).size(), "the worst message (24 emoji from, 1500-byte text) fits one frame");
            check(Codec4.encode(reply).payload().length <= Frame4.MAX_PAYLOAD, "the reply fits one frame");
            Map<String, Object> next = s.messages().handle(MessageHandler.map("type", "messages_request",
                    "deck", DECK_A, "after", first.id()), Frame4.MAX_PAYLOAD).get(0);
            equal(first.id() + 1, ((List<?>) next.get("messages")).get(0) instanceof Map<?, ?> m ? m.get("id") : null,
                    "the next request brings the next one");

            Services small = services(Files.createTempDirectory("cd-msg-count"));
            for (int i = 0; i < 12; i++) {
                small.messages().inbox().add(DECK_A, "x", "text", "hi " + i);
            }
            Map<String, Object> ten = small.messages().handle(MessageHandler.map("type", "messages_request",
                    "deck", DECK_A, "after", 0L), Frame4.MAX_PAYLOAD).get(0);
            equal(10, ((List<?>) ten.get("messages")).size(), "at most ten per reply");
        });
    }

    private static final String DECK_A = "84C7BB78E0C0";
    private static final String DECK_B = "020000000001";

    private static Services services(Path dir) throws Exception {
        return new Services(new ConfigService(dir.resolve("control-deck.db")), "", dir.resolve("var"));
    }

    /** One request from the loopback (the shared trust) carrying one frame: its answer. */
    private static Response ask(Router router, String deck, long after) {
        return ask(router, "127.0.0.1", Map.of(), Codec4.MESSAGES_REQUEST, new Wire.Writer().str(deck).u32(after));
    }

    /** A message_read (0x11) for a Deck from the loopback. */
    private static Response ask(Router router, String deck, int type, long id) {
        return ask(router, "127.0.0.1", Map.of(), type, new Wire.Writer().str(deck).u32(id));
    }

    private static Response ask(Router router, String address, Map<String, String> headers, int type, Wire.Writer payload) {
        byte[] frame = Frame4.encodeForLine(type, 0, 0, payload.toBytes());
        return router.handle(new Request("POST", "/api/deck", Map.of(), headers, "", address, frame));
    }

    /** The frames in an answer body: COBS lines, each ended by 0x00. */
    private static List<Frame4.Frame> frames(Response response) {
        byte[] body = (byte[]) response.body();
        List<Frame4.Frame> out = new ArrayList<>();
        int start = 0;
        for (int i = 0; i <= body.length; i++) {
            if (i == body.length || body[i] == 0) {
                if (i > start) {
                    byte[] line = Arrays.copyOfRange(body, start, i);
                    Frame4.Frame frame = Frame4.parse(Frame4.cobsDecode(line, line.length));
                    check(frame != null, "a valid frame in the answer");
                    out.add(frame);
                }
                start = i + 1;
            }
        }
        return out;
    }

    /** The messages of a 0x4E answer (one frame): id, kind, read, from, text. */
    private static List<Map<String, Object>> messages(Response response) {
        List<Frame4.Frame> answer = frames(response);
        equal(1, answer.size(), "one answer frame");
        equal(Codec4.MESSAGES, answer.get(0).type(), "the messages answer (0x4E)");
        Wire.Reader in = new Wire.Reader(answer.get(0).payload());
        List<Map<String, Object>> out = new ArrayList<>();
        int count = in.u8();
        for (int i = 0; i < count; i++) {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("id", in.u32());
            in.u32();  // time
            m.put("kind", in.u8() == 1 ? "voice" : "text");
            m.put("read", (in.u8() & 1) == 1);
            m.put("from", in.str());
            m.put("text", new String(in.bytes(in.u16()), StandardCharsets.UTF_8));
            out.add(m);
        }
        equal(0, in.left(), "nothing after the last message");
        return out;
    }

    private static List<Object> ids(List<Map<String, Object>> list) {
        return list.stream().map(m -> m.get("id")).toList();
    }

    private static List<Object> flags(List<Map<String, Object>> list) {
        return list.stream().map(m -> m.get("read")).toList();
    }

    private static List<Object> texts(List<Map<String, Object>> list) {
        return list.stream().map(m -> m.get("text")).toList();
    }
}
