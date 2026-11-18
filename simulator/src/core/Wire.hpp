// Protobuf wire codec for the cosim link (varint and length-delimited fields only).
// Same bit layout as the legacy C helpers in emu/sim_host.c; the wire numbers are kept.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace sim {

// Wire types used by the link: 0 = varint, 2 = length-delimited.
// 1 (fixed64) and 5 (fixed32) are read but never written.
constexpr uint32_t kWireVarint = 0;
constexpr uint32_t kWireBytes = 2;

// One decoded field. For varints `u` holds the value; for the others `p`/`n`
// point into the buffer the reader was built on (no copy, no ownership).
struct Field {
    uint32_t wire = 0;
    uint64_t u = 0;
    const uint8_t *p = nullptr;
    size_t n = 0;
};

class WireWriter {
public:
    void varint(uint64_t v);
    void varintField(uint32_t num, uint64_t v);
    void bytesField(uint32_t num, const void *data, size_t n);
    void msgField(uint32_t num, const WireWriter &msg);

    const std::vector<uint8_t> &bytes() const { return buf_; }

private:
    void key(uint32_t num, uint32_t wire);

    std::vector<uint8_t> buf_;
};

class WireReader {
public:
    // The buffer must outlive the reader and every Field it returns.
    WireReader(const uint8_t *data, size_t len);

    // Last occurrence of `num` before the first malformed field, or nullopt.
    // A truncated buffer therefore still yields the fields that were complete.
    std::optional<Field> find(uint32_t num) const;

private:
    const uint8_t *data_;
    size_t len_;
};

}  // namespace sim
