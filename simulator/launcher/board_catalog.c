#include "board_catalog.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void sbc_copy(char *dst, size_t dstsz, const char *src) {
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    snprintf(dst, dstsz, "%.*s", (int)(dstsz - 1), src);
}

static int sbc_streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}

static void sbc_set_defaults_common(sim_board_t *b) {
    memset(b, 0, sizeof(*b));
    b->flash_mb = 16;
    b->psram_mb = 8;
    sbc_copy(b->mcu, sizeof(b->mcu), "esp32s3");
}

static void sbc_init_es3c28p(sim_board_t *b) {
    sbc_set_defaults_common(b);
    sbc_copy(b->id, sizeof(b->id), "es3c28p");
    sbc_copy(b->name, sizeof(b->name), "ES3C28P (KryonDeck local emu target)");
    b->flash_mb = 16; b->psram_mb = 8;
    b->display.width = 240; b->display.height = 320;
    sbc_copy(b->display.controller, sizeof(b->display.controller), "ILI9341");
    sbc_copy(b->display.bus, sizeof(b->display.bus), "spi");
    sbc_copy(b->display.interface, sizeof(b->display.interface), "SPI (4-wire)");
    sbc_copy(b->display.notes, sizeof(b->display.notes), "Live display via external cosim host; needs-runtime-host in some routing cases.");
    sbc_copy(b->touch_input.type, sizeof(b->touch_input.type), "touch");
    sbc_copy(b->touch_input.controller, sizeof(b->touch_input.controller), "FT6336G");
    sbc_copy(b->touch_input.bus, sizeof(b->touch_input.bus), "i2c");
    sbc_copy(b->touch_input.i2c_addr, sizeof(b->touch_input.i2c_addr), "0x38");
    sbc_copy(b->touch_input.notes, sizeof(b->touch_input.notes), "Regfile/init-poll supported with board-es3c28p.json; full interaction needs-runtime-host.");
    sbc_copy(b->audio.codec, sizeof(b->audio.codec), "ES8311");
    sbc_copy(b->audio.codec_i2c_addr, sizeof(b->audio.codec_i2c_addr), "0x18");
    sbc_copy(b->audio.bus, sizeof(b->audio.bus), "i2s+i2c");
    sbc_copy(b->audio.notes, sizeof(b->audio.notes), "I2S WAV supported; ESP_EMU_SKIP_AUDIO for emu build.");
    sbc_copy(b->storage.flash_fs, sizeof(b->storage.flash_fs), "LittleFS/spiffs (merge may include FS @0xc90000)");
    sbc_copy(b->storage.sd, sizeof(b->storage.sd), "SDMMC 4-bit (hw)");
    sbc_copy(b->storage.sd_emulation, sizeof(b->storage.sd_emulation), "supported-now (esp-emu 1.1.0 --sd, ESP32-S3 SDMMC; verified with a card image)");
    sbc_copy(b->storage.notes, sizeof(b->storage.notes), "SDMMC via esp-emu 1.1.0 --sd; es3c28p_emu build runs the same initSD() (not gated by ESP_EMU_SKIP_AUDIO).");
    sbc_copy(b->cosim_board_json_suggested, sizeof(b->cosim_board_json_suggested), "simulator/emu/board-es3c28p.json");
    sbc_copy(b->firmware_presets.local_merged, sizeof(b->firmware_presets.local_merged), "simulator/build/merged-emu.bin");
    sbc_copy(b->firmware_presets.local_elf, sizeof(b->firmware_presets.local_elf), "firmware/.pio/build/es3c28p_emu/firmware.elf");
    b->firmware_presets.local_use_elf = true;
    sbc_copy(b->firmware_presets.local_notes, sizeof(b->firmware_presets.local_notes), "Local es3c28p_emu build.");
    b->firmware_presets.kryonos_v3_use_elf = false;
    sbc_copy(b->firmware_presets.kryonos_v3_notes, sizeof(b->firmware_presets.kryonos_v3_notes), "No KryonOS v3 release asset for es3c28p.");
    sbc_copy(b->firmware_presets.extra_id, sizeof(b->firmware_presets.extra_id), "asset_es3c28p");
    sbc_copy(b->firmware_presets.extra_merged, sizeof(b->firmware_presets.extra_merged), "simulator/emu/firmware/es3c28p/merged.bin");
    b->firmware_presets.extra_use_elf = false;
    sbc_copy(b->pins.lcd, sizeof(b->pins.lcd), "SPI CS10 DC46 BL45 SCLK12 MOSI11 MISO13");
    sbc_copy(b->pins.touch, sizeof(b->pins.touch), "I2C FT6336G 0x38 SCL15 SDA16 INT17 RST18");
    sbc_copy(b->pins.power_misc, sizeof(b->pins.power_misc), "WS2812 GPIO42");
    sbc_copy(b->pins.notes, sizeof(b->pins.notes), "panel_config.h / HARDWARE-SUPPORT.md");
    sbc_copy(b->support.serial_headless, sizeof(b->support.serial_headless), "supported-now");
    sbc_copy(b->support.display_live, sizeof(b->support.display_live), "needs-runtime-host");
    sbc_copy(b->support.touch_input, sizeof(b->support.touch_input), "supported-now");
    sbc_copy(b->support.audio, sizeof(b->support.audio), "supported-now");
    sbc_copy(b->support.sdmmc_emu, sizeof(b->support.sdmmc_emu), "supported-now");
    sbc_copy(b->support.cosim_in_process, sizeof(b->support.cosim_in_process), "supported-now");
    sbc_copy(b->support.cosim_tcp_external, sizeof(b->support.cosim_tcp_external), "supported-now");
    b->support.parallel_needs_runtime_host = false;
    sbc_copy(b->notes, sizeof(b->notes), "Primary local emu target. Host pins/res partly compiled (panel_config.h).");
}

