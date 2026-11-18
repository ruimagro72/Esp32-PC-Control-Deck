package dev.controldeck.host.transfer;

import dev.controldeck.host.firmware.FirmwareStore;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Objects larger than a frame sent to a Deck (docs/04-PROTOCOL.md, "Transfers"): the firmware
 * image over USB or BLE now; voice messages and images later. Each answer carries the blocks
 * after the offset the Deck asked for, up to a window, so the Deck's acks pace the server on
 * any link. A repeated ack (a block lost or damaged) sends again from that offset.
 */
public final class Transfers {

    public static final int KIND_FIRMWARE = 1;
    public static final int KIND_VOICE = 2;
    public static final int KIND_PACK = 4;
    public static final int BLOCK = 1024;
    public static final int WINDOW = 4;
    private static final int MAX_OPEN = 8;              // one per Deck in practice
    private static final long FORGET_AFTER_MS = 10 * 60_000L;

    private static final class Transfer {
        final int id;
        final byte[] bytes;
        long sentUpTo;
        long lastAck = -1;
        long touchedMs;

        Transfer(int id, byte[] bytes) {
            this.id = id;
            this.bytes = bytes;
            this.touchedMs = System.currentTimeMillis();
        }
    }

    private final Map<Integer, Transfer> open = new LinkedHashMap<>();
    private int nextId = 1;

    /** The firmware image for a board (kind 1): its begin frame and the first blocks, or null. */
    public List<Map<String, Object>> start(int kind, String what, FirmwareStore firmware) {
        if (kind != KIND_FIRMWARE || firmware == null) {
            return null;
        }
        FirmwareStore.Image image = firmware.find(what).orElse(null);
        return image == null ? null : start(kind, image.bytes());
    }

    /** Any object: its begin frame and the first blocks. */
    public synchronized List<Map<String, Object>> start(int kind, byte[] bytes) {
        forgetOld();
        Transfer t = new Transfer(nextId, bytes);
        nextId = nextId >= 0xFFFF ? 1 : nextId + 1;
        open.put(t.id, t);
        List<Map<String, Object>> frames = new ArrayList<>();
        frames.add(frame("type", "transfer_begin", "id", t.id, "kind", kind, "size", (long) t.bytes.length, "md5", md5(t.bytes)));
        frames.addAll(blocks(t, 0));
        return frames;
    }

    /** The Deck has everything before `next`: the blocks after it, up to the window. */
    public synchronized List<Map<String, Object>> ack(int id, long next) {
        Transfer t = open.get(id);
        if (t == null || next < 0 || next > t.bytes.length) {
            return List.of();
        }
        if (next == t.lastAck || next > t.sentUpTo) {
            t.sentUpTo = next;  // asked again: what followed was lost
        }
        t.lastAck = next;
        return blocks(t, next);
    }

    /** The Deck kept it (or gave up): the transfer is forgotten. */
    public synchronized void end(int id) {
        open.remove(id);
    }

    public synchronized int openCount() {
        return open.size();
    }

    private List<Map<String, Object>> blocks(Transfer t, long acked) {
        t.touchedMs = System.currentTimeMillis();
        List<Map<String, Object>> frames = new ArrayList<>();
        long limit = Math.min(t.bytes.length, acked + (long) WINDOW * BLOCK);
        if (t.sentUpTo < acked) {
            t.sentUpTo = acked;
        }
        while (t.sentUpTo < limit) {
            int from = (int) t.sentUpTo;
            int length = (int) Math.min(BLOCK, t.bytes.length - t.sentUpTo);
            byte[] piece = new byte[length];
            System.arraycopy(t.bytes, from, piece, 0, length);
            frames.add(frame("type", "transfer_data", "id", t.id, "offset", (long) from, "bytes", piece));
            t.sentUpTo += length;
        }
        return frames;
    }

    private void forgetOld() {
        long now = System.currentTimeMillis();
        open.values().removeIf(t -> now - t.touchedMs > FORGET_AFTER_MS);
        while (open.size() >= MAX_OPEN) {
            open.remove(open.keySet().iterator().next());
        }
    }

    private static Map<String, Object> frame(Object... pairs) {
        Map<String, Object> map = new LinkedHashMap<>();
        for (int i = 0; i < pairs.length; i += 2) {
            map.put((String) pairs[i], pairs[i + 1]);
        }
        return map;
    }

    private static String md5(byte[] bytes) {
        try {
            return HexFormat.of().formatHex(MessageDigest.getInstance("MD5").digest(bytes));
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);
        }
    }
}
