#ifndef SIM_FIRMWARE_MGR_H
#define SIM_FIRMWARE_MGR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FW_MAX_PATH       512
#define FW_MAX_SHORT      64
#define FW_MAX_MSG        1024
#define FW_MAX_ARGV       32
#define FW_MAX_CMDLINE    2048
#define FW_MAX_SEGMENTS   4

/* Default split offsets (ESP32-S3 layouts used by this repo) */
#define FW_OFF_BOOTLOADER 0x00000000u
#define FW_OFF_PARTITIONS 0x00008000u
#define FW_OFF_APP        0x00010000u
#define FW_OFF_FS         0x00C90000u /* LittleFS/spiffs when present (es3c28p merge) */

/* Region size for the FS partition in the 16MB es3c28p/KryonOS layouts
 * (spiffs@0xc90000 .. coredump@0xff0000). */
#define FW_LIMIT_FS_16MB  0x00360000u

/* ESP image header magic (bootloader/app/merged) and partition table magic. */
#define FW_IMAGE_MAGIC    0xE9u
#define FW_PART_MAGIC0    0xAAu
#define FW_PART_MAGIC1    0x50u

/* Known firmware presets */
typedef enum {
    FW_PRESET_LOCAL_ES3C28P_EMU = 0, /* simulator/build/merged-emu.bin + firmware/.pio/build/es3c28p_emu/firmware.elf */
    FW_PRESET_ASSET_ES3C28P,         /* simulator/emu/firmware/es3c28p/merged.bin (+ optional asset elf) */
    FW_PRESET_KRYONOS_V3,            /* simulator/emu/firmware/kryonos-v3/<board>/merged.bin (no elf) */
    FW_PRESET_CUSTOM,                /* caller-provided path */
    FW_PRESET_COUNT
} fw_preset_t;

/* Artifact kind inferred from a path/selection */
typedef enum {
    FW_KIND_UNKNOWN = 0,
    FW_KIND_MERGED,
    FW_KIND_SPLIT,
    FW_KIND_ELF,
    FW_KIND_FS
} fw_kind_t;

typedef struct {
    fw_kind_t kind;
    char source_path[FW_MAX_PATH]; /* input path inspected */

    /* Resolved artifact paths (empty if absent/not applicable) */
    char merged[FW_MAX_PATH];
    char bootloader[FW_MAX_PATH];
    char partitions[FW_MAX_PATH];
    char app[FW_MAX_PATH];         /* firmware.bin or app.bin */
    char elf[FW_MAX_PATH];
    char fs[FW_MAX_PATH];          /* littlefs.bin/spiffs.bin when provided/found */

    /* Offsets for split/merge inputs */
    uint32_t off_bootloader;
    uint32_t off_partitions;
    uint32_t off_app;
    uint32_t off_fs;

    /* Sizes in bytes (0 if missing/unknown) */
    uint64_t size_merged;
    uint64_t size_bootloader;
    uint64_t size_partitions;
    uint64_t size_app;
    uint64_t size_elf;
    uint64_t size_fs;

    bool has_merged;
    bool has_split;  /* bootloader + partitions + app */
    bool has_elf;
    bool has_fs;
} fw_artifacts_t;

typedef struct {
    bool ok;
    int error_count;
    int warning_count;
    char errors[FW_MAX_MSG];
    char warnings[FW_MAX_MSG];
} fw_validation_t;

/* Merge/flash parameters for esptool merge-bin */
typedef struct {
    char chip[FW_MAX_SHORT];       /* e.g. esp32s3 */
    char flash_mode[FW_MAX_SHORT]; /* e.g. qio */
    char flash_freq[FW_MAX_SHORT]; /* e.g. 80m */
    char flash_size[FW_MAX_SHORT]; /* e.g. 16MB */
    char output[FW_MAX_PATH];
} fw_merge_opts_t;

/* Built command (argv for exec or string for logging/UI). Not executed. */
typedef struct {
    char esptool_path[FW_MAX_PATH];
    int argc;
    char *argv[FW_MAX_ARGV];
    char argv_storage[FW_MAX_CMDLINE];
    char cmdline[FW_MAX_CMDLINE];
} fw_merge_cmd_t;

typedef struct {
    fw_preset_t preset;
    char board[FW_MAX_SHORT];      /* used by KRYONOS_V3; empty for others */
    fw_artifacts_t artifacts;
    bool exists;                   /* required artifacts for preset exist and non-empty */
    char note[FW_MAX_MSG];         /* brief status note */
} fw_preset_info_t;

