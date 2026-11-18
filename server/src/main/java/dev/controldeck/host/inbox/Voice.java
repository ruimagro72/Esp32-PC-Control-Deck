package dev.controldeck.host.inbox;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;

/**
 * Voice messages' audio: 16 kHz, 16-bit, mono PCM, little endian (docs/04-PROTOCOL.md,
 * "Transfers", kind 2), kept raw and given out as WAV.
 */
public final class Voice {

    public static final int RATE = 16000;
    public static final int BYTES_PER_SECOND = RATE * 2;
    public static final int MAX_SECONDS = 60;
    public static final int MAX_BYTES = MAX_SECONDS * BYTES_PER_SECOND;

    private Voice() {
    }

    /** "12 s": what the inbox shows as the message's text. */
    public static String lengthText(int pcmBytes) {
        int seconds = Math.max(1, Math.round(pcmBytes / (float) BYTES_PER_SECOND));
        return seconds + " s";
    }

    /** The PCM in a WAV file (44-byte header). */
    public static byte[] toWav(byte[] pcm) {
        ByteBuffer b = ByteBuffer.allocate(44 + pcm.length).order(ByteOrder.LITTLE_ENDIAN);
        b.put("RIFF".getBytes()).putInt(36 + pcm.length).put("WAVE".getBytes());
        b.put("fmt ".getBytes()).putInt(16).putShort((short) 1).putShort((short) 1).putInt(RATE)
                .putInt(BYTES_PER_SECOND).putShort((short) 2).putShort((short) 16);
        b.put("data".getBytes()).putInt(pcm.length).put(pcm);
        return b.array();
    }

    /** The PCM of a WAV file in the Deck's format; anything else is refused, saying how to convert it. */
    public static byte[] fromWav(byte[] wav) {
        String convert = " (convert it: ffmpeg -i in.wav -ar 16000 -ac 1 -sample_fmt s16 out.wav)";
        if (wav.length < 44 || !"RIFF".equals(new String(wav, 0, 4)) || !"WAVE".equals(new String(wav, 8, 4))) {
            throw new IllegalArgumentException("not a WAV file" + convert);
        }
        ByteBuffer b = ByteBuffer.wrap(wav).order(ByteOrder.LITTLE_ENDIAN);
        int at = 12;
        boolean formatOk = false;
        while (at + 8 <= wav.length) {
            String id = new String(wav, at, 4);
            int size = b.getInt(at + 4);
            int body = at + 8;
            if (size < 0 || body + size > wav.length) {
                size = wav.length - body;  // some writers leave the size open
            }
            if ("fmt ".equals(id) && size >= 16) {
                formatOk = b.getShort(body) == 1 && b.getShort(body + 2) == 1 && b.getInt(body + 4) == RATE
                        && b.getShort(body + 14) == 16;
            } else if ("data".equals(id)) {
                if (!formatOk) {
                    throw new IllegalArgumentException("the WAV must be 16 kHz, 16-bit, mono PCM" + convert);
                }
                int length = Math.min(size, MAX_BYTES) & ~1;
                return Arrays.copyOfRange(wav, body, body + length);
            }
            at = body + size + (size & 1);
        }
        throw new IllegalArgumentException("the WAV has no audio" + convert);
    }
}
