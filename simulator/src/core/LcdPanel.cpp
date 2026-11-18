#include "LcdPanel.hpp"

#include <algorithm>
#include <utility>

namespace sim {

namespace {

constexpr uint8_t kSwReset = 0x01;
constexpr uint8_t kInvertOff = 0x20;
constexpr uint8_t kInvertOn = 0x21;
constexpr uint8_t kColumnAddr = 0x2A;
constexpr uint8_t kRowAddr = 0x2B;
constexpr uint8_t kMemWrite = 0x2C;
constexpr uint8_t kMemWriteContinue = 0x3C;
constexpr uint8_t kMadctl = 0x36;
constexpr uint8_t kPixelFormat = 0x3A;

constexpr uint8_t kMadMx = 0x40;
constexpr uint8_t kMadMy = 0x80;
constexpr uint8_t kMadMv = 0x20;

constexpr size_t kArgsPair = 4;  // CASET and PASET: start and end, 16 bits each.

int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int clampSide(int v) { return clampi(v, 1, LcdPanel::kMaxSide); }

}  // namespace

// Public methods lock mu_ once; the helpers below assume the lock is held.

LcdPanel::LcdPanel(int w, int h)
    : nativeW_(clampSide(w)),
      nativeH_(clampSide(h)),
      fb_(static_cast<size_t>(nativeW_) * static_cast<size_t>(nativeH_), 0) {
    resetLocked();
}

void LcdPanel::reset() {
    std::lock_guard<std::mutex> lock(mu_);
    resetLocked();
}

void LcdPanel::setMirror(bool x, bool y) {
    std::lock_guard<std::mutex> lock(mu_);
    mirrorX_ = x;
    mirrorY_ = y;
}

void LcdPanel::setHwRotation(int r) {
    std::lock_guard<std::mutex> lock(mu_);
    hwRot_ = r & 3;
}

void LcdPanel::command(uint8_t c) {
    std::lock_guard<std::mutex> lock(mu_);
    commandLocked(c);
}

void LcdPanel::data(const uint8_t *p, size_t n) {
    std::lock_guard<std::mutex> lock(mu_);
    dataLocked(p, n);
}

void LcdPanel::spiTransfer(const uint8_t *p, size_t n, bool dc) {
    std::lock_guard<std::mutex> lock(mu_);
    if (dc) {
        dataLocked(p, n);
        return;
    }
    for (size_t i = 0; i < n; i++) commandLocked(p[i]);
}

int LcdPanel::width() const {
    std::lock_guard<std::mutex> lock(mu_);
    return panelW();
}

int LcdPanel::height() const {
    std::lock_guard<std::mutex> lock(mu_);
    return panelH();
}

bool LcdPanel::inverted() const {
    std::lock_guard<std::mutex> lock(mu_);
    return inverted_;
}

std::vector<uint16_t> LcdPanel::snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    return fb_;
}

bool LcdPanel::takeDirty() {
    std::lock_guard<std::mutex> lock(mu_);
    bool was = dirty_;
    dirty_ = false;
    return was;
}

uint64_t LcdPanel::frameCounter() const {
    std::lock_guard<std::mutex> lock(mu_);
    return frames_;
}

const std::vector<uint16_t> &LcdPanel::framebuffer() const { return fb_; }

int LcdPanel::panelW() const { return (hwRot_ % 2 == 0) ? nativeW_ : nativeH_; }

int LcdPanel::panelH() const { return (hwRot_ % 2 == 0) ? nativeH_ : nativeW_; }

void LcdPanel::markDirty() {
    dirty_ = true;
    frames_++;
}

void LcdPanel::clearState() {
    std::fill(fb_.begin(), fb_.end(), 0);
    xs_ = 0;
    xe_ = panelW() - 1;
    ys_ = 0;
    ye_ = panelH() - 1;
    cx_ = 0;
    cy_ = 0;
    madctl_ = 0;
    inverted_ = false;
    markDirty();
}

void LcdPanel::resetLocked() {
    clearState();
    cmd_ = 0;
    argsLen_ = 0;
    oddByte_ = 0;
    haveOdd_ = false;
}

void LcdPanel::commandLocked(uint8_t c) {
    flushArgs();
    cmd_ = c;
    argsLen_ = 0;
    haveOdd_ = false;  // a command ends any half-written pixel
    switch (c) {
        case kSwReset:
            clearState();
            break;
        case kInvertOn:
            inverted_ = true;
            markDirty();
            break;
        case kInvertOff:
            inverted_ = false;
            markDirty();
            break;
        case kMemWrite:
        case kMemWriteContinue:
            cx_ = xs_;
            cy_ = ys_;
            break;
        default:
            break;
    }
}

