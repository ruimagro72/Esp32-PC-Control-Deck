package dev.controldeck.host.selftest;

import static dev.controldeck.host.selftest.Runner.check;
import static dev.controldeck.host.selftest.Runner.equal;

import dev.controldeck.host.Services;
import dev.controldeck.host.config.ConfigService;
import dev.controldeck.host.firmware.FirmwareStore;
import dev.controldeck.host.http.Request;
import dev.controldeck.host.http.Response;
import dev.controldeck.host.http.Router;
import dev.controldeck.host.protocol.MessageHandler;
import dev.controldeck.host.protocol.Protocol;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.sql.DriverManager;
import java.util.Arrays;
import java.util.Map;

/** Updates over the air: the firmware store, the offer a Deck gets, the image route. */
final class FirmwareTests {

    private FirmwareTests() {
    }

    static void run(Runner r) {
        r.test("firmware: an image names its version after DECKFW:, and a file without it is no image", () -> {
            equal("0.5.7-es3c28p", FirmwareStore.versionOf(image("0.5.7-es3c28p")), "version");
            equal(null, FirmwareStore.versionOf("no tag here".getBytes(StandardCharsets.US_ASCII)), "no tag");
            equal(null, FirmwareStore.versionOf("xxDECKFW:".getBytes(StandardCharsets.US_ASCII)), "tag at the end");
            check(!FirmwareStore.validBoard("../etc"), "a path is not a board");
            check(FirmwareStore.validBoard("e32r35t"), "a board name");

            Path dir = Files.createTempDirectory("cd-fw-store");
            Files.write(dir.resolve("es3c28p.bin"), image("0.5.7-es3c28p"));
            Files.write(dir.resolve("e32r35t.bin"), "not firmware".getBytes(StandardCharsets.US_ASCII));
            FirmwareStore store = new FirmwareStore(dir);
            FirmwareStore.Image found = store.find("es3c28p").orElseThrow();
            equal("0.5.7-es3c28p", found.version(), "found version");
            equal(64, found.sha256().length(), "sha256 in hex");
            check(store.find("e32r35t").isEmpty(), "a file without the tag is not offered");
            check(store.find("nothing").isEmpty(), "no file, no image");
        });
        r.test("firmware: firmware_check offers another version, not the one running; the route serves the bytes (needs sqlite-jdbc)", () -> {
            requireSqlite();
            Path dir = Files.createTempDirectory("cd-fw-offer");
            Services services = new Services(new ConfigService(dir.resolve("control-deck.db")), "", dir.resolve("var"));
            MessageHandler messages = services.messages();
            Path firmware = messages.firmwareStore().dir();
            Files.createDirectories(firmware);
            byte[] bytes = image("0.5.7-es3c28p");
            Files.write(firmware.resolve("es3c28p.bin"), bytes);

            Map<String, Object> offer = messages.handle(
                    MessageHandler.map("type", "firmware_check", "board", "es3c28p", "firmware", "0.5.6-es3c28p"), 4096).get(0);
            equal("firmware_offer", offer.get("type"), "type");
            check(Protocol.REQUEST_TYPES.contains("firmware_check"), "the serial line passes the question on");
            equal(true, offer.get("available"), "a newer image is offered");
            equal("0.5.7-es3c28p", offer.get("version"), "its version");
            equal((long) bytes.length, ((Number) offer.get("size")).longValue(), "its size");
            equal(FirmwareStore.sha256(bytes), offer.get("sha256"), "its hash");
            equal(32, String.valueOf(offer.get("md5")).length(), "the MD5 the board's update library checks");
            equal("/api/firmware/es3c28p", offer.get("path"), "where to fetch it");

            Map<String, Object> same = messages.handle(
                    MessageHandler.map("type", "firmware_check", "board", "es3c28p", "firmware", "0.5.7-es3c28p"), 4096).get(0);
            equal(false, same.get("available"), "the running version is not offered again");
            Map<String, Object> none = messages.handle(
                    MessageHandler.map("type", "firmware_check", "board", "e32r35t", "firmware", "0.5.6-e32r35t"), 4096).get(0);
            equal(false, none.get("available"), "no image for the board");

            Router router = new Router(services);
            Response served = router.handle(new Request("GET", "/api/firmware/es3c28p", Map.of(),
                    Map.of("Host", "127.0.0.1:8080"), "", "127.0.0.1"));
            equal(200, served.status(), "the image is served on this PC");
            check(served.body() instanceof byte[] raw && Arrays.equals(raw, bytes), "byte for byte");
            equal(404, router.handle(new Request("GET", "/api/firmware/e32r35t", Map.of(),
                    Map.of("Host", "127.0.0.1:8080"), "", "127.0.0.1")).status(), "no image: 404");
            equal(403, router.handle(new Request("GET", "/api/firmware/es3c28p", Map.of(),
                    Map.of("Host", "10.0.0.2:8080"), "", "10.0.0.5")).status(), "the LAN needs a Deck token");
            services.close();
        });
    }

    /** A stand-in image: some bytes, the tag and version, a terminating zero, more bytes. */
    private static byte[] image(String version) {
        byte[] head = new byte[300];
        Arrays.fill(head, (byte) 0xE9);
        byte[] tag = ("DECKFW:" + version + "\0").getBytes(StandardCharsets.US_ASCII);
        byte[] out = Arrays.copyOf(head, head.length + tag.length + 100);
        System.arraycopy(tag, 0, out, head.length, tag.length);
        // the 80-byte trailer of a signed image (the store checks its form; the Deck checks the signature)
        byte[] signed = Arrays.copyOf(out, out.length + 80);
        System.arraycopy("DECKSIG1".getBytes(StandardCharsets.US_ASCII), 0, signed, out.length, 8);
        signed[out.length + 8] = 70;
        return signed;
    }

    private static void requireSqlite() {
        if (DriverManager.drivers().noneMatch(d -> d.getClass().getName().contains("sqlite"))) {
            throw new Runner.Skip("sqlite-jdbc not on the class path; run with the shaded jar");
        }
    }
}
