package dev.controldeck.host.selftest;

import dev.controldeck.host.config.ConfigService;
import dev.controldeck.host.macro.MacroExecutor;
import dev.controldeck.host.macro.MacroService;
import dev.controldeck.host.protocol.MessageHandler;
import dev.controldeck.host.telemetry.Inventory;
import dev.controldeck.host.transport.TransportRegistry;
import dev.controldeck.host.transport.serial.JSerialCommProvider;
import dev.controldeck.host.protocol.Codec4;
import dev.controldeck.host.protocol.Frame4;
import dev.controldeck.host.protocol.Wire;
import dev.controldeck.host.transport.serial.LineSplitter;
import dev.controldeck.host.transport.serial.PortInfo;
import dev.controldeck.host.transport.serial.PortResolver;
import dev.controldeck.host.transport.serial.SerialBridge;
import dev.controldeck.host.transport.serial.SerialLog;
import dev.controldeck.host.transport.serial.SerialPortIO;
import dev.controldeck.host.transport.serial.SerialPortProvider;
import dev.controldeck.host.transport.serial.SerialSession;
import dev.controldeck.host.util.Json;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.sql.DriverManager;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.BooleanSupplier;

import static dev.controldeck.host.selftest.Runner.check;
import static dev.controldeck.host.selftest.Runner.equal;

/**
 * The USB serial bridge (protocol 4 frames): port choice, the conversation with the board
 * (against a recorded board, no hardware) and the bridge loop with a fake serial port.
 */
final class SerialTests {

    private static final long SECOND = 1_000_000_000L;

    private SerialTests() {
    }

    static void run(Runner r) {
        portChoice(r);
        session(r);
        bridge(r);
        registry(r);
        library(r);
    }

    // -- pure pieces -----------------------------------------------------------------

    private static void portChoice(Runner r) {
        r.test("serial: the operating system is told apart (darwin contains win)", () -> {
            equal(PortResolver.Os.MAC, PortResolver.Os.detect("Mac OS X"), "mac");
            equal(PortResolver.Os.MAC, PortResolver.Os.detect("Darwin"), "darwin");
            equal(PortResolver.Os.WINDOWS, PortResolver.Os.detect("Windows 11"), "windows");
            equal(PortResolver.Os.LINUX, PortResolver.Os.detect("Linux"), "linux");
        });
        r.test("serial: auto picks the only board and refuses to guess", () -> {
            PortInfo acm = new PortInfo("/dev/ttyACM0", "USB JTAG/serial debug unit");
            PortInfo serialPort = new PortInfo("/dev/ttyS0", "ttyS0");
            equal("/dev/ttyACM0", PortResolver.resolve("auto", () -> List.of(serialPort, acm), PortResolver.Os.LINUX), "single board");
            equal("/dev/ttyACM0", PortResolver.resolve("AUTO", () -> List.of(acm), PortResolver.Os.LINUX), "case is ignored");
            expectRefusal(() -> PortResolver.resolve("auto", () -> List.of(serialPort), PortResolver.Os.LINUX), "No serial device found");
            expectRefusal(() -> PortResolver.resolve("auto", () -> List.of(acm, new PortInfo("/dev/ttyUSB0", "CH340")),
                    PortResolver.Os.LINUX), "Multiple serial devices found");
            PortInfo deck = new PortInfo("/dev/ttyACM0", "USB JTAG/serial debug unit", PortInfo.VENDOR_ESPRESSIF);
            PortInfo ch340 = new PortInfo("/dev/ttyUSB0", "USB Serial", 0x1A86);
            equal("/dev/ttyACM0", PortResolver.resolve("auto", () -> List.of(ch340, deck), PortResolver.Os.LINUX),
                    "the one Espressif native USB board wins over a USB-UART chip");
            expectRefusal(() -> PortResolver.resolve("auto", () -> List.of(deck,
                    new PortInfo("/dev/ttyACM1", "USB JTAG/serial debug unit", PortInfo.VENDOR_ESPRESSIF)),
                    PortResolver.Os.LINUX), "Multiple serial devices found");
            equal("/dev/cu.usbmodem1101", PortResolver.resolve("auto",
                    () -> List.of(new PortInfo("/dev/cu.usbmodem1101", ""), new PortInfo("/dev/tty.usbmodem1101", "")),
                    PortResolver.Os.MAC), "macOS uses the cu device");
            equal("COM4", PortResolver.resolve("auto", () -> List.of(new PortInfo("COM1", "Communications Port"),
                    new PortInfo("COM3", "Standard Serial over Bluetooth link (COM3) usb"),
                    new PortInfo("COM4", "USB Serial Device (COM4)")), PortResolver.Os.WINDOWS), "Windows USB serial only");
        });
        r.test("serial: a configured port is only checked for its shape", () -> {
            equal("/dev/ttyACM3", PortResolver.resolve("/dev/ttyACM3", () -> {
                throw new AssertionError("the port list must not be read for an explicit port");
            }, PortResolver.Os.LINUX), "explicit");
            expectRefusal(() -> PortResolver.resolve("ttyACM0", List::of, PortResolver.Os.LINUX), "must start with /dev/");
            expectRefusal(() -> PortResolver.resolve("COM", List::of, PortResolver.Os.WINDOWS), "COM<number>");
            equal("COM12", PortResolver.resolve("COM12", List::of, PortResolver.Os.WINDOWS), "windows");
        });
    }

