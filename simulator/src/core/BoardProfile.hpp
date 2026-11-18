// Board description (simulator/emu/boards/<id>.json) as a plain value.
// Defaults match emu/panel_config.h, so a board file may omit any optional key.
#pragma once

#include <optional>
#include <string>

namespace sim {

struct BoardProfile {
    struct Display {
        int width = 240;          // PANEL_WIDTH
        int height = 320;         // PANEL_HEIGHT
        int rotationDefault = 0;  // stored as rotation & 3, as the legacy host does
        bool mirrorX = true;      // PANEL_MIRROR_X
        bool mirrorY = false;     // PANEL_MIRROR_Y
        std::string bus = "spi";
        std::string controller;
        std::string panel;
        int cs = 10, dc = 46, sclk = 12, mosi = 11, miso = 13, bl = 45;  // PANEL_LCD_*
    };

    struct Touch {
        std::string type = "capacitive";
        std::string controller = "FT6336G";
        std::string bus = "i2c";
        int addr = 0x38;                                  // -1 when the file sets null
        int scl = 15, sda = 16, intPin = 17, rstPin = 18;  // PANEL_TOUCH_*
        bool swapXy = false;   // PANEL_TOUCH_SWAP_XY
        bool invertX = false;  // PANEL_TOUCH_INVERT_X (the chip reports glass coordinates, see TouchChip)
        bool invertY = false;  // PANEL_TOUCH_INVERT_Y
        int xOff = 0;
        int yOff = 0;
    };

    struct Input {
        std::string type = "touchscreen";
        std::string keyboardController;
        bool matrixKeyboard = false;
    };

    struct Cosim {
        std::string device = "es3c28p-lcd-touch";  // PANEL_COSIM_NAME
    };

    std::string path;  // file the profile was read from
    std::string id = "es3c28p";
    std::string name = "ES3C28P (KryonOS local)";
    std::string status = "supported-default";
    Display display;
    Touch touch;
    Input input;
    Cosim cosim;

    // idOrPath: a file path, or a board id looked up as <boardsDir>/<id>.json.
    // Empty id means "es3c28p". On failure returns nullopt and sets *err (if non-null).
    static std::optional<BoardProfile> load(const std::string &idOrPath,
                                            const std::string &boardsDir,
                                            std::string *err = nullptr);

    // Multi-line summary for the debug log.
    std::string describe() const;
};

}  // namespace sim
