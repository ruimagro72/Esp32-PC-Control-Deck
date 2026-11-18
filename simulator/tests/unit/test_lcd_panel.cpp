// Unit tests for sim::LcdPanel. Expected indices are hand-computed from the
// legacy host_lcd_map_xy / host_lcd_pixels in emu/sim_host.c (240x320 panel).
#include <cstdint>
#include <cstdio>
#include <vector>

#include "core/LcdPanel.hpp"

using sim::LcdPanel;

namespace {

struct Tally {
    int failures = 0;
    void expect(bool ok, const char *what) {
        if (ok) return;
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
};

constexpr int kW = 240;
constexpr int kH = 320;

// RGB565 pixels sent MSB first, as the firmware SPI stream does.
std::vector<uint8_t> bytesOf(const std::vector<uint16_t> &px) {
    std::vector<uint8_t> out;
    for (uint16_t p : px) {
        out.push_back(static_cast<uint8_t>(p >> 8));
        out.push_back(static_cast<uint8_t>(p & 0xFF));
    }
    return out;
}

size_t countNonZero(const std::vector<uint16_t> &fb) {
    size_t n = 0;
    for (uint16_t v : fb) n += (v != 0) ? 1 : 0;
    return n;
}

// CASET/PASET style 16-bit pair: start then end.
void setWindow(LcdPanel &p, uint8_t cmd, int from, int to) {
    p.command(cmd);
    const uint8_t args[4] = {static_cast<uint8_t>(from >> 8), static_cast<uint8_t>(from & 0xFF),
                             static_cast<uint8_t>(to >> 8), static_cast<uint8_t>(to & 0xFF)};
    p.data(args, 4);
}

void testResetIsBlack(Tally &t) {
    LcdPanel p(kW, kH);
    t.expect(p.width() == kW, "reset: width");
    t.expect(p.height() == kH, "reset: height");
    std::vector<uint16_t> fb = p.snapshot();
    t.expect(fb.size() == static_cast<size_t>(kW * kH), "reset: framebuffer size");
    t.expect(countNonZero(fb) == 0, "reset: frame is black");
    t.expect(p.takeDirty(), "reset: marks dirty");
    t.expect(!p.takeDirty(), "takeDirty clears the flag");
}

void testWindow2x2(Tally &t) {
    LcdPanel p(kW, kH);
    setWindow(p, 0x2A, 10, 11);  // columns 10..11
    setWindow(p, 0x2B, 20, 21);  // rows 20..21
    p.command(0x2C);
    const std::vector<uint16_t> px = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    const std::vector<uint8_t> b = bytesOf(px);
    p.data(b.data(), b.size());

    const std::vector<uint16_t> fb = p.snapshot();
    t.expect(fb[20 * kW + 10] == 0xF800, "window: (10,20)");
    t.expect(fb[20 * kW + 11] == 0x07E0, "window: (11,20)");
    t.expect(fb[21 * kW + 10] == 0x001F, "window: (10,21) wraps to next row");
    t.expect(fb[21 * kW + 11] == 0xFFFF, "window: (11,21)");
    t.expect(countNonZero(fb) == 4, "window: nothing outside the 2x2 written");
}

void testOddByteCarriesAcrossTransfers(Tally &t) {
    LcdPanel p(kW, kH);
    p.command(0x2C);
    const uint8_t hi = 0xF8;
    const uint8_t lo = 0x00;
    const uint8_t next = 0x07;
    const uint8_t last = 0xE0;
    p.data(&hi, 1);  // half a pixel, held until the next transfer
    p.data(&lo, 1);
    p.data(&next, 1);
    p.data(&last, 1);
    const std::vector<uint16_t> fb = p.snapshot();
    t.expect(fb[0] == 0xF800, "odd byte: first pixel completed");
    t.expect(fb[1] == 0x07E0, "odd byte: second pixel completed");
}

// MADCTL / mirror cases. Each writes one pixel at the window cursor and checks the index.
void testMadctlMapping(Tally &t) {
    {
        // MX (0x40): cursor (0,0) -> x = 239.
        LcdPanel p(kW, kH);
        p.command(0x36);
        const uint8_t m = 0x40;
        p.data(&m, 1);
        p.command(0x2C);
        const uint8_t px[2] = {0xF8, 0x00};
        p.data(px, 2);
        t.expect(p.snapshot()[239] == 0xF800, "MADCTL MX: (0,0) lands at x=239");
    }
    {
        // MY (0x80): cursor (0,0) -> y = 319.
        LcdPanel p(kW, kH);
        p.command(0x36);
        const uint8_t m = 0x80;
        p.data(&m, 1);
        p.command(0x2C);
        const uint8_t px[2] = {0x07, 0xE0};
        p.data(px, 2);
        t.expect(p.snapshot()[319 * kW] == 0x07E0, "MADCTL MY: (0,0) lands at y=319");
    }
    {
        // MV|MX (0x60) with cursor (1,0): swap -> (0,1), x = 319 - 0 = 319, swap back -> (1,319).
        LcdPanel p(kW, kH);
        p.command(0x36);
        const uint8_t m = 0x60;
        p.data(&m, 1);
        setWindow(p, 0x2A, 1, 239);
        p.command(0x2C);
        const uint8_t px[2] = {0x00, 0x1F};
        p.data(px, 2);
        t.expect(p.snapshot()[319 * kW + 1] == 0x001F, "MADCTL MV|MX: (1,0) lands at (1,319)");
    }
}

void testMirrorAndRotation(Tally &t) {
    {
        // Panel mirror X on an even rotation flips x.
        LcdPanel p(kW, kH);
        p.setMirror(true, false);
        p.command(0x2C);
        const uint8_t px[2] = {0xFF, 0xFF};
        p.data(px, 2);
        t.expect(p.snapshot()[239] == 0xFFFF, "mirror X, rot 0: (0,0) lands at x=239");
    }
    {
        // Odd rotation swaps the panel and cancels the X mirror: (0,0) stays at index 0.
        LcdPanel p(kW, kH);
        p.setHwRotation(1);
        p.setMirror(true, false);
        t.expect(p.width() == kH && p.height() == kW, "rot 1: effective size swapped");
        p.command(0x2C);
        const uint8_t px[2] = {0xFF, 0xFF};
        p.data(px, 2);
        t.expect(p.snapshot()[0] == 0xFFFF, "mirror X, rot 1: (0,0) stays at index 0");
    }
}

void testOutOfWindowDoesNotCrash(Tally &t) {
    LcdPanel p(kW, kH);
    // Values above the panel clamp to 239; the window is one column wide.
    setWindow(p, 0x2A, 0x01F0, 0x0200);
    setWindow(p, 0x2B, 0x0000, 0x0200);
    p.command(0x2C);
    const std::vector<uint8_t> flood(2000, 0xAB);  // 1000 pixels, more than the window holds
    p.data(flood.data(), flood.size());
    const std::vector<uint16_t> fb = p.snapshot();
    t.expect(countNonZero(fb) == static_cast<size_t>(kH), "clamped window: whole column, wraps");

    // Inverted window (end before start): the cursor still stays on the panel.
    LcdPanel q(kW, kH);
    setWindow(q, 0x2A, 5, 2);
    setWindow(q, 0x2B, 0, 1);
    q.command(0x2C);
    const uint8_t two[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    q.data(two, 4);
    const std::vector<uint16_t> qfb = q.snapshot();
    t.expect(qfb[0 * kW + 5] == 0xFFFF && qfb[1 * kW + 5] == 0xFFFF, "inverted window: lands at xs");
}

void testSoftResetAndInvert(Tally &t) {
    LcdPanel p(kW, kH);
    p.command(0x2C);
    const uint8_t px[2] = {0x12, 0x34};
    p.data(px, 2);
    t.expect(countNonZero(p.snapshot()) == 1, "soft reset: pixel written before reset");
    p.command(0x01);
    t.expect(countNonZero(p.snapshot()) == 0, "soft reset: framebuffer cleared");
    p.command(0x21);
    t.expect(p.inverted(), "0x21 sets inverted");
    p.command(0x20);
    t.expect(!p.inverted(), "0x20 clears inverted");
}

}  // namespace

int main() {
    Tally t;
    testResetIsBlack(t);
    testWindow2x2(t);
    testOddByteCarriesAcrossTransfers(t);
    testMadctlMapping(t);
    testMirrorAndRotation(t);
    testOutOfWindowDoesNotCrash(t);
    testSoftResetAndInvert(t);
    if (t.failures) {
        std::fprintf(stderr, "test_lcd_panel: %d failure(s)\n", t.failures);
        return 1;
    }
    std::printf("test_lcd_panel: ok\n");
    return 0;
}