    // -- the conversation with the board ---------------------------------------------

    /** Everything a session sends, in order, and every state update. */
    private static final class Rig {
        final List<Map<String, Object>> sent = Collections.synchronizedList(new ArrayList<>());
        final Map<String, Object> state = Collections.synchronizedMap(new LinkedHashMap<>());
        final List<String> warnings = Collections.synchronizedList(new ArrayList<>());
        final AtomicLong clock = new AtomicLong(1_000 * SECOND);
        final SerialSession session;

        Rig(MessageHandler messages, double interval) {
            SerialLog log = new SerialLog() {
                @Override
                public void info(String message) {
                }

                @Override
                public void warn(String message) {
                    warnings.add(message);
                }

                @Override
                public void debug(String message) {
                }
            };
            session = new SerialSession(messages, age -> MessageHandler.map("type", "stats", "cpu", 12.5), interval,
                    sent::add, state::putAll, log, clock::get);
        }

        List<String> types() {
            List<String> types = new ArrayList<>();
            for (Map<String, Object> frame : new ArrayList<>(sent)) {
                types.add(String.valueOf(frame.get("type")));
            }
            return types;
        }

        void hello() throws IOException {
            session.onMessage(MessageHandler.map("type", "hello", "firmware", "0.5.2-es3c28p", "protocol", 4));
        }
    }

    /** A board session whose macros run through a recording launcher instead of real processes. */
    private static final class MacroRig {
        final ConfigService config;
        final Rig rig;

        MacroRig(Path dir, List<List<String>> launched) throws IOException {
            Files.createDirectories(dir);
            Path bin = Files.createDirectories(dir.resolve("bin"));
            Path sh = bin.resolve("sh");
            Files.writeString(sh, "#!/bin/sh\n");
            sh.toFile().setExecutable(true);
            config = new ConfigService(dir.resolve("control-deck.db"));
            MacroExecutor executor = new MacroExecutor(MacroExecutor.Platform.LINUX, Map.of("PATH", bin.toString()),
                    (command, workingDirectory) -> launched.add(List.copyOf(command)), url -> false);
            rig = new Rig(new MessageHandler(config, null, new Inventory(dir.resolve("root").toString(), "/", null),
                    new MacroService(config, executor)), 2.0);
        }
    }

    private static Map<String, Object> frameOf(Rig rig, String type) {
        for (Map<String, Object> frame : new ArrayList<>(rig.sent)) {
            if (type.equals(frame.get("type"))) {
                return frame;
            }
        }
        throw new AssertionError("no " + type + " frame was sent: " + rig.types());
    }

    private static MessageHandler handler(Path dir) {
        ConfigService config = new ConfigService(dir.resolve("control-deck.db"));
        return new MessageHandler(config, null, new Inventory(dir.resolve("root").toString(), "/", null));
    }

    private static void requireSqlite() {
        boolean driver = DriverManager.drivers().anyMatch(d -> d.getClass().getName().contains("sqlite"));
        if (!driver) {
            throw new Runner.Skip("sqlite-jdbc not on the class path; run with the shaded jar");
        }
    }