void LcdPanel::dataLocked(const uint8_t *p, size_t n) {
    if (n == 0) return;
    if (cmd_ == kMemWrite || cmd_ == kMemWriteContinue) {
        pixels(p, n);
        return;
    }
        appendArgs(p, n);
    flushArgs();
}

void LcdPanel::appendArgs(const uint8_t *p, size_t n) {
    const size_t room = args_.size() - argsLen_;
    const size_t copy = std::min(n, room);
    std::copy_n(p, copy, args_.begin() + static_cast<std::ptrdiff_t>(argsLen_));
    argsLen_ += copy;
}

// Consumes at most one argument group per call, as the legacy code did.
void LcdPanel::flushArgs() {
    if (cmd_ == kColumnAddr && argsLen_ >= kArgsPair) {
        setColumns();
        consumeArgs(kArgsPair);
        return;
    }
    if (cmd_ == kRowAddr && argsLen_ >= kArgsPair) {
        setRows();
        consumeArgs(kArgsPair);
        return;
    }
    if (cmd_ == kMadctl && argsLen_ >= 1) {
        madctl_ = args_[0];
        consumeArgs(1);
        return;
    }
    if (cmd_ == kPixelFormat && argsLen_ >= 1) consumeArgs(1);  // colour format is fixed at RGB565
}

void LcdPanel::setColumns() {
    const int pw = panelW();
    xs_ = clampi(argBe16(0), 0, pw - 1);
    xe_ = clampi(argBe16(2), 0, pw - 1);
    cx_ = xs_;
}

void LcdPanel::setRows() {
    const int ph = panelH();
    ys_ = clampi(argBe16(0), 0, ph - 1);
    ye_ = clampi(argBe16(2), 0, ph - 1);
    cy_ = ys_;
}

void LcdPanel::consumeArgs(size_t n) {
    std::copy(args_.begin() + static_cast<std::ptrdiff_t>(n),
              args_.begin() + static_cast<std::ptrdiff_t>(argsLen_), args_.begin());
    argsLen_ -= n;
}

uint16_t LcdPanel::argBe16(size_t at) const {
    return static_cast<uint16_t>((args_[at] << 8) | args_[at + 1]);
}

// Pixels are RGB565, most significant byte first, and may split across transfers.
void LcdPanel::pixels(const uint8_t *p, size_t n) {
    std::vector<uint8_t> bytes;
    bytes.reserve(n + 1);
    if (haveOdd_) {
        bytes.push_back(oddByte_);
        haveOdd_ = false;
    }
    bytes.insert(bytes.end(), p, p + n);

    const size_t pairs = bytes.size() & ~static_cast<size_t>(1);
    for (size_t i = 0; i < pairs; i += 2) {
        putPixel(static_cast<uint16_t>((bytes[i] << 8) | bytes[i + 1]));
    }
    if (bytes.size() & 1) {
        oddByte_ = bytes.back();
        haveOdd_ = true;
    }
}

void LcdPanel::putPixel(uint16_t v) {
    int px = 0;
    int py = 0;
    if (mapPixel(cx_, cy_, px, py)) {
        fb_[static_cast<size_t>(py) * static_cast<size_t>(panelW()) + static_cast<size_t>(px)] = v;
        markDirty();
    }
    advanceCursor();
}

void LcdPanel::advanceCursor() {
    cx_++;
    if (cx_ <= xe_) return;
    cx_ = xs_;
    cy_++;
    if (cy_ > ye_) cy_ = ys_;
}

// Same order as the legacy host_lcd_map_xy: MADCTL transform, then panel mirror.
bool LcdPanel::mapPixel(int inx, int iny, int &outx, int &outy) const {
    int x = inx;
    int y = iny;
    int w = panelW();
    int h = panelH();
    const bool mv = (madctl_ & kMadMv) != 0;
    if (mv) {
        std::swap(x, y);
        std::swap(w, h);
    }
    if ((madctl_ & kMadMx) != 0) x = (w - 1) - x;
    if ((madctl_ & kMadMy) != 0) y = (h - 1) - y;
    if (mv) std::swap(x, y);

    const int pw = panelW();
    const int ph = panelH();
    if (x < 0 || x >= pw || y < 0 || y >= ph) return false;

        const bool flipX = mirrorX_ != (hwRot_ % 2 == 1);
    if (flipX) x = (pw - 1) - x;
    if (mirrorY_) y = (ph - 1) - y;
    outx = x;
    outy = y;
    return true;
}

}  // namespace sim
