#ifndef SIM_BOARD_CATALOG_H
#define SIM_BOARD_CATALOG_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Board/profile catalog for simulator launcher.
 * Metadata is editable via JSON files in simulator/launcher/boards/.
 * Embedded defaults match shipped JSONs; JSON loads override fields by board id.
 * Facts based on docs/simulator/SIMULATOR_CAPABILITIES_MATRIX.md, docs/simulator/SIMULATOR_PROFILE_SPEC.md,
 * simulator/emu/HARDWARE-SUPPORT.md, simulator/emu/KRYONOS-V3.0.0-PREP.md.
 */

#define SBC_MAX_BOARDS      16
#define SBC_MAX_ID          64
#define SBC_MAX_NAME        128
#define SBC_MAX_SHORT       64
#define SBC_MAX_MED         192
#define SBC_MAX_PATH        384
#define SBC_MAX_LONG        512

typedef struct {
    int  width;
    int  height;
    char controller[SBC_MAX_SHORT]; /* e.g. ILI9341, ST7789, ST7789V2 */
    char bus[SBC_MAX_SHORT];        /* spi, parallel */
    char interface[SBC_MAX_MED];    /* human description */
    char notes[SBC_MAX_LONG];
} sim_board_display_t;

typedef struct {
    char type[SBC_MAX_SHORT];       /* touch, keyboard, touch+keyboard+trackball */
    char controller[SBC_MAX_SHORT]; /* FT6336G, GT911, XPT2046, null/none */
    char bus[SBC_MAX_SHORT];        /* i2c, spi, spi/bitbang, gpio-matrix */
    char i2c_addr[SBC_MAX_SHORT];   /* primary addr or empty */
    char keyboard[SBC_MAX_MED];     /* keyboard/trackball notes */
    char notes[SBC_MAX_LONG];
} sim_board_touch_input_t;

typedef struct {
    char codec[SBC_MAX_SHORT];
    char codec_i2c_addr[SBC_MAX_SHORT];
    char bus[SBC_MAX_SHORT];
    bool uncertain;
    char notes[SBC_MAX_LONG];
} sim_board_audio_t;

typedef struct {
    char flash_fs[SBC_MAX_MED];
    char sd[SBC_MAX_MED];
    char sd_emulation[SBC_MAX_SHORT]; /* blocked-by-emu, conditional, etc */
    char notes[SBC_MAX_LONG];
} sim_board_storage_t;

typedef struct {
    char local_merged[SBC_MAX_PATH];
    char local_elf[SBC_MAX_PATH];
    bool local_use_elf;
    char local_notes[SBC_MAX_MED];

    char kryonos_v3_merged[SBC_MAX_PATH];
    char kryonos_v3_elf[SBC_MAX_PATH];
    bool kryonos_v3_use_elf;
    char kryonos_v3_notes[SBC_MAX_MED];

    /* optional extra preset (e.g. asset_es3c28p) */
    char extra_id[SBC_MAX_SHORT];
    char extra_merged[SBC_MAX_PATH];
    char extra_elf[SBC_MAX_PATH];
    bool extra_use_elf;
    char extra_notes[SBC_MAX_MED];
} sim_board_firmware_presets_t;

typedef struct {
    /* Selected known pins as strings for easy display/edit; JSON carries details. */
    char lcd[SBC_MAX_LONG];
    char touch[SBC_MAX_MED];
    char input[SBC_MAX_MED];    /* keyboard/trackball */
    char storage[SBC_MAX_MED];
    char power_misc[SBC_MAX_MED];
    char notes[SBC_MAX_LONG];
} sim_board_pins_t;

typedef struct {
    char serial_headless[SBC_MAX_SHORT];
    char display_live[SBC_MAX_SHORT];
    char touch_input[SBC_MAX_SHORT];
    char audio[SBC_MAX_SHORT];
    char sdmmc_emu[SBC_MAX_SHORT];
    char cosim_in_process[SBC_MAX_SHORT];
    char cosim_tcp_external[SBC_MAX_SHORT];
    bool parallel_needs_runtime_host;
    char notes[SBC_MAX_MED];
} sim_board_support_t;

typedef struct {
    char id[SBC_MAX_ID];
    char name[SBC_MAX_NAME];
    char mcu[SBC_MAX_SHORT]; /* esp32s3 */
    int  flash_mb;
    int  psram_mb;

    sim_board_display_t          display;
    sim_board_touch_input_t      touch_input;
    sim_board_audio_t            audio;
    sim_board_storage_t          storage;
    char                         cosim_board_json_suggested[SBC_MAX_PATH];
    sim_board_firmware_presets_t firmware_presets;
    sim_board_pins_t             pins;
    sim_board_support_t          support;

    char notes[SBC_MAX_LONG];
    char source_json[SBC_MAX_PATH]; /* path loaded from, if any */
} sim_board_t;

typedef struct {
    sim_board_t boards[SBC_MAX_BOARDS];
    int         count;
} sim_board_catalog_t;

/* Initialize catalog with embedded defaults for all known boards. */
void sim_board_catalog_init(sim_board_catalog_t *cat);

/* Number of boards. */
int sim_board_catalog_count(const sim_board_catalog_t *cat);

/* List boards (array of length count). count_out optional. */
const sim_board_t *sim_board_catalog_list(const sim_board_catalog_t *cat, int *count_out);

/* Get board by id (e.g. "es3c28p"). Returns NULL if not found. */
const sim_board_t *sim_board_catalog_get(const sim_board_catalog_t *cat, const char *id);

/* Mutable access by id (for advanced callers); NULL if not found. */
sim_board_t *sim_board_catalog_get_mut(sim_board_catalog_t *cat, const char *id);

/* Load/override metadata from a single board JSON file.
 * If board id in file matches an existing entry, fields present in JSON override it.
 * If unknown id and space available, appends a new entry with parsed basics.
 * Returns 0 on success, -1 on IO/parse failure. */
int sim_board_catalog_load_json(sim_board_catalog_t *cat, const char *path);

/* Load all *.json files from directory (non-recursive). Returns number loaded (>=0), -1 on dir error. */
int sim_board_catalog_load_dir(sim_board_catalog_t *cat, const char *dir_path);

/* --- Helpers / presets --- */

/* Suggested cosim board JSON path (may be empty). */
const char *sim_board_cosim_json(const sim_board_t *b);

/* LCD dimensions. */
void sim_board_lcd_size(const sim_board_t *b, int *w, int *h);

/* Firmware preset helpers. preset: "local", "kryonos_v3", or board-specific extra id (e.g. "asset_es3c28p").
 * Return NULL/false if unavailable. merged_out/elf_out receive internal pointers (do not free). */
const char *sim_board_preset_merged(const sim_board_t *b, const char *preset);
const char *sim_board_preset_elf(const sim_board_t *b, const char *preset);
bool        sim_board_preset_use_elf(const sim_board_t *b, const char *preset);
const char *sim_board_preset_notes(const sim_board_t *b, const char *preset);

/* Convenience: map preset to esp-emu style args hints.
 * Fills merged/elf/use_elf when non-NULL; returns 1 if a usable merged path string is set, 0 otherwise. */
int sim_board_resolve_firmware(const sim_board_t *b, const char *preset,
                               const char **merged_out, const char **elf_out, bool *use_elf_out);

#ifdef __cplusplus
}
#endif

#endif /* SIM_BOARD_CATALOG_H */