static void sbc_init_devkit(sim_board_t *b) {
    sbc_set_defaults_common(b);
    sbc_copy(b->id, sizeof(b->id), "esp32-s3-devkitc-1-n16r8");
    sbc_copy(b->name, sizeof(b->name), "ESP32-S3-DevKitC-1 N16R8");
    b->flash_mb = 16; b->psram_mb = 8;
    b->display.width = 240; b->display.height = 320;
    sbc_copy(b->display.controller, sizeof(b->display.controller), "ILI9341");
    sbc_copy(b->display.bus, sizeof(b->display.bus), "spi");
    sbc_copy(b->display.interface, sizeof(b->display.interface), "SPI HSPI (KryonOS v3 common)");
    sbc_copy(b->display.notes, sizeof(b->display.notes), "Module-dependent; live display needs-runtime-host.");
    sbc_copy(b->touch_input.type, sizeof(b->touch_input.type), "touch");
    sbc_copy(b->touch_input.controller, sizeof(b->touch_input.controller), "XPT2046");
    sbc_copy(b->touch_input.bus, sizeof(b->touch_input.bus), "spi");
    sbc_copy(b->touch_input.notes, sizeof(b->touch_input.notes), "XPT2046 not modeled in current host (needs-runtime-host).");
    sbc_copy(b->audio.bus, sizeof(b->audio.bus), "i2s");
    sbc_copy(b->audio.notes, sizeof(b->audio.notes), "Conditional; dedicated pins not specified in KryonOS v3 env extract.");
    sbc_copy(b->storage.flash_fs, sizeof(b->storage.flash_fs), "Per firmware (KryonOS v3 split offsets)");
    sbc_copy(b->storage.sd, sizeof(b->storage.sd), "SD via FS abstraction (pins vary)");
    sbc_copy(b->storage.sd_emulation, sizeof(b->storage.sd_emulation), "conditional(version/tool): esp-emu 1.1.0 --sd (ESP32-S3 SDMMC); board wiring not verified");
    sbc_copy(b->firmware_presets.kryonos_v3_merged, sizeof(b->firmware_presets.kryonos_v3_merged),
             "simulator/emu/firmware/kryonos-v3/esp32-s3-devkitc-1-n16r8/merged.bin");
    b->firmware_presets.kryonos_v3_use_elf = false;
    sbc_copy(b->firmware_presets.kryonos_v3_notes, sizeof(b->firmware_presets.kryonos_v3_notes), "KryonOS v3 release, no ELF.");
    sbc_copy(b->pins.lcd, sizeof(b->pins.lcd), "KryonOS v3: MOSI11 MISO13 SCLK12 CS10 DC9 RST8 BL21");
    sbc_copy(b->pins.touch, sizeof(b->pins.touch), "KryonOS v3 XPT2046: CS7 CLK4 DIN5 DO6 IRQ14");
    sbc_copy(b->support.serial_headless, sizeof(b->support.serial_headless), "conditional");
    sbc_copy(b->support.display_live, sizeof(b->support.display_live), "needs-runtime-host");
    sbc_copy(b->support.touch_input, sizeof(b->support.touch_input), "needs-runtime-host");
    sbc_copy(b->support.audio, sizeof(b->support.audio), "conditional");
    sbc_copy(b->support.sdmmc_emu, sizeof(b->support.sdmmc_emu), "conditional(version/tool)");
    sbc_copy(b->support.cosim_in_process, sizeof(b->support.cosim_in_process), "needs-runtime-host");
    sbc_copy(b->support.cosim_tcp_external, sizeof(b->support.cosim_tcp_external), "conditional");
    sbc_copy(b->notes, sizeof(b->notes), "Generic DevKit; validate module specifics. Best first serial boot candidate for v3.");
}

