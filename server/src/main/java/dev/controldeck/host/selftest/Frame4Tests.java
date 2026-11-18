package dev.controldeck.host.selftest;

import static dev.controldeck.host.selftest.Runner.check;
import static dev.controldeck.host.selftest.Runner.equal;

import dev.controldeck.host.protocol.Frame4;
import dev.controldeck.host.protocol.Wire;
import dev.controldeck.host.transport.serial.LineSplitter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HexFormat;
import java.util.List;

/** Protocol 4's frames: the same bytes as the firmware's deck_frame.h (shared vectors). */
final class Frame4Tests {

    private Frame4Tests() {
    }

    static void run(Runner r) {
        r.test("protocol 4: CRC, COBS and the frame give the firmware's bytes (shared vectors)", () -> {
            equal(0x29B1, Frame4.crc16("123456789".getBytes(StandardCharsets.US_ASCII), 0, 9), "CRC-16/CCITT-FALSE check");
            byte[] ab = {'A', 'B'};
            byte[] raw = Frame4.build(0x10, 0, 0x0102, ab);
            equal("9534", HexFormat.of().formatHex(Arrays.copyOfRange(raw, 10, 12)), "the shared vector's CRC");
            equal("024002100207020205414" + "2c5dd00",
                    HexFormat.of().formatHex(Frame4.encodeForLine(0x10, 0, 7, ab)), "the shared line vector");
            byte[] run = new byte[600];
            for (int i = 0; i < run.length; i++) {
                run[i] = (byte) (i % 7 == 3 ? 0 : i + 1);
            }
            byte[] enc = Frame4.cobsEncode(run);
            for (byte b : enc) {
                check(b != 0, "no 0x00 after COBS");
            }
            check(Arrays.equals(run, Frame4.cobsDecode(enc, enc.length)), "COBS round trip");
            raw[9] ^= 1;
            check(Frame4.parse(raw) == null, "a damaged byte: the CRC says so");
        });
        r.test("protocol 4: text lines and frames on one line; damage dropped, the next frame fine", () -> {
            LineSplitter splitter = new LineSplitter(4096);
            byte[] frame = Frame4.encodeForLine(0x10, 0, 7, new byte[] {'A', 'B'});
            byte[] bad = frame.clone();
            bad[9] ^= 1;
            byte[] cut = frame.clone();
            cut[3] = 0;
            List<byte[]> parts = List.of("DD:{\"type\":\"ping\"}\r\n# debug\n".getBytes(StandardCharsets.UTF_8),
                    frame, bad, frame, cut, frame);
            List<LineSplitter.Item> items = new ArrayList<>();
            for (byte[] part : parts) {
                items.addAll(splitter.feed(part, part.length));
            }
            equal(new LineSplitter.Text("DD:{\"type\":\"ping\"}"), items.get(0), "a text line");
            equal(new LineSplitter.Text("# debug"), items.get(1), "a debug line");
            long frames = items.stream().filter(i -> i instanceof LineSplitter.Frame).count();
            long damaged = items.stream().filter(i -> i instanceof LineSplitter.Damaged).count();
            equal(3L, frames, "every good frame arrives");
            check(damaged >= 2, "the bad ones are counted: " + damaged);
            LineSplitter.Frame last = (LineSplitter.Frame) items.get(items.size() - 1);
            equal(7, last.frame().seq(), "sequence");

            // the Arduino core's own log line right before a frame: a text line, the frame whole
            LineSplitter after = new LineSplitter(4096);
            byte[] core = "[  1046][E][Wire.cpp:422] beginTransmission(): could not acquire lock\r\n"
                    .getBytes(StandardCharsets.US_ASCII);
            List<LineSplitter.Item> mixed = new ArrayList<>(after.feed(core, core.length));
            mixed.addAll(after.feed(frame, frame.length));
            equal(2, mixed.size(), "a line and a frame");
            check(mixed.get(0) instanceof LineSplitter.Text t && t.line().startsWith("[  1046][E]"), "the core's line as text");
            check(mixed.get(1) instanceof LineSplitter.Frame, "the frame after it kept");
        });
        r.test("protocol 4: payload writer and reader", () -> {
            byte[] p = new Wire.Writer().u8(7).u16(0xBEEF).u32(0x01020304L).f32(1.5f).str("pc").f32((Double) null).toBytes();
            Wire.Reader reader = new Wire.Reader(p);
            equal(7, reader.u8(), "u8");
            equal(0xBEEF, reader.u16(), "u16");
            equal(0x01020304L, reader.u32(), "u32");
            equal(1.5f, reader.f32(), "f32");
            equal("pc", reader.str(), "str");
            check(Float.isNaN(reader.f32()), "a missing value is NaN");
            equal(0, reader.left(), "all read");
            try {
                reader.u8();
                check(false, "past the end throws");
            } catch (IllegalArgumentException expected) {
                check(true, "past the end throws");
            }
            String accents = "é".repeat(200);  // 400 bytes of UTF-8
            String back = new Wire.Reader(new Wire.Writer().str(accents).toBytes()).str();
            check(back.length() == 127 && accents.startsWith(back), "a long text is cut at a character");
        });
    }
}