    private static void session(Runner r) {
        r.test("board: a Deck's description is kept per id, trimmed, and described in one line", () -> {
            requireSqlite();
            MessageHandler messages = handler(Files.createTempDirectory("cd-board"));
            Map<String, Object> board = MessageHandler.map("type", "board", "id", "84C7BB78E0C0",
                    "board", "LCDWiki ES3C28P", "firmware", "0.5.5-es3c28p", "chip", "ESP32-S3", "rev", 2,
                    "cores", 2, "mhz", 240, "flash", 16777216L, "flash_build", 8388608L, "psram", 0,
                    "psram_in_package", true, "radios", List.of("wifi", "ble"), "screen", List.of(320, 240),
                    "display", "ILI9341V", "touch", "FT6336G", "sd", "sdmmc-4bit", "audio", "ES8311",
                    "battery_mv", 4050, "heap", List.of(200000, 150000, 300000), "nvs", List.of(12, 600, 630),
                    "evil", "x".repeat(10000));
            equal(List.of(), messages.handle(board, 1800), "no reply");
            Map<String, Object> kept = messages.boards().get("84C7BB78E0C0");
            check(kept != null && !kept.containsKey("evil"), "only known fields are kept");
            String line = MessageHandler.describeBoard(kept);
            check(line.contains("flash 16 MB (firmware built for 8 MB)"), line);
            check(line.contains("PSRAM in the chip but not enabled"), line);
            check(line.contains("radios wifi+ble") && line.contains("audio ES8311") && line.contains("battery 4.05 V"), line);
            equal("error", messages.handle(MessageHandler.map("type", "board"), 1800).get(0).get("type"),
                    "a board without id is refused");
        });
        r.test("serial: a Deck that moved to Wi-Fi gets nothing more on USB until its next hello", () -> {
            requireSqlite();
            Rig rig = new Rig(handler(Files.createTempDirectory("cd-ser-moved")), 1.0);
            rig.hello();
            rig.session.onMessage(MessageHandler.map("type", "transport", "to", "wifi"));
            equal("wifi", rig.state.get("peer_transport"), "state says where the Deck went");
            int before = rig.sent.size();
            for (int i = 0; i < 10; i++) {
                rig.clock.addAndGet(2 * SECOND);
                rig.session.tick();
            }
            equal(before, rig.sent.size(), "no pings and no stats while the Deck is on Wi-Fi");
            rig.hello();
            equal("usb", rig.state.get("peer_transport"), "back on USB");
            check(rig.sent.size() > before, "the handshake is answered again");
        });
        r.test("serial: a Deck on its cable gets its own Wi-Fi token, or the reason it gets none", () -> {
            requireSqlite();
            Path dir = Files.createTempDirectory("cd-ser-token");
            MessageHandler messages = handler(dir);
            Rig off = new Rig(messages, 1.0);
            Map<String, Object> hello = MessageHandler.map("type", "hello", "firmware", "0.5.4-es3c28p", "protocol", 4,
                    "id", "84C7BB78E0C0");
            off.session.onMessage(hello);
            Map<String, Object> refused = frameOf(off, "wifi_token");
            equal(false, refused.get("available"), "Wi-Fi off: no token");
            equal("Wi-Fi is off on the host", refused.get("reason"), "reason");

            ConfigService config = new ConfigService(dir.resolve("control-deck.db"));
            config.setWifiSettings(Map.of("enabled", true, "bind", "192.168.1.20", "port", 8080));
            Rig on = new Rig(messages, 1.0);
            on.session.onMessage(hello);
            Map<String, Object> access = frameOf(on, "wifi_token");
            equal(true, access.get("available"), "token given");
            equal(48, String.valueOf(access.get("token")).length(), "token length");
            equal(8080, access.get("port"), "port");
            equal(List.of("192.168.1.20"), access.get("addresses"), "the bind address");
            check(Codec4.encode(access).payload().length < Frame4.MAX_PAYLOAD, "fits a frame");

            Rig asking = new Rig(messages, 1.0);
            asking.session.onMessage(MessageHandler.map("type", "wifi_token_request", "id", "84C7BB78E0C0"));
            equal(true, frameOf(asking, "wifi_token").get("available"), "a Deck on Wi-Fi asks on its cable");
            check(!asking.types().contains("host_hello"), "asking is not a handshake: the line stays quiet");

            Rig plain = new Rig(messages, 1.0);
            plain.hello();
            check(!plain.types().contains("wifi_token"), "a hello without the Deck's id gets no token");
            config.close();
        });
        r.test("openaction: a Deck becomes a device, its tap a press, an image becomes rows for that Deck", () -> {
            dev.controldeck.host.transport.openaction.OpenActionBridge bridge =
                    new dev.controldeck.host.transport.openaction.OpenActionBridge();
            List<Map<String, Object>> sent = new java.util.ArrayList<>();
            Map<String, Object> keypad = MessageHandler.map("rows", 2, "columns", 3, "w", 96, "h", 78);
            bridge.deckSeen("AA", keypad);
            bridge.attach(text -> sent.add(Json.parseObject(text)), "registerPlugin", "dev.controldeck.deck");
            equal("registerPlugin", sent.get(0).get("event"), "registers first");
            equal("registerDevice", sent.get(1).get("event"), "a known Deck is announced");
            check(bridge.press("AA", 4), "a press goes to OpenDeck");
            equal("keyDown", sent.get(2).get("event"), "down");
            equal("keyUp", sent.get(3).get("event"), "up");
            check(!bridge.press("BB", 0), "an unknown Deck is not a device");

            java.awt.image.BufferedImage red = new java.awt.image.BufferedImage(72, 72, java.awt.image.BufferedImage.TYPE_INT_RGB);
            java.awt.Graphics2D g = red.createGraphics();
            g.setColor(java.awt.Color.RED);
            g.fillRect(0, 0, 72, 72);
            g.dispose();
            java.io.ByteArrayOutputStream png = new java.io.ByteArrayOutputStream();
            javax.imageio.ImageIO.write(red, "png", png);
            String uri = "data:image/png;base64," + java.util.Base64.getEncoder().encodeToString(png.toByteArray());
            String event = Json.encode(MessageHandler.map("event", "setImage", "device", "cd-AA", "position", 1, "image", uri));
            bridge.onMessage(event);
            List<Map<String, Object>> frames = bridge.take("AA", 100);
            equal(13, frames.size(), "78 rows in frames of 6");
            for (Map<String, Object> frame : frames) {
                check(Json.encode(frame).length() < dev.controldeck.host.protocol.Protocol.SERIAL_MAX_FRAME_BYTES, "fits the line");
            }
            byte[] row = java.util.Base64.getDecoder().decode((String) frames.get(6).get("data"));
            equal(0xF800, (row[96] & 0xFF) | ((row[97] & 0xFF) << 8), "little-endian RGB565, red in the middle");
            bridge.onMessage(event);
            check(bridge.take("AA", 100).isEmpty(), "the same image is not sent again");
            bridge.onMessage(Json.encode(MessageHandler.map("event", "setImage", "device", "cd-AA", "position", 1)));
            equal(true, bridge.take("AA", 100).get(0).get("clear"), "no image: the label again");
            bridge.detach();
            check(!bridge.attached(), "OpenDeck gone");
            equal(6, bridge.take("AA", 100).size(), "every key goes back to its label");
        });
        r.test("discovery: a Deck's broadcast gets the name and port, nothing when Wi-Fi is off or the frame is not ours", () -> {
            java.util.concurrent.atomic.AtomicBoolean on = new java.util.concurrent.atomic.AtomicBoolean(true);
            dev.controldeck.host.transport.DiscoveryResponder responder =
                    new dev.controldeck.host.transport.DiscoveryResponder(0, 8123, on::get);
            responder.start();
            try (java.net.DatagramSocket client = new java.net.DatagramSocket()) {
                client.setSoTimeout(300);
                java.net.InetAddress here = java.net.InetAddress.getLoopbackAddress();
                java.util.function.Function<byte[], byte[]> ask = out -> {
                    try {
                        client.send(new java.net.DatagramPacket(out, out.length, here, responder.port()));
                        java.net.DatagramPacket in = new java.net.DatagramPacket(new byte[256], 256);
                        client.receive(in);
                        return java.util.Arrays.copyOf(in.getData(), in.getLength());
                    } catch (java.net.SocketTimeoutException e) {
                        return null;
                    } catch (java.io.IOException e) {
                        throw new java.io.UncheckedIOException(e);
                    }
                };
                byte[] discover = Frame4.build(Codec4.DISCOVER, 0, 0, new byte[0]);
                Frame4.Frame reply = Frame4.parse(ask.apply(discover));
                equal(Codec4.HOST_HERE, reply.type(), "answer type");
                Wire.Reader here4 = new Wire.Reader(reply.payload());
                equal(8123, here4.u16(), "API port");
                check(!here4.str().isEmpty() && here4.left() == 0, "the name, and nothing else (no token)");
                check(ask.apply("{\"type\":\"discover\"}".getBytes(java.nio.charset.StandardCharsets.UTF_8)) == null,
                        "JSON is not a frame: no answer");
                on.set(false);
                check(ask.apply(discover) == null, "Wi-Fi off: no answer");
            } finally {
                responder.stop();
            }
        });
        r.test("serial: pings once a second until the board answers (needs sqlite-jdbc)", () -> {
            requireSqlite();
            Rig rig = new Rig(handler(Files.createTempDirectory("cd-ser-ping")), 2.0);
            rig.session.tick();
            rig.session.tick();
            equal(List.of("ping"), rig.types(), "one ping at the start");
            rig.clock.addAndGet(SECOND / 2);
            rig.session.tick();
            equal(1, rig.sent.size(), "no ping after half a second");
            rig.clock.addAndGet(SECOND / 2);
            rig.session.tick();
            equal(List.of("ping", "ping"), rig.types(), "second ping after one second");
        });
        r.test("serial: a Deck's hello gets host_hello, config, the inventory and stats (needs sqlite-jdbc)", () -> {
            requireSqlite();
            Rig rig = new Rig(handler(Files.createTempDirectory("cd-ser-hello")), 2.0);
            rig.hello();
            List<String> types = rig.types();
            equal("host_hello", types.get(0), "first frame");
            equal("config", types.get(1), "second frame");
            equal(List.of("inventory", "inventory", "inventory", "inventory", "inventory"), types.subList(2, 7), "five components");
            equal("stats", types.get(7), "stats last");
            equal(8, types.size(), "nothing else");
            check(rig.session.ready(), "ready after the hello");
            equal("0.5.2-es3c28p", rig.state.get("firmware"), "firmware recorded");
            equal(4L, rig.state.get("peer_protocol"), "protocol recorded");
            equal(List.of("history", "inventory", "wifi_token", "key_image"), rig.state.get("peer_capabilities"), "a protocol 4 Deck can do it all");
        });
        r.test("serial: a second hello within three seconds does not repeat the handshake (needs sqlite-jdbc)", () -> {
            requireSqlite();
            Rig rig = new Rig(handler(Files.createTempDirectory("cd-ser-dup")), 2.0);
            rig.hello();
            int after = rig.sent.size();
            rig.clock.addAndGet(2 * SECOND);
            rig.hello();
            equal(after, rig.sent.size(), "duplicate ignored");
            rig.clock.addAndGet(2 * SECOND);
            rig.hello();
            equal(after * 2, rig.sent.size(), "a hello after the window is a reboot and is answered again");
        });
        r.test("serial: a board that answers a ping with pong gets config and stats once (needs sqlite-jdbc)", () -> {
            requireSqlite();
            Rig rig = new Rig(handler(Files.createTempDirectory("cd-ser-pong")), 2.0);
            rig.session.onMessage(MessageHandler.map("type", "pong"));
            equal(List.of("config", "stats"), rig.types(), "handshake from pong");
            rig.session.onMessage(MessageHandler.map("type", "pong"));
            equal(2, rig.sent.size(), "a later pong changes nothing");
            check(rig.session.ready(), "ready");
        });
        r.test("serial: a macro press runs the macro; the Deck gets no answer frame (needs sqlite-jdbc)", () -> {
            requireSqlite();
            Path dir = Files.createTempDirectory("cd-ser-macro");
            List<List<String>> launched = Collections.synchronizedList(new ArrayList<>());
            MacroRig deck = new MacroRig(dir.resolve("deck"), launched);
            deck.config.setMacros(List.of(MessageHandler.map("id", 1, "label", "ECHO", "action", "command",
                    "value", List.of("sh", "-c", "true"))));
            deck.rig.hello();
            int before = deck.rig.sent.size();
            deck.rig.session.onMessage(MessageHandler.map("type", "macro", "id", 1));
            equal(List.of(List.of("sh", "-c", "true")), launched, "the configured command ran");
            equal(before, deck.rig.sent.size(), "no reply: the console says what ran");
        });
        r.test("serial: a changed macro list reaches the board without a reconnect (needs sqlite-jdbc)", () -> {
            requireSqlite();
            MacroRig board = new MacroRig(Files.createTempDirectory("cd-ser-config"), new ArrayList<>());
            board.rig.hello();
            long configs = board.rig.types().stream().filter("config"::equals).count();
            equal(1L, configs, "one config frame at the handshake");

            board.config.setMacros(List.of(MessageHandler.map("id", 2, "label", "NEW ONE", "action", "url",
                    "value", "https://example.com")));
            board.rig.clock.addAndGet(1 * SECOND);
            board.rig.session.tick();
            equal(1L, board.rig.types().stream().filter("config"::equals).count(), "not checked again before 2 s");

            board.rig.clock.addAndGet(2 * SECOND);
            board.rig.session.tick();
            List<Map<String, Object>> frames = new ArrayList<>(board.rig.sent);
            Map<String, Object> last = null;
            for (Map<String, Object> frame : frames) {
                if ("config".equals(frame.get("type"))) {
                    last = frame;
                }
            }
            equal(2L, board.rig.types().stream().filter("config"::equals).count(), "the new list was sent");
            check(String.valueOf(last).contains("NEW ONE"), "the new label: " + last);

            board.rig.clock.addAndGet(3 * SECOND);
            board.rig.session.tick();
            equal(2L, board.rig.types().stream().filter("config"::equals).count(), "an unchanged list is not sent again");
        });
        r.test("serial: a sampling failure is reported once and does not drop the connection (needs sqlite-jdbc)", () -> {
            requireSqlite();
            List<String> warnings = Collections.synchronizedList(new ArrayList<>());
            List<Map<String, Object>> sent = Collections.synchronizedList(new ArrayList<>());
            AtomicLong clock = new AtomicLong(1_000 * SECOND);
            SerialLog log = new SerialLog() {
                @Override
                public void info(String message) {
                }

                @Override
                public void warn(String message) {
                    warnings.add(message);
                }

                @Override
                public void debug(String message) {
                }
            };
            SerialSession session = new SerialSession(handler(Files.createTempDirectory("cd-ser-fail")), age -> {
                throw new IllegalStateException("no sample");
            }, 0.5, sent::add, fields -> { }, log, clock::get);
            session.onMessage(MessageHandler.map("type", "pong"));
            for (int i = 0; i < 4; i++) {
                clock.addAndGet(SECOND);
                session.tick();
            }
            equal(1, warnings.size(), "the same problem is not repeated: " + warnings);
            check(session.ready(), "the session carries on");
            List<String> types = sent.stream().map(f -> String.valueOf(f.get("type"))).toList();
            equal(List.of("config"), types.stream().filter(t -> !t.equals("ping")).toList(), "no stats frame was invented");
            check(types.contains("ping"), "a pong with no hello: pinged again for the hello");
        });
        r.test("serial: stats follow the telemetry interval once the board is ready (needs sqlite-jdbc)", () -> {
            requireSqlite();
            Rig rig = new Rig(handler(Files.createTempDirectory("cd-ser-stats")), 2.0);
            rig.hello();
            int after = rig.sent.size();
            rig.clock.addAndGet(SECOND);
            rig.session.tick();
            equal(after, rig.sent.size(), "not yet after 1 s");
            rig.clock.addAndGet(SECOND + 1);
            rig.session.tick();
            equal(after + 1, rig.sent.size(), "one stats frame after 2 s");
            equal("stats", rig.sent.get(after).get("type"), "type");
            rig.session.tick();
            equal(after + 1, rig.sent.size(), "and not again on the next tick");
        });
    }

