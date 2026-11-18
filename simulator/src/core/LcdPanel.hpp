// Emulated ILI9341 panel: a byte-driven model of the SPI display.
// Ports the LCD half of emu/sim_host.c (command decode, window, pixel
// cursor, MADCTL and mirror mapping). No SDL, sockets or threads here.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace sim {

class LcdPanel {
public:
    // Largest side the framebuffer supports (legacy LCD_MAX_W / LCD_MAX_H).
    static constexpr int kMaxSide = 480;

    // Native size, clamped to [1, kMaxSide] like the legacy host does.
    LcdPanel(int w, int h);
    LcdPanel(const LcdPanel &) = delete;
    LcdPanel &operator=(const LcdPanel &) = delete;

    // Clears the framebuffer and the command state. Keeps mirror and rotation.
    void reset();
    void setMirror(bool x, bool y);
    // Only the low two bits count, as in the legacy code.
    void setHwRotation(int r);

    // Command byte (DC low). Argument bytes of the previous command are flushed first.
    void command(uint8_t c);
    // Data bytes (DC high) that follow a command, in SPI order.
    void data(const uint8_t *p, size_t n);
    // One SPI transfer: dc == false sends every byte as a command, dc == true as data.
    void spiTransfer(const uint8_t *p, size_t n, bool dc);

    // Effective size after hardware rotation (swapped on odd rotations).
    int width() const;
    int height() const;
    bool inverted() const;

    // Copy of the RGB565 framebuffer, taken under the lock for the renderer.
    std::vector<uint16_t> snapshot() const;
    // Returns the dirty flag and clears it, like the legacy renderer did.
    bool takeDirty();
    // Number of times the framebuffer was marked dirty. Not in the legacy code.
    uint64_t frameCounter() const;
    // Unlocked view for single-threaded use only (tests). Renderers use snapshot().
    const std::vector<uint16_t> &framebuffer() const;

private:
    int panelW() const;
    int panelH() const;
    void markDirty();
    // Shared by reset() and the software reset command (0x01).
    void clearState();
    void resetLocked();
    void commandLocked(uint8_t c);
    void dataLocked(const uint8_t *p, size_t n);
    void appendArgs(const uint8_t *p, size_t n);
    void flushArgs();
    void setColumns();
    void setRows();
    void consumeArgs(size_t n);
    uint16_t argBe16(size_t at) const;
    void pixels(const uint8_t *p, size_t n);
    void putPixel(uint16_t v);
    void advanceCursor();
    // False when the window coordinate maps outside the panel.
    bool mapPixel(int inx, int iny, int &outx, int &outy) const;

    int nativeW_;
    int nativeH_;
    int hwRot_ = 0;
    bool mirrorX_ = false;
    bool mirrorY_ = false;

    std::vector<uint16_t> fb_;
    bool dirty_ = false;
    uint64_t frames_ = 0;

    uint8_t cmd_ = 0;
    std::array<uint8_t, 16> args_{};
    size_t argsLen_ = 0;
    int xs_ = 0;
    int xe_ = 0;
    int ys_ = 0;
    int ye_ = 0;
    int cx_ = 0;
    int cy_ = 0;
    uint8_t madctl_ = 0;
    bool inverted_ = false;
    // A pixel can be split across two SPI transfers; this holds the first byte.
    uint8_t oddByte_ = 0;
    bool haveOdd_ = false;

    mutable std::mutex mu_;
};

}  // namespace sim
