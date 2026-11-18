package dev.controldeck.host.protocol;

import java.io.ByteArrayOutputStream;
import java.util.Arrays;

/**
 * Protocol 4's frames (docs/04-PROTOCOL.md), the same bytes as the firmware's
 * {@code app/deck_frame.h}: a 10-byte binary header and a CRC-16 around a binary payload,
 * COBS-encoded and ended by one 0x00 on the serial line (USB, BLE).
 */
public final class Frame4 {

    public static final int VERSION = 0x40;
    public static final int HEADER = 8;
    public static final int OVERHEAD = HEADER + 2;
    public static final int MAX_PAYLOAD = 2048;

    /** A checked frame. */
    public record Frame(int type, int channel, int seq, byte[] payload) {
    }

    private Frame4() {
    }

    /** CRC-16/CCITT-FALSE: polynomial 0x1021, initial 0xFFFF. */
    public static int crc16(byte[] data, int offset, int length) {
        int crc = 0xFFFF;
        for (int i = offset; i < offset + length; i++) {
            crc ^= (data[i] & 0xFF) << 8;
            for (int bit = 0; bit < 8; bit++) {
                crc = (crc & 0x8000) != 0 ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
            }
        }
        return crc;
    }

    public static byte[] cobsEncode(byte[] in) {
        ByteArrayOutputStream out = new ByteArrayOutputStream(in.length + in.length / 254 + 2);
        byte[] block = new byte[255];
        int n = 1;
        for (byte b : in) {
            if (b == 0) {
                block[0] = (byte) n;
                out.write(block, 0, n);
                n = 1;
            } else {
                block[n++] = b;
                if (n == 255) {
                    block[0] = (byte) 255;
                    out.write(block, 0, 255);
                    n = 1;
                }
            }
        }
        block[0] = (byte) n;
        out.write(block, 0, n);
        return out.toByteArray();
    }

    /** The reverse; null for bytes that are not COBS. */
    public static byte[] cobsDecode(byte[] in, int length) {
        ByteArrayOutputStream out = new ByteArrayOutputStream(length);
        int i = 0;
        while (i < length) {
            int code = in[i++] & 0xFF;
            if (code == 0) {
                return null;
            }
            for (int k = 1; k < code; k++) {
                if (i >= length || in[i] == 0) {
                    return null;
                }
                out.write(in[i++]);
            }
            if (code != 0xFF && i < length) {
                out.write(0);
            }
        }
        return out.toByteArray();
    }

    /** Header, payload and CRC. */
    public static byte[] build(int type, int channel, int seq, byte[] payload) {
        if (payload.length > MAX_PAYLOAD) {
            throw new IllegalArgumentException("payload of " + payload.length + " bytes, at most " + MAX_PAYLOAD);
        }
        byte[] raw = new byte[OVERHEAD + payload.length];
        raw[0] = (byte) VERSION;
        raw[2] = (byte) type;
        raw[3] = (byte) channel;
        raw[4] = (byte) seq;
        raw[5] = (byte) (seq >> 8);
        raw[6] = (byte) payload.length;
        raw[7] = (byte) (payload.length >> 8);
        System.arraycopy(payload, 0, raw, HEADER, payload.length);
        int crc = crc16(raw, 0, HEADER + payload.length);
        raw[HEADER + payload.length] = (byte) crc;
        raw[HEADER + payload.length + 1] = (byte) (crc >> 8);
        return raw;
    }

    /** A decoded frame checked (version, length, CRC), or null. */
    public static Frame parse(byte[] raw) {
        if (raw == null || raw.length < OVERHEAD || (raw[0] & 0xF0) != VERSION || raw[1] != 0) {
            return null;
        }
        int length = (raw[6] & 0xFF) | (raw[7] & 0xFF) << 8;
        if (raw.length != OVERHEAD + length) {
            return null;
        }
        int crc = (raw[HEADER + length] & 0xFF) | (raw[HEADER + length + 1] & 0xFF) << 8;
        if (crc != crc16(raw, 0, HEADER + length)) {
            return null;
        }
        return new Frame(raw[2] & 0xFF, raw[3] & 0xFF, (raw[4] & 0xFF) | (raw[5] & 0xFF) << 8,
                Arrays.copyOfRange(raw, HEADER, HEADER + length));
    }

    /** A frame as it goes on the line: COBS, then the 0x00. */
    public static byte[] encodeForLine(int type, int channel, int seq, byte[] payload) {
        byte[] encoded = cobsEncode(build(type, channel, seq, payload));
        byte[] line = Arrays.copyOf(encoded, encoded.length + 1);
        line[encoded.length] = 0;
        return line;
    }
}
