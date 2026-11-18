#ifndef SIM_LAUNCHER_PARTS_UI_H
#define SIM_LAUNCHER_PARTS_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Virtual parts (esp-part / WASM) management skeleton for launcher/cosim.
 * Data-only module: no GTK/runtime dependencies. Conditional on esp-emu/cosim
 * version, ABI and toolchain support.
 */

#define PARTS_UI_MAX_PATH      512
#define PARTS_UI_MAX_NAME      96
#define PARTS_UI_MAX_SHORT     64
#define PARTS_UI_MAX_CLAIMS    64
#define PARTS_UI_MAX_PARTS     32
#define PARTS_UI_MAX_INSTANCES 32
#define PARTS_UI_MAX_EVENTS    64
#define PARTS_UI_MAX_DIAGS     64
#define PARTS_UI_MAX_TEXT      256

typedef enum {
    PARTS_UI_SUPPORT_SUPPORTED_NOW = 0,
    PARTS_UI_SUPPORT_CONDITIONAL_VERSION_TOOL,
    PARTS_UI_SUPPORT_NEEDS_RUNTIME_HOST,
    PARTS_UI_SUPPORT_BLOCKED_BY_EMU
} parts_ui_support_state_t;

typedef enum {
    PARTS_UI_CLAIM_GPIO = 0,
    PARTS_UI_CLAIM_I2C_ADDR,
    PARTS_UI_CLAIM_SPI_CS,
    PARTS_UI_CLAIM_UART,
    PARTS_UI_CLAIM_PWM,
    PARTS_UI_CLAIM_ADC,
    PARTS_UI_CLAIM_EN
} parts_ui_claim_kind_t;

typedef enum {
    PARTS_UI_DIAG_INFO = 0,
    PARTS_UI_DIAG_WARN,
    PARTS_UI_DIAG_ERROR
} parts_ui_diag_sev_t;

typedef struct {
    parts_ui_claim_kind_t kind;
    /* Common selectors (use as applicable per kind). -1/empty means unset. */
    char bus[PARTS_UI_MAX_SHORT];      /* i2c/spi/uart instance or name */
    int  port;                         /* bus/controller port/index */
    int  pin;                          /* GPIO pin */
    int  addr;                         /* I2C address (7-bit/byte value as discovered) */
    int  cs_pin;                       /* SPI CS pin */
    int  channel;                      /* PWM/ADC channel */
    char role[PARTS_UI_MAX_SHORT];     /* drive/watch/en/sda/scl/mosi/miso/sclk/dc/bl/int/... */
    char note[PARTS_UI_MAX_TEXT];
} parts_ui_claim_t;

typedef struct {
    char key[PARTS_UI_MAX_NAME];
    char value[PARTS_UI_MAX_TEXT];
} parts_ui_kv_t;

typedef struct {
    char wasm_path[PARTS_UI_MAX_PATH];
    char sidecar_json_path[PARTS_UI_MAX_PATH]; /* <name>.json / <name>.part.json if found */
    bool has_sidecar;
    char name[PARTS_UI_MAX_NAME];
    char abi_version[PARTS_UI_MAX_SHORT];      /* from sidecar if available */
    char proto_version[PARTS_UI_MAX_SHORT];    /* esp_cosim.proto/ABI hint */
    parts_ui_support_state_t support;
    char support_note[PARTS_UI_MAX_TEXT];

    /* Declared claims/metadata (from sidecar or inferred). */
    parts_ui_claim_t claims[PARTS_UI_MAX_CLAIMS];
    int claim_count;

    /* Generic metadata/properties exposed to UI as strings. */
    parts_ui_kv_t meta[PARTS_UI_MAX_CLAIMS];
    int meta_count;
} parts_ui_part_meta_t;

