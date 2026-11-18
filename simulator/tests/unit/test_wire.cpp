// Unit test for the protobuf wire codec. Plain checks, nonzero exit on failure.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/Wire.hpp"

namespace {

int failures = 0;

void expect(bool ok, const char *what, int line) {
    if (ok) return;
    std::fprintf(stderr, "FAIL test_wire.cpp:%d: %s\n", line, what);
    ++failures;
}

#define EXPECT(cond) expect((cond), #cond, __LINE__)

// Writes one varint field and reads it back through the reader.
uint64_t roundTrip(uint64_t v) {
    sim::WireWriter w;
    w.varintField(1, v);
    const auto &b = w.bytes();
    sim::WireReader r(b.data(), b.size());
    auto f = r.find(1);
    return (f && f->wire == sim::kWireVarint) ? f->u : ~uint64_t{0};
}

void testVarintRoundTrip() {
    const uint64_t values[] = {0, 1, 127, 128, 300, uint64_t{1} << 32, uint64_t{1} << 63};
    for (uint64_t v : values) EXPECT(roundTrip(v) == v);
}

void testVarintBitLayout() {
    // Known protobuf bytes: 300 -> AC 02, 128 -> 80 01; key for field 1 varint -> 08.
    sim::WireWriter w;
    w.varint(300);
    EXPECT((w.bytes() == std::vector<uint8_t>{0xAC, 0x02}));
    sim::WireWriter k;
    k.varintField(1, 0);
    EXPECT((k.bytes() == std::vector<uint8_t>{0x08, 0x00}));
}

void testNestedMessage() {
    sim::WireWriter inner;
    inner.varintField(1, 42);
    inner.bytesField(2, "abc", 3);

    sim::WireWriter outer;
    outer.varintField(1, 7);
    outer.msgField(3, inner);

    const auto &b = outer.bytes();
    sim::WireReader r(b.data(), b.size());
    auto m = r.find(3);
    EXPECT(m.has_value());
    EXPECT(m && m->wire == sim::kWireBytes);
    if (!m) return;

    sim::WireReader ir(m->p, m->n);
    auto a = ir.find(1);
    EXPECT(a && a->u == 42);
    auto s = ir.find(2);
    EXPECT(s && s->n == 3 && std::memcmp(s->p, "abc", 3) == 0);
    auto top = r.find(1);
    EXPECT(top && top->u == 7);
}

void testMissingFieldIsNullopt() {
    sim::WireWriter w;
    w.varintField(1, 5);
    const auto &b = w.bytes();
    sim::WireReader r(b.data(), b.size());
    EXPECT(!r.find(2).has_value());
    sim::WireReader empty(nullptr, 0);
    EXPECT(!empty.find(1).has_value());
}

void testLastOccurrenceWins() {
    const std::vector<uint8_t> b = {0x08, 0x01, 0x08, 0x02};
    sim::WireReader r(b.data(), b.size());
    auto f = r.find(1);
    EXPECT(f && f->u == 2);
}

void testTruncatedBuffers() {
    // Tag with no value: nothing found.
    const std::vector<uint8_t> noValue = {0x08};
    EXPECT(!sim::WireReader(noValue.data(), noValue.size()).find(1).has_value());

    // Complete field 1 followed by a cut field 2: field 1 still returned.
    const std::vector<uint8_t> partial = {0x08, 0x05, 0x10};
    sim::WireReader pr(partial.data(), partial.size());
    auto a = pr.find(1);
    EXPECT(a && a->u == 5);
    EXPECT(!pr.find(2).has_value());

    // Length prefix larger than the rest of the buffer.
    const std::vector<uint8_t> badLen = {0x12, 0x05, 'a'};
    EXPECT(!sim::WireReader(badLen.data(), badLen.size()).find(2).has_value());

    // Varint that never terminates (11 continuation bytes).
    std::vector<uint8_t> endless(11, 0x80);
    endless.insert(endless.begin(), 0x08);
    EXPECT(!sim::WireReader(endless.data(), endless.size()).find(1).has_value());
}

}  // namespace

int main() {
    testVarintRoundTrip();
    testVarintBitLayout();
    testNestedMessage();
    testMissingFieldIsNullopt();
    testLastOccurrenceWins();
    testTruncatedBuffers();
    if (failures) {
        std::fprintf(stderr, "test_wire: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_wire: ok\n");
    return 0;
}