static void sbc_init_tdeck(sim_board_t *b) {
    sbc_set_defaults_common(b);
    sbc_copy(b->id, sizeof(b->id), "lilygo-t-deck");
    sbc_copy(b->name, sizeof(b->name), "LILYGO T-Deck");
    b->flash_mb = 16; b->psram_mb = 8;
    b->display.width = 320; b->display.height = 240;
    sbc_copy(b->display.controller, sizeof(b->display.controller), "ST7789");
    sbc_copy(b->display.bus, sizeof(b->display.bus), "spi");
    sbc_copy(b->display.interface, sizeof(b->display.interface), "SPI (logical 320x240 rot1)");
    sbc_copy(b->display.notes, sizeof(b->display.notes), "Panel 240x320 native, logical 320x240; needs-runtime-host.");
    sbc_copy(b->touch_input.type, sizeof(b->touch_input.type), "touch+keyboard+trackball");
    sbc_copy(b->touch_input.controller, sizeof(b->touch_input.controller), "GT911");
    sbc_copy(b->touch_input.bus, sizeof(b->touch_input.bus), "i2c");
    sbc_copy(b->touch_input.i2c_addr, sizeof(b->touch_input.i2c_addr), "0x5D");
    sbc_copy(b->touch_input.keyboard, sizeof(b->touch_input.keyboard), "KB coprocessor I2C 0x55; trackball GPIO");
    sbc_copy(b->touch_input.notes, sizeof(b->touch_input.notes), "GT911 differs from FT6336; keyboard/trackball not modeled.");
    sbc_copy(b->audio.bus, sizeof(b->audio.bus), "i2s");
    sbc_copy(b->audio.notes, sizeof(b->audio.notes), "Speaker/mic per board caps (conditional).");
    sbc_copy(b->storage.flash_fs, sizeof(b->storage.flash_fs), "Per firmware");
    sbc_copy(b->storage.sd, sizeof(b->storage.sd), "MicroSD SPI shared bus CS39");
    sbc_copy(b->storage.sd_emulation, sizeof(b->storage.sd_emulation), "conditional(version/tool): esp-emu 1.1.0 --sd (ESP32-S3 SDMMC); board wiring not verified");
    sbc_copy(b->firmware_presets.kryonos_v3_merged, sizeof(b->firmware_presets.kryonos_v3_merged),
             "simulator/emu/firmware/kryonos-v3/lilygo-t-deck/merged.bin");
    b->firmware_presets.kryonos_v3_use_elf = false;
    sbc_copy(b->pins.lcd, sizeof(b->pins.lcd), "SPI MOSI41 MISO38 SCLK40 CS12 DC11 RST-1 BL42");
    sbc_copy(b->pins.touch, sizeof(b->pins.touch), "GT911 I2C SDA18 SCL8 INT16 @0x5D/0x14");
    sbc_copy(b->pins.input, sizeof(b->pins.input), "KB 0x55 INT46; trackball U3 D15 L1 R2 C0");
    sbc_copy(b->pins.storage, sizeof(b->pins.storage), "SD CS39 on TFT SPI bus");
    sbc_copy(b->support.serial_headless, sizeof(b->support.serial_headless), "conditional");
    sbc_copy(b->support.display_live, sizeof(b->support.display_live), "needs-runtime-host");
    sbc_copy(b->support.touch_input, sizeof(b->support.touch_input), "needs-runtime-host");
    sbc_copy(b->support.audio, sizeof(b->support.audio), "conditional");
    sbc_copy(b->support.sdmmc_emu, sizeof(b->support.sdmmc_emu), "conditional(version/tool)");
    sbc_copy(b->support.cosim_in_process, sizeof(b->support.cosim_in_process), "needs-runtime-host");
    sbc_copy(b->support.cosim_tcp_external, sizeof(b->support.cosim_tcp_external), "conditional");
    sbc_copy(b->notes, sizeof(b->notes), "GT911/keyboard/trackball/GPS models needed for full parity.");
}