typedef struct {
    char id[PARTS_UI_MAX_NAME];               /* instance id */
    char part_name[PARTS_UI_MAX_NAME];        /* references discovered part meta name */
    char wasm_path[PARTS_UI_MAX_PATH];        /* resolved path for this instance */
    bool enabled;
    char board_id[PARTS_UI_MAX_SHORT];        /* optional board/profile association */
    char profile_id[PARTS_UI_MAX_NAME];       /* optional profile association */

    /* Effective wiring/claims for this instance (validated/resolved). */
    parts_ui_claim_t wiring[PARTS_UI_MAX_CLAIMS];
    int wiring_count;

    /* Instance runtime/UI state (data flags/strings, no host runtime). */
    parts_ui_support_state_t state;
    bool attached;
    bool needs_sync;                          /* pending apply to profile/cosim host */
    uint32_t generation;
    char status_text[PARTS_UI_MAX_TEXT];
    char last_error[PARTS_UI_MAX_TEXT];

    /* Cosim/runtime integration intent (module-side, no execution). */
    char cosim_board_path[PARTS_UI_MAX_PATH]; /* associated board JSON path */
    char runtime_cmd[PARTS_UI_MAX_TEXT];      /* suggested/recorded command intent */
    char attach_intent[PARTS_UI_MAX_SHORT];   /* e.g. none|tcp|control|board */
    char attach_target[PARTS_UI_MAX_TEXT];    /* endpoint/path target */
    bool attach_requested;
} parts_ui_instance_t;

typedef struct {
    parts_ui_diag_sev_t severity;
    char code[PARTS_UI_MAX_SHORT];
    char subject[PARTS_UI_MAX_NAME];      /* part/instance/resource id */
    char message[PARTS_UI_MAX_TEXT];
} parts_ui_diag_t;

typedef struct {
    parts_ui_diag_t items[PARTS_UI_MAX_DIAGS];
    int count;
    int errors;
    int warnings;
} parts_ui_report_t;

typedef struct {
    char kind[PARTS_UI_MAX_SHORT];        /* input|output|state|event */
    char instance_id[PARTS_UI_MAX_NAME];
    char name[PARTS_UI_MAX_NAME];
    char value[PARTS_UI_MAX_TEXT];        /* data/string payload */
    uint64_t timestamp_ms;                /* optional, 0 if unset */
} parts_ui_event_t;

typedef struct {
    parts_ui_part_meta_t parts[PARTS_UI_MAX_PARTS];
    int part_count;

    parts_ui_instance_t instances[PARTS_UI_MAX_INSTANCES];
    int instance_count;

    parts_ui_event_t events[PARTS_UI_MAX_EVENTS];
    int event_head; /* ring buffer head */
    int event_count;

    /* Host/tool capability hints driving support state classification. */
    bool host_cosim_tcp_available;
    bool host_cosim_board_available;
    bool esp_emu_has_wasm_parts;          /* conditional(version/tool) if false */
    bool esp_emu_has_control_part_cmds;
    bool esp_emu_has_cosim_board_flag;    /* esp-emu 1.1.0: --cosim-board supported */
    bool esp_emu_has_cosim_tcp_flag;      /* esp-emu 1.1.0: --cosim-tcp supported */
    char esp_emu_version[PARTS_UI_MAX_SHORT];
    char esp_emu_binary[PARTS_UI_MAX_PATH];

    /* Board/management association. */
    char cosim_board_path[PARTS_UI_MAX_PATH];
    bool cosim_board_associated;
} parts_ui_context_t;

/* Future integration hooks (optional). */
typedef int (*parts_ui_emit_cosim_json_cb)(void *user, const char *instance_id,
                                           const char *json_fragment, char *err, size_t errsz);
typedef int (*parts_ui_profile_ext_cb)(void *user, const char *profile_id,
                                       const char *namespace_name, const char *json_payload,
                                       char *err, size_t errsz);
typedef int (*parts_ui_runtime_io_cb)(void *user, const char *instance_id,
                                      const char *name, const char *value,
                                      char *out, size_t outsz);

typedef struct {
    parts_ui_emit_cosim_json_cb emit_cosim_json;
    parts_ui_profile_ext_cb     apply_profile_extension;
    parts_ui_runtime_io_cb      send_input;   /* placeholder for sim_host/cosim input */
    parts_ui_runtime_io_cb      poll_output;  /* placeholder for sim_host/cosim output/state */
    void *user;
} parts_ui_hooks_t;

/* --- lifecycle / utils --- */
void parts_ui_context_init(parts_ui_context_t *ctx);
void parts_ui_report_init(parts_ui_report_t *r);
void parts_ui_report_add(parts_ui_report_t *r, parts_ui_diag_sev_t sev,
                         const char *code, const char *subject, const char *fmt, ...);
const char *parts_ui_support_state_name(parts_ui_support_state_t st);
const char *parts_ui_claim_kind_name(parts_ui_claim_kind_t kind);