    // -- the bridge loop with a fake port ----------------------------------------------

    /** A board on a fake cable: it says hello when pinged and records what the host writes. */
    private static final class FakeBoard implements SerialPortProvider {
        final List<PortInfo> ports = Collections.synchronizedList(new ArrayList<>());
        final List<Map<String, Object>> received = Collections.synchronizedList(new ArrayList<>());
        final List<String> opened = Collections.synchronizedList(new ArrayList<>());
        volatile FakePort current;
        volatile boolean boardAnswers = true;
        volatile int answersAtBaud = 0;  // 0: at any rate; otherwise only at this one (a USB-UART board)
        volatile int openBaud;

        @Override
        public List<PortInfo> listPorts() {
            return new ArrayList<>(ports);
        }

        @Override
        public SerialPortIO open(String path, int baud) {
            opened.add(path + "@" + baud);
            openBaud = baud;
            FakePort port = new FakePort(this);
            current = port;
            return port;
        }

        void unplug() {
            FakePort port = current;
            if (port != null) {
                port.gone = true;
            }
        }
    }

    private static final class FakePort implements SerialPortIO {
        private final FakeBoard board;
        private final BlockingQueue<byte[]> toHost = new LinkedBlockingQueue<>();
        private final LineSplitter splitter = new LineSplitter(8192);
        volatile boolean gone;
        volatile boolean closed;