static void sbc_init_thmi(sim_board_t *b) {
    sbc_set_defaults_common(b);
    sbc_copy(b->id, sizeof(b->id), "lilygo-t-hmi");
    sbc_copy(b->name, sizeof(b->name), "LILYGO T-HMI");
    b->flash_mb = 16; b->psram_mb = 8;
    b->display.width = 240; b->display.height = 320;
    sbc_copy(b->display.controller, sizeof(b->display.controller), "ST7789");
    sbc_copy(b->display.bus, sizeof(b->display.bus), "parallel");
    sbc_copy(b->display.interface, sizeof(b->display.interface), "8-bit parallel");
    sbc_copy(b->display.notes, sizeof(b->display.notes), "No parallel transport/consumer; needs-runtime-host (major gap).");
    sbc_copy(b->touch_input.type, sizeof(b->touch_input.type), "touch");
    sbc_copy(b->touch_input.controller, sizeof(b->touch_input.controller), "XPT2046");
    sbc_copy(b->touch_input.bus, sizeof(b->touch_input.bus), "spi/bitbang");
    sbc_copy(b->touch_input.notes, sizeof(b->touch_input.notes), "XPT2046 model not implemented; needs-runtime-host.");
    b->audio.uncertain = true;
    sbc_copy(b->audio.notes, sizeof(b->audio.notes), "Audio details uncertain in prep extract.");
    sbc_copy(b->storage.flash_fs, sizeof(b->storage.flash_fs), "Per firmware");
    sbc_copy(b->storage.sd, sizeof(b->storage.sd), "SD_MMC 1-bit");
    sbc_copy(b->storage.sd_emulation, sizeof(b->storage.sd_emulation), "conditional(version/tool): esp-emu 1.1.0 --sd (ESP32-S3 SDMMC); board wiring not verified");
    sbc_copy(b->firmware_presets.kryonos_v3_merged, sizeof(b->firmware_presets.kryonos_v3_merged),
             "simulator/emu/firmware/kryonos-v3/lilygo-t-hmi/merged.bin");
    b->firmware_presets.kryonos_v3_use_elf = false;
    sbc_copy(b->pins.lcd, sizeof(b->pins.lcd), "Parallel 8-bit CS6 DC7 WR8 BL38 D0-7=48,47,39,40,41,42,45,46");
    sbc_copy(b->pins.touch, sizeof(b->pins.touch), "XPT2046 SCLK1 MISO4 MOSI3 CS2 IRQ9");
    sbc_copy(b->pins.storage, sizeof(b->pins.storage), "SD_MMC CLK12 CMD11 D0_13");
    sbc_copy(b->pins.power_misc, sizeof(b->pins.power_misc), "PWR_EN10 PWR_ON14 BAT_ADC5");
    sbc_copy(b->support.serial_headless, sizeof(b->support.serial_headless), "conditional");
    sbc_copy(b->support.display_live, sizeof(b->support.display_live), "needs-runtime-host");
    sbc_copy(b->support.touch_input, sizeof(b->support.touch_input), "needs-runtime-host");
    sbc_copy(b->support.audio, sizeof(b->support.audio), "conditional");
    sbc_copy(b->support.sdmmc_emu, sizeof(b->support.sdmmc_emu), "conditional(version/tool)");
    sbc_copy(b->support.cosim_in_process, sizeof(b->support.cosim_in_process), "needs-runtime-host");
    sbc_copy(b->support.cosim_tcp_external, sizeof(b->support.cosim_tcp_external), "conditional");
    b->support.parallel_needs_runtime_host = true;
    sbc_copy(b->notes, sizeof(b->notes), "Highest visual gap (parallel LCD). Headless-first recommended.");
}

static void sbc_init_cardputer(sim_board_t *b) {
    sbc_set_defaults_common(b);
    sbc_copy(b->id, sizeof(b->id), "m5stack-cardputer");
    sbc_copy(b->name, sizeof(b->name), "M5Stack Cardputer");
    b->flash_mb = 8; b->psram_mb = 0;
    b->display.width = 240; b->display.height = 135;
    sbc_copy(b->display.controller, sizeof(b->display.controller), "ST7789V2");
    sbc_copy(b->display.bus, sizeof(b->display.bus), "spi");
    sbc_copy(b->display.interface, sizeof(b->display.interface), "SPI 240x135 landscape");
    sbc_copy(b->display.notes, sizeof(b->display.notes), "CGRAM offset 1; needs-runtime-host for host adaptation.");
    sbc_copy(b->touch_input.type, sizeof(b->touch_input.type), "keyboard");
    sbc_copy(b->touch_input.controller, sizeof(b->touch_input.controller), "none");
    sbc_copy(b->touch_input.bus, sizeof(b->touch_input.bus), "gpio-matrix");
    sbc_copy(b->touch_input.keyboard, sizeof(b->touch_input.keyboard), "56-key matrix (74HC138 + 7 inputs)");
    sbc_copy(b->touch_input.notes, sizeof(b->touch_input.notes), "No touch; keyboard matrix not modeled.");
    sbc_copy(b->audio.bus, sizeof(b->audio.bus), "i2s");
    sbc_copy(b->audio.notes, sizeof(b->audio.notes), "Speaker/mic present (conditional).");
    sbc_copy(b->storage.flash_fs, sizeof(b->storage.flash_fs), "LittleFS custom partitions_cardputer.csv");
    sbc_copy(b->storage.sd, sizeof(b->storage.sd), "SD SPI (HSPI) CS12");
    sbc_copy(b->storage.sd_emulation, sizeof(b->storage.sd_emulation), "conditional(version/tool): esp-emu 1.1.0 --sd (ESP32-S3 SDMMC); board wiring not verified");
    sbc_copy(b->firmware_presets.kryonos_v3_merged, sizeof(b->firmware_presets.kryonos_v3_merged),
             "simulator/emu/firmware/kryonos-v3/m5stack-cardputer/merged.bin");
    b->firmware_presets.kryonos_v3_use_elf = false;
    sbc_copy(b->pins.lcd, sizeof(b->pins.lcd), "SPI MOSI35 SCLK36 DC34 RST33 CS37 BL38");
    sbc_copy(b->pins.input, sizeof(b->pins.input), "74HC138 sel 8,9,11; inputs 13,15,3,4,5,6,7");
    sbc_copy(b->pins.storage, sizeof(b->pins.storage), "SD MISO39 MOSI14 SCLK40 CS12");
    sbc_copy(b->pins.power_misc, sizeof(b->pins.power_misc), "BAT_ADC10 (2x divider)");
    sbc_copy(b->support.serial_headless, sizeof(b->support.serial_headless), "conditional");
    sbc_copy(b->support.display_live, sizeof(b->support.display_live), "needs-runtime-host");
    sbc_copy(b->support.touch_input, sizeof(b->support.touch_input), "needs-runtime-host");
    sbc_copy(b->support.audio, sizeof(b->support.audio), "conditional");
    sbc_copy(b->support.sdmmc_emu, sizeof(b->support.sdmmc_emu), "conditional(version/tool)");
    sbc_copy(b->support.cosim_in_process, sizeof(b->support.cosim_in_process), "needs-runtime-host");
    sbc_copy(b->support.cosim_tcp_external, sizeof(b->support.cosim_tcp_external), "conditional");
    sbc_copy(b->notes, sizeof(b->notes), "Flash/PSRAM per stamps3 board def (verify). Primary input keyboard matrix.");
}

