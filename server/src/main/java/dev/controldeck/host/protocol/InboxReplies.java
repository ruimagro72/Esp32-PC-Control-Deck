package dev.controldeck.host.protocol;

import dev.controldeck.host.inbox.MessageStore;
import dev.controldeck.host.transfer.Uploads;
import dev.controldeck.host.util.Log;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import static dev.controldeck.host.protocol.MessageHandler.binarySize;
import static dev.controldeck.host.protocol.MessageHandler.map;

/** The Decks' inbox replies: the messages page and the voice messages that arrive. */
final class InboxReplies {

    /** The most messages in one answer (the Deck keeps the newest few). */
    private static final int MESSAGES_PER_ANSWER = 10;

    private final BoardRegistry boards;

    InboxReplies(BoardRegistry boards) {
        this.boards = boards;
    }

    /** A Deck's messages newer than the newest it has, as many as fit one frame. */
    Map<String, Object> messagesFrame(MessageStore inbox, Map<String, Object> message, int maxFrameBytes) {
        String deck = message.get("deck") instanceof String id ? id : "";
        long after = message.get("after") instanceof Number n ? n.longValue() : 0;
        List<Map<String, Object>> items = inbox == null ? new ArrayList<>()
                : new ArrayList<>(inbox.forDeck(deck, after, MESSAGES_PER_ANSWER));
        Map<String, Object> frame = map("type", "messages", "messages", items);
        while (!items.isEmpty() && binarySize(frame) > maxFrameBytes) {
            items.remove(items.size() - 1);  // the rest comes with the next request
        }
        return frame;
    }

    /** A Deck's recording came whole: a voice message for whom it named ("" = the PC). */
    void voiceArrived(MessageStore store, Uploads.Done done) {
        if (store == null) {
            return;
        }
        String to = done.to().isBlank() ? MessageStore.PC : done.to();
        Map<String, Object> board = boards.get(done.deck());
        if (board == null) {
            board = boards.get(done.deck().toUpperCase());
        }
        Object name = board == null ? null : board.getOrDefault("name", board.get("board"));
        String from = name instanceof String n && !n.isBlank() ? n : done.deck();
        MessageStore.Message m = store.addVoice(to, from, done.bytes());
        Log.info("[voice] message " + m.id() + " from " + from + " to " + to + ", " + m.text()
                + " (GET /api/messages/" + m.id() + "/audio)");
    }
}