        FakePort(FakeBoard board) {
            this.board = board;
        }

        @Override
        public int read(byte[] buffer) throws IOException {
            if (gone) {
                throw new IOException("Serial port disconnected");
            }
            try {
                byte[] chunk = toHost.poll(20, TimeUnit.MILLISECONDS);
                if (chunk == null) {
                    return 0;
                }
                System.arraycopy(chunk, 0, buffer, 0, chunk.length);
                return chunk.length;
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return 0;
            }
        }

        @Override
        public void write(byte[] data, int offset, int length) throws IOException {
            if (gone) {
                throw new IOException("Serial port disconnected while writing a protocol frame");
            }
            synchronized (splitter) {
                byte[] chunk = java.util.Arrays.copyOfRange(data, offset, offset + length);
                for (LineSplitter.Item item : splitter.feed(chunk, chunk.length)) {
                    if (!(item instanceof LineSplitter.Frame f)) {
                        continue;
                    }
                    String type = TYPE_NAMES.getOrDefault(f.frame().type(), "?");
                    board.received.add(MessageHandler.map("type", type));
                    if ("ping".equals(type) && board.boardAnswers
                            && (board.answersAtBaud == 0 || board.answersAtBaud == board.openBaud)) {
                        byte[] hello = new Wire.Writer().u8(4).str("020000000001").str("fake-1").str("es3c28p").toBytes();
                        toHost.add(Frame4.encodeForLine(Codec4.HELLO, 0, 0, hello));
                    }
                }
            }
        }

