#ifndef SIM_EMU_CAPS_PROBE_H
#define SIM_EMU_CAPS_PROBE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMU_CAPS_MAX_PATH       512
#define EMU_CAPS_MAX_VERSION    64
#define EMU_CAPS_MAX_SUMMARY    1024
#define EMU_CAPS_MAX_LINE       512

typedef enum {
    EMU_CAP_STATE_UNKNOWN = 0,
    EMU_CAP_STATE_CONDITIONAL,     /* conditional(version/tool) */
    EMU_CAP_STATE_SUPPORTED_NOW,   /* supported-now */
    EMU_CAP_STATE_NEEDS_RUNTIME_HOST, /* needs-runtime-host */
    EMU_CAP_STATE_BLOCKED_BY_EMU   /* blocked-by-emu */
} emu_cap_state_t;

typedef enum {
    EMU_CAP_FEATURE_SD = 0,        /* --sd / SDMMC/SDSPI storage */
    EMU_CAP_FEATURE_NET,           /* --net / network backend */
    EMU_CAP_FEATURE_GDB,           /* --gdb / GDB stub */
    EMU_CAP_FEATURE_TRACE,         /* --trace */
    EMU_CAP_FEATURE_PARTS,         /* virtual parts / esp-part / WASM parts */
    EMU_CAP_FEATURE_COSIM_BOARD,   /* --cosim-board */
    EMU_CAP_FEATURE_COSIM_TCP,     /* --cosim-tcp */
    EMU_CAP_FEATURE_I2S_WAV,       /* --i2s-wav */
    EMU_CAP_FEATURE_UART_TCP,      /* --uart-tcp */
    EMU_CAP_FEATURE_CONTROL_TCP,   /* --control-tcp */
    EMU_CAP_FEATURE_WIFI,          /* --wifi-* */
    EMU_CAP_FEATURE_BLE_HCI,       /* --ble-hci */
    EMU_CAP_FEATURE_COUNT
} emu_cap_feature_t;

typedef struct {
    bool initialized;
    char emu_path[EMU_CAPS_MAX_PATH];

    bool probed;
    int probe_status;              /* 0 ok, <0 process/read error */
    char version[EMU_CAPS_MAX_VERSION]; /* parsed from --version/help, may be empty */
    int version_major;
    int version_minor;
    int version_patch;
    bool version_known;

    /* Raw/normalized flags presence detected from help text */
    bool flag_sd;
    bool flag_net;
    bool flag_gdb;
    bool flag_trace;
    bool flag_parts;               /* textual hint: part/esp-part/wasm parts */
    bool flag_cosim_board;
    bool flag_cosim_tcp;
    bool flag_i2s_wav;
    bool flag_uart_tcp;
    bool flag_control_tcp;
    bool flag_wifi;
    bool flag_ble_hci;

    /* Cached help text captured during probe (truncated) for debugging/tests */
    char help_sample[EMU_CAPS_MAX_SUMMARY];
} emu_caps_probe_t;

/* Lifecycle */
void emu_caps_probe_init(emu_caps_probe_t *p);
int emu_caps_probe_set_path(emu_caps_probe_t *p, const char *emu_path);
const char *emu_caps_probe_get_path(const emu_caps_probe_t *p);

/* Probing:
 * - probe_path: runs `<emu_path> --help` and `<emu_path> --version` via popen and
 *   merges textual analysis. Uses `timeout` command when available for a simple
 *   bound; otherwise reads until EOF.
 * - probe_text: testable injection path (help and/or version text; NULL/empty ok).
 * Returns 0 on successful analysis (even if command missing), <0 on bad args. */
int emu_caps_probe_path(emu_caps_probe_t *p);
int emu_caps_probe_text(emu_caps_probe_t *p, const char *help_text, const char *version_text);

/* Queries */
bool emu_caps_probe_is_probed(const emu_caps_probe_t *p);
const char *emu_caps_probe_version(const emu_caps_probe_t *p);
bool emu_caps_probe_has_flag(const emu_caps_probe_t *p, emu_cap_feature_t feature);
emu_cap_state_t emu_caps_probe_feature_state(const emu_caps_probe_t *p, emu_cap_feature_t feature);

/* Names/helpers */
const char *emu_cap_feature_name(emu_cap_feature_t feature);
const char *emu_cap_state_name(emu_cap_state_t state);

/* Human summary into caller buffer. Returns number of chars written (excluding NUL). */
int emu_caps_probe_summary(const emu_caps_probe_t *p, char *out, size_t outsz);

#ifdef __cplusplus
}
#endif

#endif /* SIM_EMU_CAPS_PROBE_H */