void sim_board_catalog_init(sim_board_catalog_t *cat) {
    if (!cat) return;
    memset(cat, 0, sizeof(*cat));
    cat->count = 5;
    sbc_init_es3c28p(&cat->boards[0]);
    sbc_init_devkit(&cat->boards[1]);
    sbc_init_tdeck(&cat->boards[2]);
    sbc_init_thmi(&cat->boards[3]);
    sbc_init_cardputer(&cat->boards[4]);
}

int sim_board_catalog_count(const sim_board_catalog_t *cat) {
    return cat ? cat->count : 0;
}

const sim_board_t *sim_board_catalog_list(const sim_board_catalog_t *cat, int *count_out) {
    if (count_out) *count_out = cat ? cat->count : 0;
    return cat ? cat->boards : NULL;
}

const sim_board_t *sim_board_catalog_get(const sim_board_catalog_t *cat, const char *id) {
    if (!cat || !id) return NULL;
    for (int i = 0; i < cat->count; i++) {
        if (sbc_streq(cat->boards[i].id, id)) return &cat->boards[i];
    }
    return NULL;
}

sim_board_t *sim_board_catalog_get_mut(sim_board_catalog_t *cat, const char *id) {
    if (!cat || !id) return NULL;
    for (int i = 0; i < cat->count; i++) {
        if (sbc_streq(cat->boards[i].id, id)) return &cat->boards[i];
    }
    return NULL;
}

/* --- Simple JSON helpers (flat key lookup in controlled pretty JSON) --- */

static char *sbc_read_file(const char *path, size_t *out_sz) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    if (out_sz) *out_sz = n;
    return buf;
}

static const char *sbc_skip_ws(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) p++;
    return p;
}

/* Find "key" : in JSON text; returns pointer after ':' or NULL. */
static const char *sbc_find_key(const char *json, const char *key) {
    if (!json || !key) return NULL;
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = json;
    while ((p = strstr(p, pat)) != NULL) {
        const char *after = p + strlen(pat);
        after = sbc_skip_ws(after);
        if (*after == ':') return sbc_skip_ws(after + 1);
        p = p + 1;
    }
    return NULL;
}

/* Find "key" inside/after a given object marker "objkey" occurrence. */
static const char *sbc_find_in_obj(const char *json, const char *objkey, const char *key) {
    const char *obj = sbc_find_key(json, objkey);
    if (!obj) return NULL;
    /* search forward from obj for key (best-effort for our fixed files) */
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = obj;
    while ((p = strstr(p, pat)) != NULL) {
        const char *after = p + strlen(pat);
        after = sbc_skip_ws(after);
        if (*after == ':') return sbc_skip_ws(after + 1);
        p = p + 1;
    }
    return NULL;
}

static int sbc_parse_string_at(const char *v, char *out, size_t outsz) {
    if (!v || !out || outsz == 0) return 0;
    if (*v == 'n') { /* null */
        out[0] = '\0';
        return 1;
    }
    if (*v != '"') return 0;
    v++;
    size_t i = 0;
    while (*v && *v != '"') {
        char c = *v++;
        if (c == '\\' && *v) {
            char e = *v++;
            if (e == 'n') c = '\n'; else if (e == 'r') c = '\r'; else if (e == 't') c = '\t'; else c = e;
        }
        if (i + 1 < outsz) out[i++] = c;
    }
    out[i] = '\0';
    return 1;
}

