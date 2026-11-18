#include "core/TouchChip.hpp"

#include <algorithm>
#include <utility>

namespace sim {

namespace {

// Identity/ID bytes the firmware driver reads back from the chip. Values as legacy.
constexpr uint8_t kRegA3 = 0x00;
constexpr uint8_t kRegA6 = 0x11;
constexpr uint8_t kRegA8 = 0x11;
constexpr uint8_t kRegAf = 0x00;
constexpr uint8_t kRegHwRot = 0xF1;

// Touch event bits in reg 0x03/0x05: 0b10 = contact, 0b00 = press down.
constexpr uint8_t kEventContact = 0x02;
constexpr uint8_t kReleasedEvent = 0x40;

int clampInt(int v, int lo, int hi) {
    return std::max(lo, std::min(v, hi));
}

}  // namespace

TouchChip::TouchChip(int panelW, int panelH) : nativeW_(panelW), nativeH_(panelH) {
    updateRegs();
}

int TouchChip::panelW() const {
    return (hwRot_ % 2 == 0) ? nativeW_ : nativeH_;
}

int TouchChip::panelH() const {
    return (hwRot_ % 2 == 0) ? nativeH_ : nativeW_;
}

void TouchChip::setMapping(const TouchMapping &mapping) {
    std::lock_guard<std::mutex> lock(mu_);
    map_ = mapping;
}

void TouchChip::setHwRotation(int rot) {
    std::lock_guard<std::mutex> lock(mu_);
    hwRot_ = rot & 3;
}

void TouchChip::press(int screenX, int screenY) {
    std::lock_guard<std::mutex> lock(mu_);
    applyLocked(true, screenX, screenY);
}

void TouchChip::release() {
    std::lock_guard<std::mutex> lock(mu_);
    applyLocked(false, 0, 0);
}

bool TouchChip::intLevel() const {
    std::lock_guard<std::mutex> lock(mu_);
    return !down_;
}

// Screen point (rotated frame) -> glass point (portrait, rotation 0). This is the firmware's
// rotation switch in touchPoll() run backwards: the chip itself never knows the rotation.
void TouchChip::screenToGlass(int sx, int sy, int &gx, int &gy) const {
    switch (hwRot_) {
        case 1:  gx = nativeW_ - 1 - sy; gy = sx; break;
        case 2:  gx = nativeW_ - 1 - sx; gy = nativeH_ - 1 - sy; break;
        case 3:  gx = sy; gy = nativeH_ - 1 - sx; break;
        default: gx = sx; gy = sy; break;
    }
}

void TouchChip::applyLocked(bool down, int sx, int sy) {
    sx = clampInt(sx, 0, panelW() - 1);
    sy = clampInt(sy, 0, panelH() - 1);
    int gx = 0;
    int gy = 0;
    screenToGlass(sx, sy, gx, gy);
    if (map_.swapXy) std::swap(gx, gy);
    if (map_.invertX) gx = nativeW_ - 1 - gx;
    if (map_.invertY) gy = nativeH_ - 1 - gy;
    down_ = down;
    x_ = clampInt(gx + map_.xOff, 0, nativeW_ - 1);
    y_ = clampInt(gy + map_.yOff, 0, nativeH_ - 1);
    updateRegs();
}

// Whole register file is rebuilt on every event, so host writes to it are lost (legacy).
void TouchChip::updateRegs() {
    regs_.fill(0);
    regs_[0xA3] = kRegA3;
    regs_[0xA6] = kRegA6;
    regs_[0xA8] = kRegA8;
    regs_[0xAF] = kRegAf;
    regs_[kRegHwRot] = static_cast<uint8_t>(hwRot_ & 3);
    if (down_) {
        regs_[0x02] = 1;
        regs_[0x03] = static_cast<uint8_t>((kEventContact << 6) | ((x_ >> 8) & 0x0F));
        regs_[0x04] = static_cast<uint8_t>(x_ & 0xFF);
        regs_[0x05] = static_cast<uint8_t>((0x00 << 4) | ((y_ >> 8) & 0x0F));
        regs_[0x06] = static_cast<uint8_t>(y_ & 0xFF);
        return;
    }
    regs_[0x02] = 0;
    regs_[0x03] = kReleasedEvent;
    regs_[0x05] = kReleasedEvent;
}

std::vector<uint8_t> TouchChip::i2cTransaction(const uint8_t *wr, size_t wrLen, size_t rdLen) {
    std::lock_guard<std::mutex> lock(mu_);
    if (wr && wrLen > 0) {
        regPtr_ = wr[0];
        for (size_t i = 1; i < wrLen; ++i) {
            regs_[static_cast<uint8_t>(regPtr_ + static_cast<uint8_t>(i - 1))] = wr[i];
        }
    }
    std::vector<uint8_t> out(rdLen);
    for (size_t i = 0; i < rdLen; ++i) {
        out[i] = regs_[static_cast<uint8_t>(regPtr_ + static_cast<uint8_t>(i))];
    }
    regPtr_ = static_cast<uint8_t>(regPtr_ + static_cast<uint8_t>(rdLen));
    return out;
}

}  // namespace sim
