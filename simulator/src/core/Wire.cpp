#include "Wire.hpp"

#include <cstring>

namespace sim {

namespace {

// Stops at 64 bits of payload like the legacy reader; a longer varint is malformed.
constexpr int kVarintMaxShift = 64;
constexpr int kVarintBits = 7;
constexpr uint8_t kVarintMask = 0x7F;
constexpr uint8_t kVarintMore = 0x80;
constexpr uint32_t kTagWireMask = 7;
constexpr uint32_t kTagShift = 3;
constexpr size_t kFixed64Size = 8;
constexpr size_t kFixed32Size = 4;

// Returns false when the buffer ends mid-varint or the varint is over-long.
bool readVarint(const uint8_t *buf, size_t len, size_t &i, uint64_t &out) {
    uint64_t v = 0;
    int shift = 0;
    while (i < len && shift < kVarintMaxShift) {
        uint8_t byte = buf[i++];
        v |= static_cast<uint64_t>(byte & kVarintMask) << shift;
        if (!(byte & kVarintMore)) {
            out = v;
            return true;
        }
        shift += kVarintBits;
    }
    return false;
}

// Reads one field body at `i` for the given wire type; false stops the scan.
bool readFieldBody(const uint8_t *buf, size_t len, size_t &i, uint32_t wire, Field &f) {
    if (wire == kWireVarint) return readVarint(buf, len, i, f.u);
    if (wire == 1 || wire == 5) {
        const size_t size = (wire == 1) ? kFixed64Size : kFixed32Size;
        if (i > len || size > len - i) return false;
        f.p = buf + i;
        f.n = size;
        i += size;
        return true;
    }
    if (wire == kWireBytes) {
        uint64_t n = 0;
        if (!readVarint(buf, len, i, n)) return false;
        if (n > static_cast<uint64_t>(len - i)) return false;
        f.p = buf + i;
        f.n = static_cast<size_t>(n);
        i += static_cast<size_t>(n);
        return true;
    }
    return false;  // groups (3, 4) and unknown types are not part of the link
}

}  // namespace

void WireWriter::varint(uint64_t v) {
    while (v > kVarintMask) {
        buf_.push_back(static_cast<uint8_t>((v & kVarintMask) | kVarintMore));
        v >>= kVarintBits;
    }
    buf_.push_back(static_cast<uint8_t>(v));
}

void WireWriter::key(uint32_t num, uint32_t wire) {
    varint((static_cast<uint64_t>(num) << kTagShift) | wire);
}

void WireWriter::varintField(uint32_t num, uint64_t v) {
    key(num, kWireVarint);
    varint(v);
}

void WireWriter::bytesField(uint32_t num, const void *data, size_t n) {
    key(num, kWireBytes);
    varint(n);
    if (n == 0) return;
    const auto *p = static_cast<const uint8_t *>(data);
    buf_.insert(buf_.end(), p, p + n);
}

void WireWriter::msgField(uint32_t num, const WireWriter &msg) {
    bytesField(num, msg.buf_.data(), msg.buf_.size());
}

WireReader::WireReader(const uint8_t *data, size_t len) : data_(data), len_(len) {}

std::optional<Field> WireReader::find(uint32_t num) const {
    std::optional<Field> found;
    size_t i = 0;
    while (i < len_) {
        uint64_t tag = 0;
        if (!readVarint(data_, len_, i, tag)) break;
        Field f;
        const auto fieldNum = static_cast<uint32_t>(tag >> kTagShift);
        f.wire = static_cast<uint32_t>(tag & kTagWireMask);
        if (!readFieldBody(data_, len_, i, f.wire, f)) break;
        if (fieldNum == num) found = f;
    }
    return found;
}

}  // namespace sim
