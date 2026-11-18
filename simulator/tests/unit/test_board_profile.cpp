// Loads the bundled board files and checks the legacy defaults and edge cases.
// Run from simulator/ (make test); argv[1] overrides the boards directory.
#include "core/BoardProfile.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

using sim::BoardProfile;
namespace fs = std::filesystem;

namespace {

void testBundledBoardsLoad(const std::string &dir) {
    const char *ids[] = {"es3c28p", "esp32-s3-devkitc-1-n16r8", "lilygo-t-deck", "lilygo-t-hmi",
                         "m5stack-cardputer"};
    for (const char *id : ids) {
        std::string err;
        const auto p = BoardProfile::load(id, dir, &err);
        assert(p && "bundled board must load");
        assert(p->id == id);
        assert(p->display.width > 0 && p->display.height > 0);
    }
}

void testEs3c28pValues(const std::string &dir) {
    const auto p = BoardProfile::load("es3c28p", dir);
    assert(p);
    assert(p->display.width == 240);
    assert(p->display.height == 320);
    assert(p->touch.addr == 0x38);
    assert(p->touch.invertX == false);
    assert(p->touch.swapXy == false);
    assert(p->display.rotationDefault == 0);
    assert(p->name == "ES3C28P (KryonOS local)");
}

void testHexAddrAndNullAddr(const std::string &dir) {
    const auto deck = BoardProfile::load("lilygo-t-deck", dir);
    assert(deck && deck->touch.addr == 0x5D);
    const auto cardputer = BoardProfile::load("m5stack-cardputer", dir);
    assert(cardputer && cardputer->touch.addr == -1);
}

void testUnknownIdFails(const std::string &dir) {
    std::string err;
    const auto p = BoardProfile::load("no-such-board", dir, &err);
    assert(!p);
    assert(!err.empty());
}

void testMalformedJsonFails() {
    const fs::path file = fs::temp_directory_path() / "board_profile_malformed.json";
    {
        std::ofstream out(file);
        out << "{ \"id\": \"broken\", \"display\": { \"width\": 240 ";  // truncated on purpose
    }
    std::string err;
    const auto p = BoardProfile::load(file.string(), "", &err);
    std::error_code ec;
    fs::remove(file, ec);
    assert(!p);
    assert(err.find("invalid JSON") != std::string::npos);
}

void testMissingKeysKeepDefaults() {
    const fs::path file = fs::temp_directory_path() / "board_profile_minimal.json";
    {
        std::ofstream out(file);
        out << R"({"id": "minimal", "display": {"width": "wide", "height": 0}})";
    }
    const auto p = BoardProfile::load(file.string(), "");
    std::error_code ec;
    fs::remove(file, ec);
    assert(p && p->id == "minimal");
    assert(p->display.width == 240);   // wrong type ignored
    assert(p->display.height == 320);  // zero ignored, as in the legacy loader
    assert(p->touch.addr == 0x38);
    assert(p->touch.invertX == false);
}

}  // namespace

int main(int argc, char **argv) {
    const std::string dir = argc > 1 ? argv[1] : "emu/boards";
    testBundledBoardsLoad(dir);
    testEs3c28pValues(dir);
    testHexAddrAndNullAddr(dir);
    testUnknownIdFails(dir);
    testMalformedJsonFails();
    testMissingKeysKeepDefaults();
    std::puts("board_profile: ok");
    return 0;
}