        @Override
        public void close() {
            closed = true;
        }
    }

    /** The names of the server's frame types, for what the fake board received. */
    private static final Map<Integer, String> TYPE_NAMES = Map.ofEntries(
            Map.entry(Codec4.HOST_HELLO, "host_hello"), Map.entry(Codec4.PING, "ping"), Map.entry(Codec4.STATS, "stats"),
            Map.entry(Codec4.CONFIG, "config"), Map.entry(Codec4.INVENTORY, "inventory"),
            Map.entry(Codec4.WIFI_TOKEN, "wifi_token"), Map.entry(Codec4.ERROR, "error"));

    private static boolean waitUntil(BooleanSupplier condition) throws InterruptedException {
        long end = System.nanoTime() + 8 * SECOND;
        while (System.nanoTime() < end) {
            if (condition.getAsBoolean()) {
                return true;
            }
            Thread.sleep(20);
        }
        return condition.getAsBoolean();
    }

    private static Object usbState(TransportRegistry registry, String key) {
        Object usb = registry.state().get("usb");
        return usb instanceof Map<?, ?> map ? map.get(key) : null;
    }

    private static SerialBridge bridge(FakeBoard board, TransportRegistry registry, MessageHandler messages,
                                       SerialBridge.Settings settings) {
        return new SerialBridge(() -> settings, messages, age -> MessageHandler.map("type", "stats", "cpu", 1.0), registry,
                board, SerialLog.NONE, 50, PortResolver.Os.LINUX);
    }

