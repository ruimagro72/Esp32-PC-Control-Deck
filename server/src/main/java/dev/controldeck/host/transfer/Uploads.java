package dev.controldeck.host.transfer;

import dev.controldeck.host.Defaults;

import java.io.ByteArrayOutputStream;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Objects a Deck sends to the server (docs/04-PROTOCOL.md, "Transfers"): upload_begin, then
 * upload_data blocks, each answered with upload_ack and the next offset wanted. A block at
 * another offset is not kept: the ack names the offset again and the Deck sends from there.
 */
public final class Uploads {

    public static final int KIND_VOICE = 2;
    private static final long FORGET_AFTER_MS = 2 * 60_000L;

    /** What a finished upload carries. */
    public record Done(String deck, int kind, String to, byte[] bytes) {
    }

    private static final class Upload {
        final String deck;
        final int kind;
        final String to;
        final long size;
        final ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        long touchedMs = System.currentTimeMillis();

        Upload(String deck, int kind, String to, long size) {
            this.deck = deck;
            this.kind = kind;
            this.to = to;
            this.size = size;
        }
    }

    private final Map<String, Upload> open = new LinkedHashMap<>();
    private final long maxBytes;

    public Uploads(long maxBytes) {
        this.maxBytes = maxBytes;
    }

    /** A Deck starts one; the answer is its first ack (offset 0). */
    public synchronized Map<String, Object> begin(String deck, int id, int kind, long size, String to) {
        if (kind != KIND_VOICE) {
            throw new IllegalArgumentException("unknown upload kind " + kind);
        }
        if (size <= 0 || size > maxBytes) {
            throw new IllegalArgumentException("upload size " + size + " is out of range (1.." + maxBytes + ")");
        }
        forgetOld();
        open.put(key(deck, id), new Upload(deck, kind, to, size));
        return ack(id, 0);
    }

    /**
     * One block. The ack says what comes next; when the last byte is in, {@code done} gets
     * the object (and the upload is forgotten).
     */
    public synchronized Map<String, Object> data(String deck, int id, long offset, byte[] bytes, Done[] done) {
        Upload u = open.get(key(deck, id));
        if (u == null) {
            throw new IllegalArgumentException("no upload " + id + " open for this Deck");
        }
        u.touchedMs = System.currentTimeMillis();
        long have = u.bytes.size();
        if (offset == have && bytes.length > 0 && have + bytes.length <= u.size) {
            u.bytes.writeBytes(bytes);
            have += bytes.length;
        }
        if (have == u.size) {
            open.remove(key(deck, id));
            done[0] = new Done(u.deck, u.kind, u.to, u.bytes.toByteArray());
        }
        return ack(id, have);
    }

    public synchronized void cancel(String deck, int id) {
        open.remove(key(deck, id));
    }

    public synchronized int openCount() {
        return open.size();
    }

    private static Map<String, Object> ack(int id, long next) {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("type", "upload_ack");
        m.put("id", id);
        m.put("next", next);
        return m;
    }

    private static String key(String deck, int id) {
        return deck.toUpperCase() + "/" + id;
    }

    private void forgetOld() {
        long now = System.currentTimeMillis();
        open.values().removeIf(u -> now - u.touchedMs > FORGET_AFTER_MS);
        while (open.size() >= Defaults.MAX_OPEN_TRANSFERS) {
            open.remove(open.keySet().iterator().next());
        }
    }
}
