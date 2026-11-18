// TouchChip unit tests (register file) and an end-to-end MEASUREMENT of the touch path:
// emulated chip -> firmware TFT_Touch decode/map -> BoardConfig touchPoll. The e2e part
// prints a table; it asserts only bounds, because the mismatches are what it measures.
#include <cassert>
#include <cstdio>
#include <vector>

#include "core/TouchChip.hpp"

using sim::TouchChip;
using sim::TouchMapping;

namespace {

constexpr int kPanelW = 240;
constexpr int kPanelH = 320;

std::vector<uint8_t> readRegs(TouchChip &chip, uint8_t start, size_t n) {
    const uint8_t wr[] = {start};
    chip.i2cTransaction(wr, 1, 0);
    return chip.i2cTransaction(nullptr, 0, n);
}

void testPressLayout() {
    TouchChip chip(kPanelW, kPanelH);
    chip.press(10, 20);
    // Rotation 0, default mapping: glass == screen, X = 10.
    const auto r = readRegs(chip, 0x02, 5);
    assert(r[0] == 0x01);  // count
    assert(r[1] == 0x80);  // event contact, X[11:8] = 0
    assert(r[2] == 0x0A);  // X[7:0] = 10
    assert(r[3] == 0x00);  // Y[11:8] = 0
    assert(r[4] == 0x14);  // Y[7:0] = 20
    assert(!chip.intLevel());
}

void testReleaseLayout() {
    TouchChip chip(kPanelW, kPanelH);
    chip.press(10, 20);
    chip.release();
    const auto r = readRegs(chip, 0x02, 5);
    assert(r[0] == 0x00);  // count
    assert(r[1] == 0x40);  // reg 0x03 released marker
    assert(r[2] == 0x00);  // reg 0x04 cleared
    assert(r[3] == 0x40);  // reg 0x05 released marker
    assert(r[4] == 0x00);  // reg 0x06 cleared
    assert(chip.intLevel());
}

void testPointerSemantics() {
    TouchChip chip(kPanelW, kPanelH);
    chip.press(10, 20);
    // Write pointer 0x02, read 5: regs 2..6.
    const auto first = readRegs(chip, 0x02, 5);
    assert(first.size() == 5);
    assert(first[0] == 0x01);
    // Read continues at the next register: 0xA6 -> 0x11, 0xA7 -> 0x00, 0xA8 -> 0x11.
    const uint8_t wr[] = {0xA6};
    chip.i2cTransaction(wr, 1, 0);
    assert(chip.i2cTransaction(nullptr, 0, 1)[0] == 0x11);
    assert(chip.i2cTransaction(nullptr, 0, 1)[0] == 0x00);
    assert(chip.i2cTransaction(nullptr, 0, 1)[0] == 0x11);
}

void testWriteWithData() {
    TouchChip chip(kPanelW, kPanelH);
    // Write block: pointer byte then data from that register on.
    const uint8_t wr[] = {0x10, 0xAB, 0xCD};
    chip.i2cTransaction(wr, 3, 0);
    const auto r = chip.i2cTransaction(nullptr, 0, 2);
    assert(r[0] == 0xAB);
    assert(r[1] == 0xCD);
}

void testSwapAndOffsets() {
    TouchChip chip(kPanelW, kPanelH);
    TouchMapping m;
    m.swapXy = true;
    m.invertX = false;
    m.xOff = 5;
    chip.setMapping(m);
    chip.press(10, 20);  // swapped: x=20, y=10; offset x +5 -> 25
    const auto r = readRegs(chip, 0x02, 5);
    assert(r[2] == 25);
    assert(r[4] == 10);
}

void testHwRotationRegisterIsLazy() {
    TouchChip chip(kPanelW, kPanelH);
    chip.setHwRotation(3);
    assert(readRegs(chip, 0xF1, 1)[0] == 0x00);  // not refreshed until the next event
    chip.press(1, 1);
    assert(readRegs(chip, 0xF1, 1)[0] == 0x03);
}

// --- end-to-end: firmware side, reimplemented from firmware sources ---

// TFT_Touch::readPoint(): one 5-byte read from reg 0x02. Pressed() takes two samples
// for debounce; the chip returns the same sample twice, so one read stands for both.
bool fwReadPoint(TouchChip &chip, int &rawX, int &rawY) {
    const uint8_t wr[] = {0x02};
    const auto b = chip.i2cTransaction(wr, 1, 5);
    if ((b[0] & 0x0F) == 0) return false;
    rawX = ((b[1] & 0x0F) << 8) | b[2];
    rawY = ((b[3] & 0x0F) << 8) | b[4];
    return true;
}

// TFT_Touch::_mapCoords() with setCal(0,239,0,319,240,320,axis=0) and _rotation=2.
// Map with in==out range is the identity, so the code keeps the clamps and the table only.
void fwMapCoordsRot2(int rawX, int rawY, int &sx, int &sy) {
    int nx = rawX < 0 ? 0 : (rawX > 239 ? 239 : rawX);
    int ny = rawY < 0 ? 0 : (rawY > 319 ? 319 : rawY);
    sx = 239 - nx;
    sy = 319 - ny;
    sx = sx * 239 / 239;  // scale to 240x320 resolution: identity
    sy = sy * 319 / 319;
}

// BoardConfig touchPoll(): screen rotation r, width/height of that rotation.
void fwTouchPoll(int sx, int sy, int r, int &outX, int &outY) {
    const int xp = 239 - sx;
    const int yp = 319 - sy;
    switch (r & 3) {
        case 1: outX = yp; outY = 239 - xp; break;
        case 2: outX = 239 - xp; outY = 319 - yp; break;
        case 3: outX = 319 - yp; outY = xp; break;
        default: outX = xp; outY = yp; break;
    }
    const int w = (r & 1) ? 320 : 240;
    const int h = (r & 1) ? 240 : 320;
    if (outX >= w) outX = w - 1;
    if (outY >= h) outY = h - 1;
}

void runPoint(TouchChip &chip, int r, int cx, int cy, int &ox, int &oy, int &rawX, int &rawY) {
    chip.press(cx, cy);
    int sx = 0, sy = 0;
    fwReadPoint(chip, rawX, rawY);
    fwMapCoordsRot2(rawX, rawY, sx, sy);
    fwTouchPoll(sx, sy, r, ox, oy);
    chip.release();
}

void testFirmwareTable() {
    std::printf("\nE2E touch table: clicked = screen point at hw rotation r; "
                "firmware = touchPoll() result for screen rotation r\n");
    std::printf("%-10s %-3s %-12s %-12s %-12s %s\n", "invertX", "r", "clicked", "chip raw", "firmware", "match");
    for (int invX = 1; invX >= 0; --invX) {
        TouchMapping m;
        m.invertX = invX != 0;
        for (int r = 0; r < 4; ++r) {
            const int w = (r & 1) ? 320 : 240;
            const int h = (r & 1) ? 240 : 320;
            const int pts[5][2] = {{0, 0}, {w - 1, 0}, {0, h - 1}, {w - 1, h - 1}, {w / 2, h / 2}};
            int matches = 0;
            for (const auto &p : pts) {
                TouchChip chip(kPanelW, kPanelH);
                chip.setMapping(m);
                int ox = 0, oy = 0, rx = 0, ry = 0;
                chip.setHwRotation(r);
                runPoint(chip, r, p[0], p[1], ox, oy, rx, ry);
                // Bounds only: the screen is w x h for this rotation.
                assert(ox >= 0 && ox < w && oy >= 0 && oy < h);
                const bool same = (ox == p[0] && oy == p[1]);
                if (invX == 0) assert(same);  // default mapping: the firmware must see the clicked point
                matches += same ? 1 : 0;
                std::printf("%-10s %-3d (%3d,%3d)    (%4d,%4d)    (%3d,%3d)    %s\n",
                            invX ? "true" : "false", r, p[0], p[1], rx, ry, ox, oy,
                            same ? "same" : "DIFF");
            }
            std::printf("  -> invertX=%s r=%d: %d/5 same\n", invX ? "true" : "false", r, matches);
        }
    }
}

}  // namespace

int main() {
    testPressLayout();
    testReleaseLayout();
    testPointerSemantics();
    testWriteWithData();
    testSwapAndOffsets();
    testHwRotationRegisterIsLazy();
    testFirmwareTable();
    std::printf("test_touch_chip: ok\n");
    return 0;
}
