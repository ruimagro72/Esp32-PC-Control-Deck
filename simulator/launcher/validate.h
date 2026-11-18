#ifndef SIM_LAUNCHER_VALIDATE_H
#define SIM_LAUNCHER_VALIDATE_H

#include <stdbool.h>
#include <stddef.h>

#include "config.h"
#include "emu_caps_probe.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VALIDATE_MAX_CODE       64
#define VALIDATE_MAX_FIELD      64
#define VALIDATE_MAX_TEXT       256
#define VALIDATE_MAX_PATH       LAUNCHER_MAX_PATH
#define VALIDATE_MAX_RESULTS    64

typedef enum {
    VALIDATE_SEV_INFO = 0,
    VALIDATE_SEV_WARN = 1,
    VALIDATE_SEV_ERROR = 2
} validate_severity_t;

typedef enum {
    VALIDATE_CHECK_NONE      = 0,
    VALIDATE_CHECK_TOOLS     = 1u << 0, /* esp-emu, lcd_touch_host, esptool, socat/nc, gcc, pkg-config, SDL2 */
    VALIDATE_CHECK_FILES     = 1u << 1, /* firmware/ELF/cosim/littlefs existence */
    VALIDATE_CHECK_RANGES    = 1u << 2, /* hw_rot/scale/lcd/ports/basic values */
    VALIDATE_CHECK_CAPS      = 1u << 3, /* capability/support matrix rules */
    VALIDATE_CHECK_CONFLICTS = 1u << 4, /* obvious config/profile conflicts */
    VALIDATE_CHECK_ALL       = 0x1Fu
} validate_check_flags_t;

typedef struct {
    char code[VALIDATE_MAX_CODE];         /* stable machine code, e.g. CAP_SD_BLOCKED */
    validate_severity_t severity;         /* error/warn/info for UI */
    char field[VALIDATE_MAX_FIELD];       /* related field/path/key, may be empty */
    char message[VALIDATE_MAX_TEXT];      /* human-readable issue */
    char suggestion[VALIDATE_MAX_TEXT];   /* actionable recommendation, may be empty */
} validate_result_t;

typedef struct {
    validate_result_t items[VALIDATE_MAX_RESULTS];
    int count;
    bool truncated;
} validate_report_t;

/* Optional inputs not always present in launcher_config_t. Paths may be absolute
 * or repo-relative (resolved against repo_root when provided). Empty = unset. */
typedef struct {
    char littlefs_path[VALIDATE_MAX_PATH];
    bool sd_requested;          /* explicit SD/--sd intent (blocked only if the probed esp-emu lacks --sd) */
    bool release_no_elf;        /* hint: release asset without ELF */
    bool esp_emu_has_advanced;  /* hint: emu >= version with gdb/trace/sd/parts advanced support */
    /* Optional runtime probe (path/version/flags). When set, version/chip-derived
     * capability states and unsupported-flag notes use it; otherwise S3/1.1.0 defaults apply. */
    const emu_caps_probe_t *emu_caps;
} validate_extra_t;

void validate_report_init(validate_report_t *report);
void validate_report_clear(validate_report_t *report);
int validate_report_add(validate_report_t *report, const char *code,
                        validate_severity_t severity, const char *field,
                        const char *message, const char *suggestion);
int validate_report_count(const validate_report_t *report);
const validate_result_t *validate_report_get(const validate_report_t *report, int index);
bool validate_report_has_errors(const validate_report_t *report);
bool validate_report_has_warnings(const validate_report_t *report);
const char *validate_severity_name(validate_severity_t severity);

/* Helpers exposed for callers/tests. */
bool validate_path_exists(const char *path);
bool validate_path_exists_in_repo(const char *repo_root, const char *path, char *resolved, size_t resolved_len);
bool validate_find_in_path(const char *name, char *out, size_t out_len);
bool validate_port_plausible_str(const char *host_port, int *out_port);
bool validate_parse_host_port(const char *host_port, char *host, size_t host_len, int *out_port);

/* Tool/path preflight checks (optional by context). repo_root may be NULL -> cwd.
 * Emits INFO for found tools, WARN for missing optional tools. */
int validate_tools(const char *repo_root, unsigned flags, validate_report_t *report);

/* Validate launcher config + optional extra/profile-derived inputs.
 * repo_root used to resolve relative paths and default tool locations. */
int validate_config(const launcher_config_t *cfg, const validate_extra_t *extra,
                    const char *repo_root, unsigned flags, validate_report_t *report);

/* Path/profile-oriented convenience: validate explicit file/range fields without
 * requiring a full launcher_config_t (useful for profile preflight/UI). Any
 * pointer may be NULL/empty to skip that field. */
int validate_profile_paths(const char *repo_root,
                           const char *merged_or_firmware_path,
                           const char *elf_path, bool use_elf,
                           const char *cosim_board_path,
                           const char *littlefs_path,
                           int hw_rotation, int scale,
                           int lcd_w, int lcd_h,
                           int instance,
                           bool sd_requested,
                           bool release_no_elf,
                           bool esp_emu_has_advanced,
                           unsigned flags,
                           validate_report_t *report);

#ifdef __cplusplus
}
#endif

#endif /* SIM_LAUNCHER_VALIDATE_H */