static int sbc_get_string(const char *json, const char *key, char *out, size_t outsz) {
    const char *v = sbc_find_key(json, key);
    if (!v) return 0;
    return sbc_parse_string_at(v, out, outsz);
}

static int sbc_get_string2(const char *json, const char *obj, const char *key, char *out, size_t outsz) {
    const char *v = sbc_find_in_obj(json, obj, key);
    if (!v) return 0;
    return sbc_parse_string_at(v, out, outsz);
}

static int sbc_get_int(const char *json, const char *key, int *out) {
    const char *v = sbc_find_key(json, key);
    if (!v) return 0;
    if (*v == 'n') return 0; /* null */
    if (*v == '-' || isdigit((unsigned char)*v)) { *out = (int)strtol(v, NULL, 10); return 1; }
    return 0;
}

static int sbc_get_int2(const char *json, const char *obj, const char *key, int *out) {
    const char *v = sbc_find_in_obj(json, obj, key);
    if (!v) return 0;
    if (*v == 'n') return 0;
    if (*v == '-' || isdigit((unsigned char)*v)) { *out = (int)strtol(v, NULL, 10); return 1; }
    return 0;
}

static int sbc_get_bool2(const char *json, const char *obj, const char *key, bool *out) {
    const char *v = sbc_find_in_obj(json, obj, key);
    if (!v) return 0;
    if (strncmp(v, "true", 4) == 0) { *out = true; return 1; }
    if (strncmp(v, "false", 5) == 0) { *out = false; return 1; }
    return 0;
}