/* Discover .wasm parts in a directory (non-recursive). Reads best-effort sidecar
 * metadata from <stem>.json or <stem>.part.json next to the wasm. Returns number
 * of parts added (>=0), -1 on bad args/dir error. */
int parts_ui_discover_dir(parts_ui_context_t *ctx, const char *dir_path, parts_ui_report_t *report);

/* Discover from one or more paths (dir or .wasm file), semicolon/comma separated
 * list accepted for convenience. Returns number added, -1 on bad args. */
int parts_ui_discover_paths(parts_ui_context_t *ctx, const char *paths, parts_ui_report_t *report);

/* Add/update instance wiring/config for a discovered part. Copies claims from meta
 * when wiring_count==0 and meta exists. Returns 0 on success, -1 on error/full. */
int parts_ui_configure_instance(parts_ui_context_t *ctx, const char *instance_id,
                                const char *part_name, const char *board_id,
                                const char *profile_id, const parts_ui_claim_t *wiring,
                                int wiring_count, bool enabled, parts_ui_report_t *report);

/* Remove instance by id. Returns 0 if removed, -1 if not found. */
int parts_ui_remove_instance(parts_ui_context_t *ctx, const char *instance_id);

/* Validate claims/wiring conflicts (GPIO, I2C addr, SPI CS, UART, PWM, ADC, EN)
 * across enabled instances and classify support/state. Returns 0 if no errors,
 * -1 if errors found. */
int parts_ui_validate(parts_ui_context_t *ctx, parts_ui_report_t *report);

/* Compute support state for a part/instance given context hints. */
parts_ui_support_state_t parts_ui_classify_support(const parts_ui_context_t *ctx,
                                                   const parts_ui_part_meta_t *meta);

/* UI event/state helpers (data/strings/flags only). */
int parts_ui_push_event(parts_ui_context_t *ctx, const char *kind, const char *instance_id,
                        const char *name, const char *value, uint64_t timestamp_ms);
int parts_ui_set_instance_state(parts_ui_context_t *ctx, const char *instance_id,
                                parts_ui_support_state_t st, bool attached,
                                const char *status_text, const char *last_error);

/* Hooks/placeholders for future profile extensions and sim_host/cosim integration.
 * These only format/dispatch data via callbacks when provided. */
int parts_ui_apply_profile_extensions(parts_ui_context_t *ctx, const parts_ui_hooks_t *hooks,
                                      const char *profile_id, parts_ui_report_t *report);
int parts_ui_emit_cosim_requests(parts_ui_context_t *ctx, const parts_ui_hooks_t *hooks,
                                 parts_ui_report_t *report);
int parts_ui_dispatch_input(parts_ui_context_t *ctx, const parts_ui_hooks_t *hooks,
                            const char *instance_id, const char *name, const char *value,
                            parts_ui_report_t *report);
int parts_ui_collect_outputs(parts_ui_context_t *ctx, const parts_ui_hooks_t *hooks,
                             parts_ui_report_t *report);

/* Integration/management helpers (no runtime execution). */
void parts_ui_set_context_capabilities(parts_ui_context_t *ctx,
                                       const char *esp_emu_version,
                                       const char *esp_emu_binary,
                                       bool has_wasm_parts,
                                       bool has_control_part_cmds,
                                       bool has_cosim_board_flag,
                                       bool has_cosim_tcp_flag,
                                       bool host_cosim_tcp_available,
                                       bool host_cosim_board_available);
void parts_ui_set_cosim_board_path(parts_ui_context_t *ctx, const char *path, bool associated);
int parts_ui_set_instance_runtime_intent(parts_ui_context_t *ctx, const char *instance_id,
                                         const char *attach_intent, const char *attach_target,
                                         bool attach_requested, const char *runtime_cmd,
                                         const char *cosim_board_path);
int parts_ui_format_cosim_board_json(const parts_ui_context_t *ctx, char *buf, size_t buflen,
                                     int only_enabled, parts_ui_report_t *report);
int parts_ui_describe_external_steps(const parts_ui_context_t *ctx, char *buf, size_t buflen,
                                     int include_validation);
int parts_ui_build_cosim_cli_args(const parts_ui_context_t *ctx, const char *instance_id_or_null,
                                  char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* SIM_LAUNCHER_PARTS_UI_H */