/* --- Utilities --- */
bool fw_path_exists(const char *path);
bool fw_path_is_file(const char *path);
bool fw_path_is_dir(const char *path);
uint64_t fw_file_size(const char *path);

/* Known paths helpers */
const char *fw_known_local_merged_path(void); /* simulator/build/merged-emu.bin */
const char *fw_known_local_elf_path(void);    /* firmware/.pio/build/es3c28p_emu/firmware.elf */
const char *fw_known_asset_es3c28p_dir(void); /* simulator/emu/firmware/es3c28p */
const char *fw_known_kryonos_v3_root(void);   /* simulator/emu/firmware/kryonos-v3 */

/* Build known paths (out buffers must be FW_MAX_PATH). Return 0 on success. */
int fw_path_asset_es3c28p_merged(char *out, size_t outsz);
int fw_path_asset_es3c28p_elf(char *out, size_t outsz);
int fw_path_kryonos_v3_board_dir(const char *board, char *out, size_t outsz);
int fw_path_kryonos_v3_merged(const char *board, char *out, size_t outsz);

/* --- Detection --- */
/* Initialize artifacts with defaults/empty fields. */
void fw_artifacts_init(fw_artifacts_t *a);

/* Detect artifacts from a path (file or directory). Infers merged/split/elf/fs
 * and fills known split paths/offsets when found in directory. Returns 0 on
 * success (including unknown/partial), <0 on bad args. */
int fw_detect(const char *path, fw_artifacts_t *out);

/* --- Presets --- */
const char *fw_preset_name(fw_preset_t preset);
int fw_preset_from_name(const char *name, fw_preset_t *out);

/* List static presets into caller array. For KRYONOS_V3, if board is NULL/empty,
 * emits one entry per board directory found under kryonos-v3 root (up to max);
 * if board is set, emits that board entry. Returns count written, <0 on error. */
int fw_list_presets(const char *board_filter, fw_preset_info_t *out, int max);

/* Resolve a preset for board (board required for KRYONOS_V3, ignored otherwise).
 * Populates artifacts paths and preset metadata. Returns 0 on success, <0 on bad args. */
int fw_resolve_preset(fw_preset_t preset, const char *board, fw_preset_info_t *out);

/* --- Validation --- */
void fw_validation_init(fw_validation_t *v);

/* Validate artifacts for executability/coherence.
 * - files exist and size > 0
 * - merged present OR complete split (bootloader+partitions+app)
 * - warning if ELF missing for non-release/custom cases where symbols useful
 *   (is_release_hint=true suppresses ELF-missing-as-error but still warns)
 * Fills errors/warnings and ok flag. Returns 0 on successful evaluation. */
int fw_validate(const fw_artifacts_t *a, bool is_release_hint, fw_validation_t *out);

/* --- Merge command (esptool) --- */
void fw_merge_opts_defaults(fw_merge_opts_t *o);

/* Find esptool executable: $HOME/.platformio/penv/bin/esptool, esptool.py, esptool.
 * Writes resolved path/command name into out. Returns 0 if found, <0 if not. */
int fw_find_esptool(char *out, size_t outsz);

/* Build esptool merge-bin command from artifacts (split or merged->no-op not built).
 * Requires complete split or (boot+part+app[+fs]) info; uses offsets from artifacts.
 * opts may be NULL for defaults; opts->output should be set. Does not execute.
 * Returns 0 on success, <0 if inputs insufficient or esptool not found. */
int fw_build_merge_cmd(const fw_artifacts_t *a, const fw_merge_opts_t *opts, fw_merge_cmd_t *out);

/* Convenience: resolve preset, detect/validate as needed is caller's choice; this
 * helper builds merge cmd for a preset+board to opts->output when split artifacts exist.
 * Returns 0 on success. */
int fw_build_merge_cmd_for_preset(fw_preset_t preset, const char *board,
                                  const fw_merge_opts_t *opts, fw_merge_cmd_t *out);

/* ---------------------------------------------------------------------------
 * Merge plan (structured, UI/CLI friendly)
 *
 * A plan is pure data: board preset defaults, per-field paths/offsets, flash
 * settings and an output path. Validation, command generation, summarising and
 * optional execution are separate calls so the launcher can preview/tweak
 * before running. No shell is ever used for execution (argv/execvp only).
 * ------------------------------------------------------------------------- */