    private static void bridge(Runner r) {
        r.test("serial: the bridge finds the board, handshakes, reconnects after an unplug and stops cleanly (needs sqlite-jdbc)", () -> {
            requireSqlite();
            Path dir = Files.createTempDirectory("cd-ser-bridge");
            ConfigService config = new ConfigService(dir.resolve("control-deck.db"));
            TransportRegistry registry = new TransportRegistry(config, dir.resolve("var"));
            MessageHandler messages = new MessageHandler(config, null, new Inventory(dir.resolve("root").toString(), "/", null));
            FakeBoard board = new FakeBoard();

            SerialBridge bridge = bridge(board, registry, messages, new SerialBridge.Settings(true, "auto", 115200, 0.5));
            bridge.start();
            try {
                check(waitUntil(() -> "error".equals(usbState(registry, "state"))), "no board yet is an error, not a crash");
                check(String.valueOf(usbState(registry, "last_error")).contains("No serial device found"), "the reason is reported");

                board.ports.add(new PortInfo("/dev/ttyACM0", "USB JTAG/serial debug unit"));
                check(waitUntil(() -> "connected".equals(usbState(registry, "state"))), "connects once the board appears");
                check(waitUntil(() -> "fake-1".equals(usbState(registry, "firmware"))), "the hello is recorded");
                equal("/dev/ttyACM0", usbState(registry, "port"), "port");
                check(waitUntil(() -> board.received.stream().anyMatch(f -> "host_hello".equals(f.get("type")))), "host_hello was sent");
                check(waitUntil(() -> board.received.stream().filter(f -> "inventory".equals(f.get("type"))).count() == 5), "inventory was sent");
                check(waitUntil(() -> board.received.stream().filter(f -> "stats".equals(f.get("type"))).count() >= 2), "stats keep coming");
                equal("/dev/ttyACM0@115200", board.opened.get(0), "opened with the configured baud rate");

                FakePort first = board.current;
                board.unplug();
                check(waitUntil(() -> "error".equals(usbState(registry, "state"))), "an unplug is reported");
                check(waitUntil(() -> first.closed), "the old port is closed");
                check(waitUntil(() -> board.opened.size() >= 2 && "connected".equals(usbState(registry, "state"))),
                        "reconnects by itself");
                check(board.current != first, "with a new connection");
            } finally {
                bridge.stop();
            }
            equal("stopped", usbState(registry, "state"), "state after stop");
            check(board.current.closed, "the port is closed on stop");
            config.close();
        });
        r.test("serial: the bridge does not open the port while the transport is switched off (needs sqlite-jdbc)", () -> {
            requireSqlite();
            Path dir = Files.createTempDirectory("cd-ser-off");
            ConfigService config = new ConfigService(dir.resolve("control-deck.db"));
            TransportRegistry registry = new TransportRegistry(config, dir.resolve("var"));
            FakeBoard board = new FakeBoard();
            board.ports.add(new PortInfo("/dev/ttyACM0", ""));
            SerialBridge bridge = bridge(board, registry, new MessageHandler(config, null, null),
                    new SerialBridge.Settings(false, "auto", 115200, 2.0));
            bridge.start();
            Thread.sleep(300);
            bridge.stop();
            equal(0, board.opened.size(), "nothing opened");
            config.close();
        });
        r.test("serial: the rates to try: the configured one, then the boards' own behind a USB-UART chip", () -> {
            equal(List.of(115200), SerialBridge.baudRates(115200, false), "native USB: only the configured rate");
            equal(List.of(115200, 921600), SerialBridge.baudRates(115200, true), "USB-UART: then 921600");
            equal(List.of(921600, 115200), SerialBridge.baudRates(921600, true), "no rate twice");
            check(new PortInfo("/dev/ttyUSB0", "", PortInfo.VENDOR_WCH).isUartBridge(), "a CH340 is a USB-UART chip");
            check(!new PortInfo("/dev/ttyACM0", "", PortInfo.VENDOR_ESPRESSIF).isUartBridge(), "native USB is not");
        });
        r.test("serial: a CH340 board at 921600 is found after 115200 stays silent (needs sqlite-jdbc)", () -> {
            requireSqlite();
            Path dir = Files.createTempDirectory("cd-ser-baud");
            ConfigService config = new ConfigService(dir.resolve("control-deck.db"));
            TransportRegistry registry = new TransportRegistry(config, dir.resolve("var"));
            MessageHandler messages = new MessageHandler(config, null, new Inventory(dir.resolve("root").toString(), "/", null));
            FakeBoard board = new FakeBoard();
            board.answersAtBaud = 921600;
            board.ports.add(new PortInfo("/dev/ttyUSB0", "USB Serial", PortInfo.VENDOR_WCH));
            SerialBridge bridge = bridge(board, registry, messages, new SerialBridge.Settings(true, "auto", 115200, 0.5));
            bridge.start();
            try {
                check(waitUntil(() -> "fake-1".equals(usbState(registry, "firmware"))), "the hello arrives at 921600");
                equal("/dev/ttyUSB0@115200", board.opened.get(0), "the configured rate first");
                equal("/dev/ttyUSB0@921600", board.opened.get(board.opened.size() - 1), "then the board's rate");
            } finally {
                bridge.stop();
            }
            config.close();
        });
        r.test("serial: a board that never answers is pinged again and again (needs sqlite-jdbc)", () -> {
            requireSqlite();
            Path dir = Files.createTempDirectory("cd-ser-silent");
            ConfigService config = new ConfigService(dir.resolve("control-deck.db"));
            TransportRegistry registry = new TransportRegistry(config, dir.resolve("var"));
            FakeBoard board = new FakeBoard();
            board.boardAnswers = false;
            board.ports.add(new PortInfo("/dev/ttyACM0", ""));
            SerialBridge bridge = bridge(board, registry, new MessageHandler(config, null, null),
                    new SerialBridge.Settings(true, "auto", 115200, 2.0));
            bridge.start();
            try {
                check(waitUntil(() -> "connected".equals(usbState(registry, "state"))), "the port opens");
                check(waitUntil(() -> board.received.stream().filter(f -> "ping".equals(f.get("type"))).count() >= 2),
                        "pings repeat while there is no answer");
            } finally {
                bridge.stop();
            }
            config.close();
        });
    }

