// Emulated FT6336G capacitive touch controller: an I2C register file (address 0x38).
// Pure model, no SDL and no sockets. The firmware reads it exactly as it reads the real chip.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace sim {

// Board-specific correction applied to the glass coordinates (the chip's own frame).
// All off by default: the real FT6336G reports glass coordinates and the firmware rotates them.
struct TouchMapping {
    bool swapXy = false;
    bool invertX = false;
    bool invertY = false;
    int xOff = 0;
    int yOff = 0;
};

class TouchChip {
public:
    // Native panel size in pixels (portrait). Hardware rotation swaps the two sides.
    TouchChip(int panelW, int panelH);

    void setMapping(const TouchMapping &mapping);
    // Only the value of register 0xF1 follows this on the next press/release, as in legacy.
    void setHwRotation(int rot);

    // Coordinates are on the screen as currently rotated (hw rotation); they are turned into
    // glass coordinates here, the inverse of what the firmware does. Out-of-range values clamp.
    void press(int screenX, int screenY);
    void release();

    // INT line level: false while a touch is active, true otherwise.
    bool intLevel() const;

    // Write part sets the register pointer (first byte) and writes the rest from it.
    // Read part returns rdLen bytes and advances the pointer by rdLen (8-bit wrap).
    std::vector<uint8_t> i2cTransaction(const uint8_t *wr, size_t wrLen, size_t rdLen);

private:
    int panelW() const;
    int panelH() const;
    void applyLocked(bool down, int sx, int sy);
    void screenToGlass(int sx, int sy, int &gx, int &gy) const;
    void updateRegs();

    mutable std::mutex mu_;
    int nativeW_;
    int nativeH_;
    int hwRot_ = 0;
    TouchMapping map_;
    std::array<uint8_t, 256> regs_{};
    uint8_t regPtr_ = 0;
    bool down_ = false;
    int x_ = 0;
    int y_ = 0;
};

}  // namespace sim