typedef enum {
    FW_CAP_SUPPORTED_NOW = 0,     /* usable in the current local flow */
    FW_CAP_CONDITIONAL,           /* conditional(version/tool/chip/firmware) */
    FW_CAP_NEEDS_RUNTIME_HOST,    /* depends on host/part/board not ready */
    FW_CAP_BLOCKED_BY_EMU         /* blocked by an esp-emu model limitation (feature not modelled) */
} fw_capability_t;

typedef enum {
    FW_SEG_BOOTLOADER = 0,
    FW_SEG_PARTITIONS,
    FW_SEG_APP,
    FW_SEG_FS,
    FW_SEG_COUNT
} fw_segment_role_t;

typedef struct {
    fw_segment_role_t role;
    char path[FW_MAX_PATH];
    uint32_t offset;        /* flash offset for merge */
    uint32_t limit;         /* region/partition max bytes, 0 = unknown */
    uint64_t size;          /* observed bytes (validation/detect fill) */
    bool required;          /* required for a usable merge */
    bool present;           /* path exists and is non-empty (validation fill) */
    bool signature_ok;      /* magic/sanity check result (validation fill) */
    char problem[256];      /* short reason when present/signature false */
} fw_merge_segment_t;

typedef struct {
    char board[FW_MAX_SHORT];       /* board id, e.g. es3c28p */
    char chip[FW_MAX_SHORT];        /* e.g. esp32s3 */
    char flash_mode[FW_MAX_SHORT];  /* e.g. qio */
    char flash_freq[FW_MAX_SHORT];  /* e.g. 80m */
    char flash_size[FW_MAX_SHORT];  /* e.g. 16MB */

    char output[FW_MAX_PATH];       /* merged output file */
    char source_dir[FW_MAX_PATH];   /* preset input dir (informational) */
    char esptool[FW_MAX_PATH];      /* override path/name; empty = autodetect */

    bool include_fs;                /* include FW_SEG_FS when path present */
    bool dry_run;                   /* advisory; fw_merge_plan_run takes its own flag */
    bool overwrite;                 /* allow replacing an existing output */

    fw_merge_segment_t segments[FW_SEG_COUNT];
    char notes[FW_MAX_MSG];
} fw_merge_plan_t;

const char *fw_capability_str(fw_capability_t cap);
const char *fw_segment_role_name(fw_segment_role_t role);
int fw_segment_role_from_name(const char *name, fw_segment_role_t *out);

/* True for the board ids with built-in merge presets (es3c28p,
 * esp32-s3-devkitc-1-n16r8, lilygo-t-deck, lilygo-t-hmi, m5stack-cardputer). */
bool fw_board_preset_known(const char *board_id);

/* Init plan with generic ESP32-S3 defaults (no board paths). */
void fw_merge_plan_init(fw_merge_plan_t *plan);

/* Fill a coherent plan for a board preset:
 *   es3c28p                     local build dir if present, else asset dir;
 *                               16MB qio/80m, offsets 0x0/0x8000/0x10000/0xc90000,
 *                               output simulator/build/merged-emu.bin, FS auto if found.
 *   esp32-s3-devkitc-1-n16r8    KryonOS v3 split dir, 16MB, output merged-<board>.bin.
 *   lilygo-t-deck               idem.
 *   lilygo-t-hmi                idem.
 *   m5stack-cardputer           KryonOS v3 split dir, 8MB, FS offset unknown (disabled).
 * NULL/empty board id means "es3c28p". Returns 0 on success, -1 on unknown id
 * (use fw_board_preset_known) or bad args. */
int fw_merge_plan_defaults_for_board(const char *board_id, fw_merge_plan_t *out);

/* Requested exact name (alias of fw_merge_plan_defaults_for_board). */
static inline int sim_merge_plan_defaults_for_board(const char *board_id, fw_merge_plan_t *out) {
    return fw_merge_plan_defaults_for_board(board_id, out);
}

/* Per-field overrides. set_segment keeps the current offset; set_offset keeps
 * the path. path NULL/empty clears the path. Returns 0 on success. */
int fw_merge_plan_set_segment(fw_merge_plan_t *plan, fw_segment_role_t role, const char *path);
int fw_merge_plan_set_offset(fw_merge_plan_t *plan, fw_segment_role_t role, uint32_t offset);
int fw_merge_plan_set_fs(fw_merge_plan_t *plan, const char *path, bool enable);
int fw_merge_plan_set_output(fw_merge_plan_t *plan, const char *path);
int fw_merge_plan_set_flash(fw_merge_plan_t *plan, const char *mode, const char *freq,
                            const char *size);

