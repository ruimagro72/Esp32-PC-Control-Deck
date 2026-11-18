#ifndef SIM_STORAGE_LITTLEFS_H
#define SIM_STORAGE_LITTLEFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SLFS_MAX_PATH   512
#define SLFS_MAX_SHORT  64
#define SLFS_MAX_MSG    1024
#define SLFS_MAX_ARGS   512

/* Capability state (aligned with docs/simulator/SIMULATOR_CAPABILITIES_MATRIX.md) */
typedef enum {
    SLFS_CAP_SUPPORTED_NOW = 0,      /* usable in current local flow */
    SLFS_CAP_NEEDS_RUNTIME_HOST,     /* depends on host/part/board/wiring not ready */
    SLFS_CAP_BLOCKED_BY_EMU,         /* blocked by an esp-emu limitation (not modeled) */
    SLFS_CAP_CONDITIONAL             /* conditional(version/tool/chip/firmware) */
} slfs_capability_t;

typedef enum {
    SLFS_SD_MODE_NONE = 0,
    SLFS_SD_MODE_SDMMC,
    SLFS_SD_MODE_SDSPI
} slfs_sd_mode_t;

/* LittleFS/data-partition image options (alternative to SD when data fits). */
typedef struct {
    bool enabled;
    char image_path[SLFS_MAX_PATH];     /* littlefs/spiffs image for merge/flash */
    char partition_label[SLFS_MAX_SHORT]; /* e.g. "spiffs"/"littlefs" (hint) */
    char mount_path[SLFS_MAX_SHORT];    /* VFS mount hint, e.g. "/littlefs" */
    uint32_t offset;                    /* partition/image offset bytes, 0 = unknown */
    uint32_t size;                      /* partition/image size bytes, 0 = unknown */
    bool image_optional;                /* if true, missing image is warning not error */
    slfs_capability_t capability;       /* typically CONDITIONAL(version/tool) */
} slfs_littlefs_t;

/* SD card image/model options. */
typedef struct {
    bool enabled;
    char image_path[SLFS_MAX_PATH];     /* raw sdcard image (future --sd) */
    slfs_sd_mode_t mode;                /* SDMMC / SDSPI */
    bool read_only;                     /* ro hint for future --sd */
    int slot;                           /* slot hint (SDMMC) or -1 if n/a */
    int cs_pin;                         /* CS pin hint (SDSPI) or -1 if n/a */
    slfs_capability_t capability;       /* default SUPPORTED_NOW (esp-emu 1.1.0 --sd) */
    char capability_note[SLFS_MAX_MSG]; /* human note, e.g. esp-emu 1.1.0 --sd */
} slfs_sd_t;

typedef struct {
    slfs_littlefs_t littlefs;
    slfs_sd_t sd;
} slfs_config_t;

typedef struct {
    bool ok;
    int error_count;
    int warning_count;
    char errors[SLFS_MAX_MSG];
    char warnings[SLFS_MAX_MSG];
    char notes[SLFS_MAX_MSG];
} slfs_report_t;

/* --- Init / defaults --- */
const char *slfs_capability_str(slfs_capability_t cap);
const char *slfs_sd_mode_str(slfs_sd_mode_t mode);
int slfs_sd_mode_from_str(const char *s, slfs_sd_mode_t *out);

void slfs_littlefs_init(slfs_littlefs_t *lfs);
void slfs_sd_init(slfs_sd_t *sd);
void slfs_config_init(slfs_config_t *cfg);

/* Defaults: LittleFS disabled/conditional hints, SD disabled but modeled as
 * SDMMC + SUPPORTED_NOW (esp-emu 1.1.0 --sd) for ESP32-S3 with an explanatory note. */
void slfs_config_defaults(slfs_config_t *cfg);

/* --- Set/get helpers --- */
int slfs_littlefs_set_image(slfs_littlefs_t *lfs, const char *path);
int slfs_littlefs_set_partition(slfs_littlefs_t *lfs, const char *label,
                                uint32_t offset, uint32_t size);
int slfs_littlefs_set_mount(slfs_littlefs_t *lfs, const char *mount_path);

int slfs_sd_set_image(slfs_sd_t *sd, const char *path);
int slfs_sd_set_mode(slfs_sd_t *sd, slfs_sd_mode_t mode);
int slfs_sd_set_hints(slfs_sd_t *sd, bool read_only, int slot, int cs_pin);

/* Compute effective capabilities given esp-emu version context.
 * emu_major/emu_minor/emu_patch: the probed esp-emu version (1.1.0 when not probed).
 * is_esp32s3 true marks the S3 target (SDMMC via --sd from esp-emu 1.1.0).
 * Updates capability fields/notes in-place. Returns 0 on success. */
int slfs_apply_capabilities(slfs_config_t *cfg, int emu_major, int emu_minor,
                            int emu_patch, bool is_esp32s3);

/* --- Validation --- */
void slfs_report_init(slfs_report_t *r);
/* Validate inputs: numeric ranges, path existence when enabled and required.
 * check_paths=true performs filesystem existence checks for provided paths.
 * Populates report with errors/warnings/notes (including unavailability notes). */
int slfs_validate(const slfs_config_t *cfg, bool check_paths, slfs_report_t *out);

/* --- Command/config fragments (best-effort; may be unavailable) --- */
/* Build merge-related LittleFS fragment when supported/applicable.
 * Example: "--littlefs <path>" if enabled and path set. Returns chars written,
 * 0 if nothing applicable, -1 on error/truncation. Writes reason to note_out. */
int slfs_build_merge_args(const slfs_config_t *cfg, char *buf, size_t buflen,
                          char *note_out, size_t note_len);

/* Build future esp-emu SD args fragment (e.g. --sd <img> / mode hints) only when
 * capability indicates it may be usable; otherwise returns 0 with unavailability note. */
int slfs_build_sd_args(const slfs_config_t *cfg, char *buf, size_t buflen,
                       char *note_out, size_t note_len);

/* Build combined args/fragments (merge + sd) best-effort. */
int slfs_build_args(const slfs_config_t *cfg, char *buf, size_t buflen,
                    char *note_out, size_t note_len);

/* --- Summary for UI/log --- */
/* Human-readable multi-line summary into buf. Returns chars written or -1. */
int slfs_format_summary(const slfs_config_t *cfg, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* SIM_STORAGE_LITTLEFS_H */
