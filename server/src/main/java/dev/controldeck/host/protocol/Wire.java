package dev.controldeck.host.protocol;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/**
 * Protocol 4's payloads, as the firmware's {@code deck::wire}: numbers little endian, texts
 * with a one-byte length. Each message type has its fixed layout (docs/04-PROTOCOL.md).
 */
public final class Wire {

    private Wire() {
    }

    /** Builds a payload. */
    public static final class Writer {
        private final ByteArrayOutputStream out = new ByteArrayOutputStream();

        public Writer u8(int v) {
            out.write(v & 0xFF);
            return this;
        }

        public Writer u16(int v) {
            out.write(v & 0xFF);
            out.write((v >> 8) & 0xFF);
            return this;
        }

        public Writer u32(long v) {
            for (int i = 0; i < 4; i++) {
                out.write((int) (v >> (8 * i)) & 0xFF);
            }
            return this;
        }

        public Writer f32(float v) {
            return u32(Float.floatToIntBits(v) & 0xFFFFFFFFL);
        }

        /** NaN for a value that was not measured (null). */
        public Writer f32(Double v) {
            return f32(v == null ? Float.NaN : v.floatValue());
        }

        /** A text of up to 255 bytes of UTF-8, cut at a character boundary. */
        public Writer str(String text) {
            byte[] bytes = text == null ? new byte[0] : text.getBytes(StandardCharsets.UTF_8);
            int n = Math.min(bytes.length, 255);
            while (n > 0 && n < bytes.length && (bytes[n] & 0xC0) == 0x80) {
                n--;  // not in the middle of a character
            }
            u8(n);
            out.write(bytes, 0, n);
            return this;
        }

        public Writer bytes(byte[] data) {
            out.write(data, 0, data.length);
            return this;
        }

        public int size() {
            return out.size();
        }

        public byte[] toBytes() {
            return out.toByteArray();
        }
    }

    /** Reads what a Writer (or the firmware) wrote; past the end it throws. */
    public static final class Reader {
        private final byte[] in;
        private int at;

        public Reader(byte[] in) {
            this.in = in;
        }

        private void need(int n) {
            if (at + n > in.length) {
                throw new IllegalArgumentException("payload too short");
            }
        }

        public int u8() {
            need(1);
            return in[at++] & 0xFF;
        }

        public int u16() {
            need(2);
            int v = (in[at] & 0xFF) | (in[at + 1] & 0xFF) << 8;
            at += 2;
            return v;
        }

        public long u32() {
            need(4);
            long v = 0;
            for (int i = 0; i < 4; i++) {
                v |= (long) (in[at + i] & 0xFF) << (8 * i);
            }
            at += 4;
            return v;
        }

        public float f32() {
            return Float.intBitsToFloat((int) u32());
        }

        public String str() {
            int n = u8();
            need(n);
            String text = new String(in, at, n, StandardCharsets.UTF_8);
            at += n;
            return text;
        }

        public byte[] bytes(int n) {
            need(n);
            byte[] out = Arrays.copyOfRange(in, at, at + n);
            at += n;
            return out;
        }

        public int left() {
            return in.length - at;
        }
    }
}