/* Validate a plan:
 *  - required input paths set, existing and readable/non-empty
 *  - offsets strictly increasing, segments non-overlapping, within flash size
 *  - FS segment within its partition limit; error if include_fs but missing
 *  - warning when no FS image is included (the SD card is separate: --sd, see fw_sd_capability)
 *  - output dir exists or is creatable; error if output exists and !overwrite
 *  - basic signature check (0xE9 image, 0xAA50 partition table) as warnings
 *  - esptool availability reported as capability (supported-now/conditional)
 * Also fills per-segment present/size/signature fields on the plan.
 * Returns 0 on successful evaluation (out->ok reflects pass/fail). */
int fw_merge_plan_validate(fw_merge_plan_t *plan, fw_validation_t *out);

/* esptool detection/version info. probe returns 0 on evaluation (even when not
 * found; check out->found). merge_subcmd is "merge-bin" for esptool >= 5,
 * "merge_bin" for older, default "merge-bin" when version is unknown. */
typedef struct {
    bool found;
    char path[FW_MAX_PATH];
    char version[FW_MAX_SHORT];
    char merge_subcmd[FW_MAX_SHORT];
    fw_capability_t capability;
    char note[FW_MAX_MSG];
} fw_esptool_info_t;
int fw_esptool_probe(fw_esptool_info_t *out);

/* Build argv for `esptool --chip <chip> merge-bin -o ... <off> <file> ...`.
 * Uses plan->esptool when set, otherwise the detected executable (falling back
 * to "esptool" so dry-run previews work when nothing is installed).
 * Does not execute and does not require inputs to exist (validate first).
 * Returns 0 on success, <0 on bad args/no output/overflow. */
int fw_merge_plan_build_cmd(const fw_merge_plan_t *plan, fw_merge_cmd_t *out);

/* Multi-line human summary for UI/log: board, chip/flash, esptool capability,
 * segment table (role/offset/size/status), output, notes. Returns 0 on success. */
int fw_merge_plan_summary(const fw_merge_plan_t *plan, char *out, size_t outsz);

/* Execution result (argv/execvp, stdout+stderr captured tail). */
typedef struct {
    bool dry_run;
    bool executed;
    int exit_code;              /* -1 when not executed; 127 exec failure */
    char cmdline[FW_MAX_CMDLINE];
    char output[FW_MAX_MSG * 2]; /* captured combined output (tail) */
} fw_merge_run_t;

/* Run a built command. dry_run=true sets executed=false and output=cmdline.
 * Returns 0 when the command ran and exited 0, 1 when it ran and failed,
 * <0 on bad args/setup errors. No shell is involved. */
int fw_merge_cmd_run(const fw_merge_cmd_t *cmd, bool dry_run, fw_merge_run_t *out);

/* Build + (optionally) execute a plan. Returns like fw_merge_cmd_run. */
int fw_merge_plan_run(const fw_merge_plan_t *plan, bool dry_run, fw_merge_run_t *out);

/* Artifact probe for already-merged/split inputs: name heuristics (fw_detect),
 * manifest.json presence/board match, per-file magic/size checks. */
typedef struct {
    bool name_merged_hint;
    bool name_split_hint;
    bool has_merged;
    bool has_split;
    bool has_fs;
    bool has_elf;

    char merged[FW_MAX_PATH];
    char bootloader[FW_MAX_PATH];
    char partitions[FW_MAX_PATH];
    char app[FW_MAX_PATH];
    char elf[FW_MAX_PATH];
    char fs[FW_MAX_PATH];

    uint64_t size_merged;
    uint64_t size_bootloader;
    uint64_t size_partitions;
    uint64_t size_app;
    uint64_t size_fs;

    bool merged_magic_ok;
    bool bootloader_magic_ok;
    bool partitions_magic_ok;
    bool app_magic_ok;
    bool signature_ok;          /* all present image/partition magics ok */

    bool manifest_found;
    bool manifest_board_match;
    char manifest_path[FW_MAX_PATH];
    char note[FW_MAX_MSG];
} fw_artifact_probe_t;

int fw_probe_artifacts(const char *path, fw_artifact_probe_t *out);

/* SD capability on the current emulator line (esp-emu 1.1.0, ESP32-S3 SDMMC host):
 * supported-now through --sd with a FAT card image (./start.sh card image). */
fw_capability_t fw_sd_capability(void);
const char *fw_sd_capability_note(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_FIRMWARE_MGR_H */
