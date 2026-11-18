package dev.controldeck.host.transport.serial;

import dev.controldeck.host.protocol.Frame4;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * The serial line from the Deck: text lines (protocol 3, the debug build's "# ..." lines, the
 * Arduino core's own "[  1046][E][Wire.cpp...]" lines) and protocol 4 frames, told apart by
 * the first byte after the previous end. A frame's first byte is always 0x02 (COBS: its
 * second raw byte is 0), so 0x02 starts a COBS frame up to 0x00; 0x00, '\r' and '\n' are
 * skipped; anything else starts a text line up to '\n'. (Taking a log line for a frame would
 * also swallow the real frame after it, up to its 0x00.) A text line never holds 0x00: one
 * there was the tail of a broken frame, and is dropped.
 */
public final class LineSplitter {

    /** What the bytes completed: a text line, a checked frame, or something damaged. */
    public sealed interface Item permits Text, Frame, Damaged {
    }

    public record Text(String line) implements Item {
    }

    public record Frame(Frame4.Frame frame) implements Item {
    }

    public record Damaged() implements Item {
    }

    private static final byte FRAME_START = 0x02;  // COBS code of a frame's first two raw bytes (version, 0)

    private enum Mode { BETWEEN, TEXT, FRAME }

    private final int maxBytes;
    private final ByteArrayOutputStream current = new ByteArrayOutputStream();
    private Mode mode = Mode.BETWEEN;
    private boolean overflow;

    public LineSplitter(int maxBytes) {
        this.maxBytes = maxBytes;
    }

    public List<Item> feed(byte[] data, int length) {
        List<Item> items = new ArrayList<>();
        for (int i = 0; i < length; i++) {
            Item item = feed(data[i]);
            if (item != null) {
                items.add(item);
            }
        }
        return items;
    }

    private Item feed(byte b) {
        switch (mode) {
            case BETWEEN -> {
                if (b == 0 || b == '\r' || b == '\n') {
                    return null;
                }
                current.reset();
                overflow = false;
                mode = b == FRAME_START ? Mode.FRAME : Mode.TEXT;
                add(b);
                return null;
            }
            case TEXT -> {
                if (b == 0) {
                    mode = Mode.BETWEEN;
                    return new Damaged();
                }
                if (b == '\n') {
                    mode = Mode.BETWEEN;
                    if (overflow) {
                        return new Damaged();
                    }
                    String line = current.toString(StandardCharsets.UTF_8);
                    return new Text(line.endsWith("\r") ? line.substring(0, line.length() - 1) : line);
                }
                add(b);
                return null;
            }
            default -> {
                if (b != 0) {
                    add(b);
                    return null;
                }
                mode = Mode.BETWEEN;
                if (overflow) {
                    return new Damaged();
                }
                byte[] encoded = current.toByteArray();
                Frame4.Frame frame = Frame4.parse(Frame4.cobsDecode(encoded, encoded.length));
                return frame != null ? new Frame(frame) : new Damaged();
            }
        }
    }

    private void add(byte b) {
        if (current.size() >= maxBytes) {
            overflow = true;
            return;
        }
        current.write(b);
    }
}
