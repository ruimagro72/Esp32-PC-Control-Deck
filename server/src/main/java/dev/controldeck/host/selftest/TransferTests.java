package dev.controldeck.host.selftest;

import static dev.controldeck.host.selftest.Runner.check;
import static dev.controldeck.host.selftest.Runner.equal;

import dev.controldeck.host.firmware.FirmwareStore;
import dev.controldeck.host.inbox.MessageStore;
import dev.controldeck.host.inbox.Voice;
import dev.controldeck.host.protocol.MessageHandler;
import dev.controldeck.host.protocol.Protocol;
import dev.controldeck.host.protocol.Codec4;
import dev.controldeck.host.transfer.Transfers;
import dev.controldeck.host.transfer.Uploads;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

/** Transfers (docs/04-PROTOCOL.md): the firmware image in blocks, paced by the Deck's acks. */
final class TransferTests {

    private TransferTests() {
    }

    static void run(Runner r) {
        r.test("transfer: the image arrives whole through acks, and a lost block is sent again", () -> {
            Path dir = Files.createTempDirectory("cd-transfer");
            byte[] image = new byte[5000];
            for (int i = 0; i < image.length; i++) {
                image[i] = (byte) (i * 7);
            }
            byte[] tag = "DECKFW:0.5.7-es3c28p\0".getBytes(StandardCharsets.US_ASCII);
            System.arraycopy(tag, 0, image, 100, tag.length);
            // the last 80 bytes are a signature trailer (only signed images are offered)
            System.arraycopy("DECKSIG1".getBytes(StandardCharsets.US_ASCII), 0, image, image.length - 80, 8);
            image[image.length - 80 + 8] = 70;
            Files.write(dir.resolve("es3c28p.bin"), image);
            FirmwareStore store = new FirmwareStore(dir);
            Transfers transfers = new Transfers();

            check(transfers.start(Transfers.KIND_FIRMWARE, "nothing", store) == null, "no image: nothing to send");
            List<Map<String, Object>> first = transfers.start(Transfers.KIND_FIRMWARE, "es3c28p", store);
            equal("transfer_begin", first.get(0).get("type"), "begin first");
            equal(5000L, first.get(0).get("size"), "size");
            equal(1 + Transfers.WINDOW, first.size(), "then a window of blocks");
            int id = (Integer) first.get(0).get("id");

            ByteArrayOutputStream kept = new ByteArrayOutputStream();
            long expected = 0;
            List<Map<String, Object>> incoming = first.subList(1, first.size());
            boolean dropped = false;
            for (int round = 0; round < 50 && expected < image.length; round++) {
                boolean progressed = false;
                for (Map<String, Object> block : incoming) {
                    long offset = (Long) block.get("offset");
                    if (!dropped && offset == 2048) {
                        dropped = true;  // lost on the line
                        continue;
                    }
                    if (offset == expected) {
                        byte[] bytes = (byte[]) block.get("bytes");
                        kept.writeBytes(bytes);
                        expected += bytes.length;
                        progressed = true;
                    }
                }
                // The Deck acks what it has: after a loss it names the same offset again.
                incoming = transfers.ack(id, expected);
                if (!progressed && incoming.isEmpty()) {
                    break;
                }
            }
            check(dropped, "a block was lost on the way");
            check(Arrays.equals(image, kept.toByteArray()), "and the image still arrived whole: " + kept.size());
            transfers.end(id);
            equal(0, transfers.openCount(), "the transfer is forgotten at the end");
        });
        r.test("transfer: begin and data frames in binary", () -> {
            Codec4.Out begin = Codec4.encode(Map.of("type", "transfer_begin", "id", 3, "kind", 1, "size", 5000L,
                    "md5", "0123456789abcdef0123456789abcdef"));
            equal(Codec4.TRANSFER_BEGIN, begin.type(), "begin type");
            equal(2 + 1 + 4 + 1 + 32, begin.payload().length, "begin layout");
            Codec4.Out data = Codec4.encode(Map.of("type", "transfer_data", "id", 3, "offset", 1024L,
                    "bytes", new byte[] {1, 2, 3}));
            equal(Codec4.TRANSFER_DATA, data.type(), "data type");
            equal(2 + 4 + 3, data.payload().length, "data layout: id, offset, the bytes");
        });
        r.test("upload: a voice message from a Deck, a block lost on the way, kept for the PC as WAV", () -> {
            Path dir = Files.createTempDirectory("cd-upload");
            MessageStore store = new MessageStore(dir.resolve("messages.json"));
            MessageHandler handler = new MessageHandler(null, null, null);
            handler.inbox(store);
            byte[] pcm = new byte[3 * 1024 + 500];
            for (int i = 0; i < pcm.length; i++) {
                pcm[i] = (byte) (i * 13);
            }
            String deck = "84C7BB78E0C0";
            Map<String, Object> ack = handler.handle(MessageHandler.map("type", "upload_begin", "id", 9, "kind", 2,
                    "size", (long) pcm.length, "deck", deck, "to", ""), 2048).get(0);
            equal("upload_ack", ack.get("type"), "begin is answered with an ack");
            equal(0L, ack.get("next"), "from the start");
            long next = 0;
            boolean lost = false;
            for (int round = 0; round < 20 && next < pcm.length; round++) {
                int length = (int) Math.min(1024, pcm.length - next);
                long offset = next;
                if (!lost && offset == 1024) {
                    lost = true;  // this block never reached the server: the Deck sends the next one
                    offset = 2048;
                    length = 1024;
                }
                byte[] block = Arrays.copyOfRange(pcm, (int) offset, (int) offset + length);
                ack = handler.handle(MessageHandler.map("type", "upload_data", "id", 9, "deck", deck,
                        "offset", offset, "bytes", block), 2048).get(0);
                next = (Long) ack.get("next");
            }
            check(lost, "a block was lost");
            equal((long) pcm.length, next, "everything arrived");
            List<Map<String, Object>> all = store.all();
            equal(1, all.size(), "one message");
            equal("pc", all.get(0).get("to"), "for the people at the PC");
            equal("voice", all.get(0).get("kind"), "voice");
            long id = (Long) all.get(0).get("id");
            check(Arrays.equals(pcm, store.audio(id)), "the audio kept whole");
            check(Arrays.equals(pcm, Voice.fromWav(Voice.toWav(pcm))), "WAV there and back");
            check(Protocol.REQUEST_TYPES.containsAll(List.of("upload_begin", "upload_data")), "the serial line passes them on");

            // The other way: a Deck fetches a voice message's audio (kind 2) like the firmware.
            List<Map<String, Object>> frames = handler.handle(MessageHandler.map("type", "transfer_request", "kind", 2,
                    "deck", deck, "what", String.valueOf(id)), 2048);
            equal("transfer_begin", frames.get(0).get("type"), "the audio is sent");
            equal((long) pcm.length, frames.get(0).get("size"), "its size");
            try {
                Voice.fromWav(Voice.toWav(pcm).length > 0 ? new byte[50] : null);
                check(false, "a file that is no WAV is refused");
            } catch (IllegalArgumentException expected) {
                check(true, "a file that is no WAV is refused");
            }
        });
        r.test("upload: upload_ack in binary, and sizes out of range refused", () -> {
            Codec4.Out out = Codec4.encode(Map.of("type", "upload_ack", "id", 9, "next", 4096L));
            equal(Codec4.UPLOAD_ACK, out.type(), "type");
            equal(6, out.payload().length, "u16 id, u32 next");
            Uploads uploads = new Uploads(1000);
            try {
                uploads.begin("84C7BB78E0C0", 1, Uploads.KIND_VOICE, 5000, "");
                check(false, "too big is refused");
            } catch (IllegalArgumentException expected) {
                check(true, "too big is refused");
            }
        });
    }
}
