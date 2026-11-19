package dev.controldeck.host.http;

import dev.controldeck.host.Services;
import dev.controldeck.host.firmware.FirmwareStore;
import dev.controldeck.host.protocol.Codec4;
import dev.controldeck.host.protocol.Frame4;
import dev.controldeck.host.protocol.MessageHandler;
import dev.controldeck.host.protocol.Protocol;
import dev.controldeck.host.transport.serial.LineSplitter;

import java.util.List;
import java.util.Map;
import java.util.regex.Pattern;

/** The Deck's own endpoints: its binary frames (POST /api/deck) and firmware over Wi-Fi. */
final class DeckRoutes {

    /** The most frames a Deck sends in one request (its stats_request, hello, a key press...). */
    private static final int DECK_FRAMES_PER_REQUEST = 8;

    private static final Pattern DECK_ID = Pattern.compile("^[0-9A-Za-z:_-]{1,32}$");

    private final Services services;

    DeckRoutes(Services services) {
        this.services = services;
    }

    /** A board's firmware image, for a Deck that updates itself over Wi-Fi (firmware_offer). */
    Response firmware(String board) {
        FirmwareStore store = services.messages().firmwareStore();
        FirmwareStore.Image image = store == null ? null : store.find(board).orElse(null);
        if (image == null) {
            throw HttpError.notFound("No firmware for '" + board + "'");
        }
        return Response.bytes(image.bytes());
    }

    /**
     * POST /api/deck: a Deck over Wi-Fi. The body is its protocol 4 frames, COBS-encoded and
     * each ended by 0x00 (docs/04-PROTOCOL.md); the answer is the server's frames, the same way.
     */
    Response deck(Request request) {
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

    /** The Deck id a message carries (hello, board: "id"; stats_request, macro...: "deck"), or null. */
    static String deckOf(Map<String, Object> message) {
        if (message == null) {
            return null;
        }
        Object type = message.get("type");
        Object id = "hello".equals(type) || "board".equals(type) || "wifi_token_request".equals(type)
                ? message.get("id") : message.get("deck");
        return id instanceof String text && DECK_ID.matcher(text).matches() ? text : null;
    }
}