/* Apply known fields from JSON onto board b (best-effort). */
static void sbc_apply_json(sim_board_t *b, const char *json) {
    char tmp[SBC_MAX_LONG];
    int iv;

    sbc_get_string(json, "id", b->id, sizeof(b->id));
    sbc_get_string(json, "name", b->name, sizeof(b->name));
    sbc_get_string(json, "mcu", b->mcu, sizeof(b->mcu));
    if (sbc_get_int(json, "flash_mb", &iv)) b->flash_mb = iv;
    if (sbc_get_int(json, "psram_mb", &iv)) b->psram_mb = iv;

    if (sbc_get_int2(json, "display", "width", &iv)) b->display.width = iv;
    if (sbc_get_int2(json, "display", "height", &iv)) b->display.height = iv;
    sbc_get_string2(json, "display", "controller", b->display.controller, sizeof(b->display.controller));
    sbc_get_string2(json, "display", "bus", b->display.bus, sizeof(b->display.bus));
    sbc_get_string2(json, "display", "interface", b->display.interface, sizeof(b->display.interface));
    sbc_get_string2(json, "display", "notes", b->display.notes, sizeof(b->display.notes));

    sbc_get_string2(json, "touch_input", "type", b->touch_input.type, sizeof(b->touch_input.type));
    sbc_get_string2(json, "touch_input", "controller", b->touch_input.controller, sizeof(b->touch_input.controller));
    sbc_get_string2(json, "touch_input", "bus", b->touch_input.bus, sizeof(b->touch_input.bus));
    sbc_get_string2(json, "touch_input", "i2c_addr", b->touch_input.i2c_addr, sizeof(b->touch_input.i2c_addr));
    sbc_get_string2(json, "touch_input", "keyboard", b->touch_input.keyboard, sizeof(b->touch_input.keyboard));
    sbc_get_string2(json, "touch_input", "notes", b->touch_input.notes, sizeof(b->touch_input.notes));

    sbc_get_string2(json, "audio", "codec", b->audio.codec, sizeof(b->audio.codec));
    sbc_get_string2(json, "audio", "codec_i2c_addr", b->audio.codec_i2c_addr, sizeof(b->audio.codec_i2c_addr));
    sbc_get_string2(json, "audio", "bus", b->audio.bus, sizeof(b->audio.bus));
    sbc_get_string2(json, "audio", "notes", b->audio.notes, sizeof(b->audio.notes));
    { bool bv = b->audio.uncertain; if (sbc_get_bool2(json, "audio", "uncertain", &bv)) b->audio.uncertain = bv; }

    sbc_get_string2(json, "storage", "flash_fs", b->storage.flash_fs, sizeof(b->storage.flash_fs));
    sbc_get_string2(json, "storage", "sd", b->storage.sd, sizeof(b->storage.sd));
    sbc_get_string2(json, "storage", "sd_emulation", b->storage.sd_emulation, sizeof(b->storage.sd_emulation));
    sbc_get_string2(json, "storage", "notes", b->storage.notes, sizeof(b->storage.notes));

    if (sbc_get_string(json, "cosim_board_json_suggested", tmp, sizeof(tmp))) {
        /* allow null -> empty */
        if (strcmp(tmp, "null") == 0) b->cosim_board_json_suggested[0] = '\0';
        else sbc_copy(b->cosim_board_json_suggested, sizeof(b->cosim_board_json_suggested), tmp);
    } else {
        const char *v = sbc_find_key(json, "cosim_board_json_suggested");
        if (v && *v == 'n') b->cosim_board_json_suggested[0] = '\0';
    }

    /* firmware presets (search within sub-objects by name) */
    sbc_get_string2(json, "local", "merged", b->firmware_presets.local_merged, sizeof(b->firmware_presets.local_merged));
    sbc_get_string2(json, "local", "elf", b->firmware_presets.local_elf, sizeof(b->firmware_presets.local_elf));
    { bool bv = b->firmware_presets.local_use_elf; if (sbc_get_bool2(json, "local", "use_elf_default", &bv)) b->firmware_presets.local_use_elf = bv; }
    sbc_get_string2(json, "local", "notes", b->firmware_presets.local_notes, sizeof(b->firmware_presets.local_notes));

    sbc_get_string2(json, "kryonos_v3", "merged", b->firmware_presets.kryonos_v3_merged, sizeof(b->firmware_presets.kryonos_v3_merged));
    sbc_get_string2(json, "kryonos_v3", "elf", b->firmware_presets.kryonos_v3_elf, sizeof(b->firmware_presets.kryonos_v3_elf));
    { bool bv = b->firmware_presets.kryonos_v3_use_elf; if (sbc_get_bool2(json, "kryonos_v3", "use_elf_default", &bv)) b->firmware_presets.kryonos_v3_use_elf = bv; }
    sbc_get_string2(json, "kryonos_v3", "notes", b->firmware_presets.kryonos_v3_notes, sizeof(b->firmware_presets.kryonos_v3_notes));

    /* asset_es3c28p / extra */
    sbc_get_string2(json, "asset_es3c28p", "merged", b->firmware_presets.extra_merged, sizeof(b->firmware_presets.extra_merged));
    sbc_get_string2(json, "asset_es3c28p", "elf", b->firmware_presets.extra_elf, sizeof(b->firmware_presets.extra_elf));
    if (b->firmware_presets.extra_merged[0] && !b->firmware_presets.extra_id[0])
        sbc_copy(b->firmware_presets.extra_id, sizeof(b->firmware_presets.extra_id), "asset_es3c28p");

    /* pins (string summaries - best effort from nested; keep embedded if not simple strings) */
    sbc_get_string2(json, "pins", "notes", b->pins.notes, sizeof(b->pins.notes));

    /* support flags */
    sbc_get_string2(json, "support", "serial_headless_local_emu", b->support.serial_headless, sizeof(b->support.serial_headless));
    if (!b->support.serial_headless[0]) sbc_get_string2(json, "support", "serial_headless_release_v3", b->support.serial_headless, sizeof(b->support.serial_headless));
    sbc_get_string2(json, "support", "display_spi_live", b->support.display_live, sizeof(b->support.display_live));
    if (!b->support.display_live[0]) sbc_get_string2(json, "support", "display_parallel_live", b->support.display_live, sizeof(b->support.display_live));
    sbc_get_string2(json, "support", "touch_i2c_ft6336g", b->support.touch_input, sizeof(b->support.touch_input));
    if (!b->support.touch_input[0]) sbc_get_string2(json, "support", "touch_xpt2046", b->support.touch_input, sizeof(b->support.touch_input));
    if (!b->support.touch_input[0]) sbc_get_string2(json, "support", "touch_gt911", b->support.touch_input, sizeof(b->support.touch_input));
    if (!b->support.touch_input[0]) sbc_get_string2(json, "support", "keyboard_matrix", b->support.touch_input, sizeof(b->support.touch_input));
    if (!b->support.touch_input[0]) sbc_get_string2(json, "support", "keyboard_trackball", b->support.touch_input, sizeof(b->support.touch_input));
    sbc_get_string2(json, "support", "audio_i2s_wav", b->support.audio, sizeof(b->support.audio));
    sbc_get_string2(json, "support", "sdmmc_emu", b->support.sdmmc_emu, sizeof(b->support.sdmmc_emu));
    sbc_get_string2(json, "support", "cosim_in_process", b->support.cosim_in_process, sizeof(b->support.cosim_in_process));
    sbc_get_string2(json, "support", "cosim_tcp_external", b->support.cosim_tcp_external, sizeof(b->support.cosim_tcp_external));

    b->support.parallel_needs_runtime_host = (b->display.bus[0] && strstr(b->display.bus, "parallel") != NULL);

    /* top-level notes: if string */
    const char *nv = sbc_find_key(json, "notes");
    if (nv && *nv == '"') {
        sbc_parse_string_at(nv, tmp, sizeof(tmp));
        if (tmp[0]) sbc_copy(b->notes, sizeof(b->notes), tmp);
    }
}