    // -- what the API shows --------------------------------------------------------------

    private static void registry(Runner r) {
        r.test("serial: the USB transport state is stopped, connected, stale or disabled (needs sqlite-jdbc)", () -> {
            requireSqlite();
            Path dir = Files.createTempDirectory("cd-ser-reg");
            ConfigService config = new ConfigService(dir.resolve("control-deck.db"));
            TransportRegistry registry = new TransportRegistry(config, dir.resolve("var"));
            Map<?, ?> usb = (Map<?, ?>) registry.all().get("usb");
            equal(true, usb.get("implemented"), "the bridge exists now");
            equal("stopped", usb.get("state"), "nothing running yet");

            long now = System.currentTimeMillis() / 1000L;
            registry.update("usb", fields("state", "connected", "port", "/dev/ttyACM0", "last_activity", now,
                    "firmware", "fake-1"));
            usb = (Map<?, ?>) registry.all().get("usb");
            equal("connected", usb.get("state"), "recent traffic");
            equal("fake-1", usb.get("firmware"), "firmware");

            registry.update("usb", fields("last_activity", now - 60));
            equal("stale", ((Map<?, ?>) registry.all().get("usb")).get("state"), "no traffic for a minute");

            config.setSerialSettings(Map.of("enabled", false));
            equal("disabled", ((Map<?, ?>) registry.all().get("usb")).get("state"), "switched off in the settings");
            config.close();
        });
    }

    private static void library(Runner r) {
        r.test("serial: the jSerialComm library loads and lists ports (needs the shaded jar)", () -> {
            try {
                Class.forName("com.fazecast.jSerialComm.SerialPort");
            } catch (ClassNotFoundException e) {
                throw new Runner.Skip("jSerialComm not on the class path; run with the shaded jar");
            }
            List<PortInfo> ports = new JSerialCommProvider().listPorts();
            check(ports != null, "a list, possibly empty");
        });
    }

    // -- helpers ---------------------------------------------------------------------------

    private static Map<String, Object> fields(Object... pairs) {
        return MessageHandler.map(pairs);
    }

    private static void expectRefusal(ThrowingRunnable action, String expected) {
        try {
            action.run();
        } catch (IllegalArgumentException | IOException error) {
            check(String.valueOf(error.getMessage()).contains(expected),
                    "message should contain \"" + expected + "\" but was: " + error.getMessage());
            return;
        }
        throw new AssertionError("expected a refusal containing: " + expected);
    }

    @FunctionalInterface
    private interface ThrowingRunnable {
        void run() throws IOException;
    }
}
