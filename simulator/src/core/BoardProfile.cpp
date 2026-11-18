#include "core/BoardProfile.hpp"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "util/File.hpp"

namespace sim {
namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

const char *const kDefaultBoardId = "es3c28p";

// Lenient like the legacy scanner: a missing or wrong-typed key keeps its default.
const json *field(const json *obj, const char *key) {
    if (!obj || !obj->is_object()) return nullptr;
    auto it = obj->find(key);
    return it == obj->end() ? nullptr : &*it;
}

bool readString(const json *obj, const char *key, std::string &out) {
    const json *v = field(obj, key);
    if (!v || !v->is_string()) return false;
    out = v->get<std::string>();
    return true;
}

bool readInt(const json *obj, const char *key, int &out) {
    const json *v = field(obj, key);
    if (!v || !v->is_number_integer()) return false;
    out = v->get<int>();
    return true;
}

bool readBool(const json *obj, const char *key, bool &out) {
    const json *v = field(obj, key);
    if (!v || !v->is_boolean()) return false;
    out = v->get<bool>();
    return true;
}

// The address is an integer or a string such as "0x38"; JSON null means no I2C address.
bool readAddr(const json *obj, const char *key, int &out) {
    const json *v = field(obj, key);
    if (!v) return false;
    if (v->is_null()) {
        out = -1;
        return true;
    }
    if (v->is_number_integer()) {
        out = v->get<int>();
        return true;
    }
    if (!v->is_string()) return false;
    const std::string text = v->get<std::string>();
    char *end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 0);  // base 0: accepts 0x hex
    if (end == text.c_str() || *end != '\0') return false;
    out = static_cast<int>(parsed);
    return true;
}

// Width and height of 0 or less keep the default, as the legacy loader does.
void readPositive(const json *obj, const char *key, int &out) {
    int v = 0;
    if (readInt(obj, key, v) && v > 0) out = v;
}

void parseDisplay(const json *d, BoardProfile::Display &out) {
    readPositive(d, "width", out.width);
    readPositive(d, "height", out.height);
    int rot = 0;
    if (readInt(d, "rotation_default", rot)) out.rotationDefault = rot & 3;
    readBool(d, "mirror_x", out.mirrorX);
    readBool(d, "mirror_y", out.mirrorY);
    readString(d, "bus", out.bus);
    readString(d, "controller", out.controller);
    readString(d, "panel", out.panel);
    const json *pins = field(d, "pins");
    readInt(pins, "cs", out.cs);
    readInt(pins, "dc", out.dc);
    readInt(pins, "sclk", out.sclk);
    readInt(pins, "mosi", out.mosi);
    readInt(pins, "miso", out.miso);
    readInt(pins, "bl", out.bl);
}

void parseTouch(const json *t, BoardProfile::Touch &out) {
    readString(t, "type", out.type);
    readString(t, "controller", out.controller);
    readString(t, "bus", out.bus);
    readAddr(t, "addr", out.addr);
    const json *pins = field(t, "pins");
    readInt(pins, "scl", out.scl);
    readInt(pins, "sda", out.sda);
    readInt(pins, "int", out.intPin);
    readInt(pins, "rst", out.rstPin);
    const json *map = field(t, "mapping");
    readBool(map, "swap_xy", out.swapXy);
    readBool(map, "invert_x", out.invertX);
    readBool(map, "invert_y", out.invertY);
    const json *cal = field(t, "calibration");
    readInt(cal, "x_off", out.xOff);
    readInt(cal, "y_off", out.yOff);
}

void parseInput(const json *in, BoardProfile::Input &out) {
    readString(in, "type", out.type);
    readString(in, "keyboard_controller", out.keyboardController);
    readBool(in, "matrix_keyboard", out.matrixKeyboard);
}

// Resolves an id or path to an existing file; returns false when none exists.
bool resolvePath(const std::string &idOrPath, const std::string &boardsDir, std::string &out) {
    const fs::path direct(idOrPath);
    const fs::path inDir = fs::path(boardsDir) / idOrPath;
    const fs::path candidates[] = {direct, inDir.string() + ".json", inDir};
    for (const fs::path &c : candidates) {
        std::error_code ec;  // a bad path is "not found", not an exception
        if (!fs::is_regular_file(c, ec)) continue;
        out = c.string();
        return true;
    }
    return false;
}

void setError(std::string *err, const std::string &message) {
    if (err) *err = message;
}

}  // namespace

std::optional<BoardProfile> BoardProfile::load(const std::string &idOrPath,
                                               const std::string &boardsDir,
                                               std::string *err) {
    const std::string target = idOrPath.empty() ? kDefaultBoardId : idOrPath;
    std::string path;
    if (!resolvePath(target, boardsDir, path)) {
        setError(err, "board not found: " + target);
        return std::nullopt;
    }
    const std::optional<std::string> text = readFile(path);
    if (!text) {
        setError(err, "cannot read " + path);
        return std::nullopt;
    }
    json root;
    try {
        root = json::parse(*text);
    } catch (const json::parse_error &e) {
        setError(err, "invalid JSON in " + path + ": " + e.what());
        return std::nullopt;
    }
    if (!root.is_object()) {
        setError(err, "top level of " + path + " is not an object");
        return std::nullopt;
    }

    BoardProfile p;
    p.path = path;
    readString(&root, "id", p.id);
    readString(&root, "name", p.name);
    readString(&root, "status", p.status);
    parseDisplay(field(&root, "display"), p.display);
    parseTouch(field(&root, "touch"), p.touch);
    parseInput(field(&root, "input"), p.input);
    readString(field(&root, "cosim"), "device", p.cosim.device);
    return p;
}

std::string BoardProfile::describe() const {
    std::ostringstream os;
    os << "board id=" << id << " name='" << name << "' status=" << status << " path=" << path << '\n';
    os << "display " << display.width << 'x' << display.height << " rot=" << display.rotationDefault
       << " mirror=" << display.mirrorX << ',' << display.mirrorY << " bus=" << display.bus
       << " controller=" << display.controller << " panel=" << display.panel << '\n';
    os << "touch " << touch.type << " controller=" << touch.controller << " bus=" << touch.bus << " addr=";
    if (touch.addr < 0) {
        os << "none";
    } else {
        os << "0x" << std::hex << touch.addr << std::dec;
    }
    os << " invert=" << touch.invertX << ',' << touch.invertY << " swap=" << touch.swapXy
       << " off=" << touch.xOff << ',' << touch.yOff << '\n';
    os << "input " << input.type << " matrix_kb=" << input.matrixKeyboard << '\n';
    os << "cosim device=" << cosim.device;
    return os.str();
}

}  // namespace sim