int sim_board_catalog_load_json(sim_board_catalog_t *cat, const char *path) {
    if (!cat || !path) return -1;
    size_t sz = 0;
    char *json = sbc_read_file(path, &sz);
    if (!json) return -1;

    char id[SBC_MAX_ID] = {0};
    sbc_get_string(json, "id", id, sizeof(id));
    sim_board_t *target = NULL;
    if (id[0]) target = sim_board_catalog_get_mut(cat, id);
    if (!target && cat->count < SBC_MAX_BOARDS) {
        target = &cat->boards[cat->count++];
        sbc_set_defaults_common(target);
        if (id[0]) sbc_copy(target->id, sizeof(target->id), id);
    }
    if (!target) { free(json); return -1; }

    sbc_apply_json(target, json);
    sbc_copy(target->source_json, sizeof(target->source_json), path);
    if (!target->id[0] && id[0]) sbc_copy(target->id, sizeof(target->id), id);
    free(json);
    return 0;
}

int sim_board_catalog_load_dir(sim_board_catalog_t *cat, const char *dir_path) {
    if (!cat || !dir_path) return -1;
    DIR *d = opendir(dir_path);
    if (!d) return -1;
    int loaded = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        size_t ln = strlen(name);
        if (ln < 6) continue;
        if (strcmp(name + ln - 5, ".json") != 0) continue;
        char path[SBC_MAX_PATH];
        snprintf(path, sizeof(path), "%s/%s", dir_path, name);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (sim_board_catalog_load_json(cat, path) == 0) loaded++;
    }
    closedir(d);
    return loaded;
}

const char *sim_board_cosim_json(const sim_board_t *b) {
    if (!b) return NULL;
    return b->cosim_board_json_suggested[0] ? b->cosim_board_json_suggested : NULL;
}

void sim_board_lcd_size(const sim_board_t *b, int *w, int *h) {
    if (w) *w = b ? b->display.width : 0;
    if (h) *h = b ? b->display.height : 0;
}

static int sbc_preset_eq(const char *a, const char *b) { return sbc_streq(a, b); }

const char *sim_board_preset_merged(const sim_board_t *b, const char *preset) {
    if (!b || !preset) return NULL;
    if (sbc_preset_eq(preset, "local")) return b->firmware_presets.local_merged[0] ? b->firmware_presets.local_merged : NULL;
    if (sbc_preset_eq(preset, "kryonos_v3")) return b->firmware_presets.kryonos_v3_merged[0] ? b->firmware_presets.kryonos_v3_merged : NULL;
    if (b->firmware_presets.extra_id[0] && sbc_preset_eq(preset, b->firmware_presets.extra_id))
        return b->firmware_presets.extra_merged[0] ? b->firmware_presets.extra_merged : NULL;
    if (sbc_preset_eq(preset, "asset_es3c28p")) return b->firmware_presets.extra_merged[0] ? b->firmware_presets.extra_merged : NULL;
    return NULL;
}

const char *sim_board_preset_elf(const sim_board_t *b, const char *preset) {
    if (!b || !preset) return NULL;
    if (sbc_preset_eq(preset, "local")) return b->firmware_presets.local_elf[0] ? b->firmware_presets.local_elf : NULL;
    if (sbc_preset_eq(preset, "kryonos_v3")) return b->firmware_presets.kryonos_v3_elf[0] ? b->firmware_presets.kryonos_v3_elf : NULL;
    if (b->firmware_presets.extra_id[0] && sbc_preset_eq(preset, b->firmware_presets.extra_id))
        return b->firmware_presets.extra_elf[0] ? b->firmware_presets.extra_elf : NULL;
    return NULL;
}

bool sim_board_preset_use_elf(const sim_board_t *b, const char *preset) {
    if (!b || !preset) return false;
    if (sbc_preset_eq(preset, "local")) return b->firmware_presets.local_use_elf;
    if (sbc_preset_eq(preset, "kryonos_v3")) return b->firmware_presets.kryonos_v3_use_elf;
    if (b->firmware_presets.extra_id[0] && sbc_preset_eq(preset, b->firmware_presets.extra_id))
        return b->firmware_presets.extra_use_elf;
    return false;
}

const char *sim_board_preset_notes(const sim_board_t *b, const char *preset) {
    if (!b || !preset) return NULL;
    if (sbc_preset_eq(preset, "local")) return b->firmware_presets.local_notes;
    if (sbc_preset_eq(preset, "kryonos_v3")) return b->firmware_presets.kryonos_v3_notes;
    if (b->firmware_presets.extra_id[0] && sbc_preset_eq(preset, b->firmware_presets.extra_id))
        return b->firmware_presets.extra_notes;
    return NULL;
}

int sim_board_resolve_firmware(const sim_board_t *b, const char *preset,
                               const char **merged_out, const char **elf_out, bool *use_elf_out) {
    const char *m = sim_board_preset_merged(b, preset);
    const char *e = sim_board_preset_elf(b, preset);
    bool u = sim_board_preset_use_elf(b, preset);
    if (merged_out) *merged_out = m;
    if (elf_out) *elf_out = e;
    if (use_elf_out) *use_elf_out = u;
    return (m && m[0]) ? 1 : 0;
}
