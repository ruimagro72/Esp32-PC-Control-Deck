#define _POSIX_C_SOURCE 200809L

#include "emu_caps_probe.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define EMU_CAPS_CAPTURE_MAX (64 * 1024)

static void safe_copy(char *dst, size_t dstsz, const char *src) {
    size_t n;
    if (!dst || dstsz == 0) return;
    if (!src) src = "";
    n = strlen(src);
    if (n >= dstsz) n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* POSIX sh single-quote: 'abc' -> 'abc', a'b -> 'a'\''b'. Caller frees. NULL on out of memory. */
static char *shell_quote_alloc(const char *src) {
    size_t i, n = 3;
    char *q, *w;
    if (!src) src = "";
    for (i = 0; src[i]; i++) n += (src[i] == '\'') ? 4 : 1;
    q = malloc(n);
    if (!q) return NULL;
    w = q;
    *w++ = '\'';
    for (i = 0; src[i]; i++) {
        if (src[i] == '\'') {
            memcpy(w, "'\\''", 4);
            w += 4;
        } else {
            *w++ = src[i];
        }
    }
    *w++ = '\'';
    *w = '\0';
    return q;
}

static void append_str(char *dst, size_t dstsz, const char *src) {
    size_t dl, sl;
    if (!dst || dstsz == 0 || !src) return;
    dl = strlen(dst);
    if (dl >= dstsz - 1) return;
    sl = strlen(src);
    if (sl > dstsz - 1 - dl) sl = dstsz - 1 - dl;
    memcpy(dst + dl, src, sl);
    dst[dl + sl] = '\0';
}

static bool str_contains_ci(const char *hay, const char *needle) {
    size_t hn, nn;
    if (!hay || !needle) return false;
    hn = strlen(hay);
    nn = strlen(needle);
    if (nn == 0 || nn > hn) return nn == 0;
    for (size_t i = 0; i + nn <= hn; i++) {
        size_t j = 0;
        while (j < nn) {
            unsigned char a = (unsigned char)hay[i + j];
            unsigned char b = (unsigned char)needle[j];
            if (tolower(a) != tolower(b)) break;
            j++;
        }
        if (j == nn) return true;
    }
    return false;
}

void emu_caps_probe_init(emu_caps_probe_t *p) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->initialized = true;
    p->probe_status = -1;
}

int emu_caps_probe_set_path(emu_caps_probe_t *p, const char *emu_path) {
    if (!p || !p->initialized) return -1;
    safe_copy(p->emu_path, sizeof(p->emu_path), emu_path ? emu_path : "");
    /* changing path invalidates previous probe */
    p->probed = false;
    p->probe_status = -1;
    p->version[0] = '\0';
    p->version_major = p->version_minor = p->version_patch = 0;
    p->version_known = false;
    p->flag_sd = p->flag_net = p->flag_gdb = p->flag_trace = false;
    p->flag_parts = p->flag_cosim_board = p->flag_cosim_tcp = false;
    p->flag_i2s_wav = p->flag_uart_tcp = p->flag_control_tcp = false;
    p->flag_wifi = p->flag_ble_hci = false;
    p->help_sample[0] = '\0';
    return 0;
}

const char *emu_caps_probe_get_path(const emu_caps_probe_t *p) {
    if (!p) return "";
    return p->emu_path;
}

static bool file_exists_exec(const char *path) {
    struct stat st;
    if (!path || !path[0]) return false;
    if (stat(path, &st) != 0) return false;
    if (!S_ISREG(st.st_mode)) return false;
    return access(path, X_OK) == 0;
}

static bool command_exists(const char *name) {
    char cmd[256];
    int rc;
    if (!name || !name[0]) return false;
    snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", name);
    rc = system(cmd);
    return (rc == 0);
}

static int run_capture(const char *cmd, char *out, size_t outsz) {
    FILE *fp;
    size_t total = 0;
    char buf[EMU_CAPS_MAX_LINE];
    if (!cmd || !out || outsz == 0) return -1;
    out[0] = '\0';
    fp = popen(cmd, "r");
    if (!fp) return -1;
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        size_t len = strlen(buf);
        if (total + 1 >= outsz) break;
        if (len > outsz - 1 - total) len = outsz - 1 - total;
        memcpy(out + total, buf, len);
        total += len;
        out[total] = '\0';
        if (total >= EMU_CAPS_CAPTURE_MAX - 1) break;
    }
    (void)pclose(fp);
    return 0;
}

static void parse_version(emu_caps_probe_t *p, const char *text) {
    const char *s;
    int maj = 0, min = 0, pat = 0;
    if (!p || !text) return;
    s = text;
    while (*s) {
        const char *cur = s;
        /* look for patterns like "1.1.0" or "esp-emu 1.1.0" */
        while (*cur && !isdigit((unsigned char)*cur)) cur++;
        if (!*cur) break;
        if (sscanf(cur, "%d.%d.%d", &maj, &min, &pat) == 3) {
            /* prefer first version-looking token that follows esp-emu or any plausible one */
            snprintf(p->version, sizeof(p->version), "%d.%d.%d", maj, min, pat);
            p->version_major = maj;
            p->version_minor = min;
            p->version_patch = pat;
            p->version_known = true;
            return;
        }
        /* advance past digits to avoid infinite loop */
        while (*cur && (isdigit((unsigned char)*cur) || *cur == '.')) cur++;
        s = cur;
    }
}

static bool has_flag_token(const char *text, const char *flag) {
    /* naive textual detection; adequate for --help output */
    return str_contains_ci(text, flag);
}

static void analyze_text(emu_caps_probe_t *p, const char *help_text, const char *version_text) {
    const char *ht = help_text ? help_text : "";
    const char *vt = version_text ? version_text : "";
    char merged[EMU_CAPS_CAPTURE_MAX];
    if (!p) return;

    merged[0] = '\0';
    append_str(merged, sizeof(merged), vt);
    if (merged[0] && ht[0]) append_str(merged, sizeof(merged), "\n");
    append_str(merged, sizeof(merged), ht);

    if (!p->version_known) parse_version(p, vt);
    if (!p->version_known) parse_version(p, ht);
    if (!p->version_known) parse_version(p, merged);

    p->flag_sd = has_flag_token(merged, "--sd");
    p->flag_net = has_flag_token(merged, "--net");
    p->flag_gdb = has_flag_token(merged, "--gdb");
    p->flag_trace = has_flag_token(merged, "--trace");
    p->flag_cosim_board = has_flag_token(merged, "--cosim-board");
    p->flag_cosim_tcp = has_flag_token(merged, "--cosim-tcp");
    p->flag_i2s_wav = has_flag_token(merged, "--i2s-wav");
    p->flag_uart_tcp = has_flag_token(merged, "--uart-tcp");
    p->flag_control_tcp = has_flag_token(merged, "--control-tcp");
    p->flag_wifi = has_flag_token(merged, "--wifi");
    p->flag_ble_hci = has_flag_token(merged, "--ble-hci");
    p->flag_parts = has_flag_token(merged, "esp-part") || has_flag_token(merged, "wasm part") ||
                    has_flag_token(merged, "virtual part") || has_flag_token(merged, "--part");

    /* keep a truncated sample for summary/debug */
    safe_copy(p->help_sample, sizeof(p->help_sample), merged);
}

int emu_caps_probe_text(emu_caps_probe_t *p, const char *help_text, const char *version_text) {
    if (!p || !p->initialized) return -1;
    p->probed = true;
    p->probe_status = 0;
    /* reset detections but keep path */
    p->version[0] = '\0';
    p->version_major = p->version_minor = p->version_patch = 0;
    p->version_known = false;
    p->flag_sd = p->flag_net = p->flag_gdb = p->flag_trace = false;
    p->flag_parts = p->flag_cosim_board = p->flag_cosim_tcp = false;
    p->flag_i2s_wav = p->flag_uart_tcp = p->flag_control_tcp = false;
    p->flag_wifi = p->flag_ble_hci = false;
    p->help_sample[0] = '\0';
    analyze_text(p, help_text, version_text);
    return 0;
}

int emu_caps_probe_path(emu_caps_probe_t *p) {
    char help_buf[EMU_CAPS_CAPTURE_MAX];
    char ver_buf[4096];
    char cmd[EMU_CAPS_MAX_PATH * 2 + 64];
    char *qpath;
    bool use_timeout;
    int n;
    int rc = 0;
    if (!p || !p->initialized) return -1;

    help_buf[0] = '\0';
    ver_buf[0] = '\0';
    p->probed = true;
    p->probe_status = 0;

    if (!p->emu_path[0] || !file_exists_exec(p->emu_path)) {
        /* analyze empty text; keep probed with error status */
        p->probe_status = -2;
        emu_caps_probe_text(p, "", "");
        p->probe_status = -2;
        return 0;
    }

    use_timeout = command_exists("timeout");
    qpath = shell_quote_alloc(p->emu_path);
    if (!qpath) {
        emu_caps_probe_text(p, "", "");
        p->probe_status = -3;
        return 0;
    }

    n = use_timeout
            ? snprintf(cmd, sizeof(cmd), "timeout 3 %s --help 2>&1", qpath)
            : snprintf(cmd, sizeof(cmd), "%s --help 2>&1", qpath);
    if (n < 0 || (size_t)n >= sizeof(cmd) || run_capture(cmd, help_buf, sizeof(help_buf)) != 0) rc = -3;

    n = use_timeout
            ? snprintf(cmd, sizeof(cmd), "timeout 3 %s --version 2>&1", qpath)
            : snprintf(cmd, sizeof(cmd), "%s --version 2>&1", qpath);
    if (n < 0 || (size_t)n >= sizeof(cmd) || run_capture(cmd, ver_buf, sizeof(ver_buf)) != 0) {
        if (rc == 0) rc = -3;
    }

    free(qpath);
    emu_caps_probe_text(p, help_buf, ver_buf);
    p->probe_status = rc;
    return 0;
}

bool emu_caps_probe_is_probed(const emu_caps_probe_t *p) {
    return p && p->probed;
}

const char *emu_caps_probe_version(const emu_caps_probe_t *p) {
    if (!p || !p->version[0]) return "";
    return p->version;
}

bool emu_caps_probe_has_flag(const emu_caps_probe_t *p, emu_cap_feature_t feature) {
    if (!p) return false;
    switch (feature) {
        case EMU_CAP_FEATURE_SD: return p->flag_sd;
        case EMU_CAP_FEATURE_NET: return p->flag_net;
        case EMU_CAP_FEATURE_GDB: return p->flag_gdb;
        case EMU_CAP_FEATURE_TRACE: return p->flag_trace;
        case EMU_CAP_FEATURE_PARTS: return p->flag_parts;
        case EMU_CAP_FEATURE_COSIM_BOARD: return p->flag_cosim_board;
        case EMU_CAP_FEATURE_COSIM_TCP: return p->flag_cosim_tcp;
        case EMU_CAP_FEATURE_I2S_WAV: return p->flag_i2s_wav;
        case EMU_CAP_FEATURE_UART_TCP: return p->flag_uart_tcp;
        case EMU_CAP_FEATURE_CONTROL_TCP: return p->flag_control_tcp;
        case EMU_CAP_FEATURE_WIFI: return p->flag_wifi;
        case EMU_CAP_FEATURE_BLE_HCI: return p->flag_ble_hci;
        default: return false;
    }
}

emu_cap_state_t emu_caps_probe_feature_state(const emu_caps_probe_t *p, emu_cap_feature_t feature) {
    bool has;
    if (!p) return EMU_CAP_STATE_UNKNOWN;

    has = emu_caps_probe_has_flag(p, feature);
    switch (feature) {
        case EMU_CAP_FEATURE_SD:
            /* esp-emu 1.1.0 advertises --sd and mounts the card image with it (verified).
             * A successful probe without --sd means the card is not modelled. */
            if (has) {
                bool ge_1_1 = p->version_known &&
                              (p->version_major > 1 ||
                               (p->version_major == 1 && p->version_minor >= 1));
                return ge_1_1 ? EMU_CAP_STATE_SUPPORTED_NOW : EMU_CAP_STATE_CONDITIONAL;
            }
            /* No --sd in a successful probe means blocked; a failed probe (missing binary) is unknown. */
            return (p->probed && p->probe_status == 0) ? EMU_CAP_STATE_BLOCKED_BY_EMU : EMU_CAP_STATE_UNKNOWN;

        case EMU_CAP_FEATURE_NET:
        case EMU_CAP_FEATURE_GDB:
        case EMU_CAP_FEATURE_TRACE:
        case EMU_CAP_FEATURE_I2S_WAV:
        case EMU_CAP_FEATURE_UART_TCP:
        case EMU_CAP_FEATURE_CONTROL_TCP:
        case EMU_CAP_FEATURE_WIFI:
        case EMU_CAP_FEATURE_BLE_HCI:
        case EMU_CAP_FEATURE_COSIM_BOARD:
        case EMU_CAP_FEATURE_COSIM_TCP:
            if (has) return EMU_CAP_STATE_CONDITIONAL;
            return p->probed ? EMU_CAP_STATE_UNKNOWN : EMU_CAP_STATE_UNKNOWN;

        case EMU_CAP_FEATURE_PARTS:
            /* WASM/esp-part integration depends on runtime host/tooling; textual hints
             * only indicate potential support. */
            if (has) return EMU_CAP_STATE_NEEDS_RUNTIME_HOST;
            return p->probed ? EMU_CAP_STATE_NEEDS_RUNTIME_HOST : EMU_CAP_STATE_UNKNOWN;

        default:
            return EMU_CAP_STATE_UNKNOWN;
    }
}

const char *emu_cap_feature_name(emu_cap_feature_t feature) {
    switch (feature) {
        case EMU_CAP_FEATURE_SD: return "sd";
        case EMU_CAP_FEATURE_NET: return "net";
        case EMU_CAP_FEATURE_GDB: return "gdb";
        case EMU_CAP_FEATURE_TRACE: return "trace";
        case EMU_CAP_FEATURE_PARTS: return "parts";
        case EMU_CAP_FEATURE_COSIM_BOARD: return "cosim-board";
        case EMU_CAP_FEATURE_COSIM_TCP: return "cosim-tcp";
        case EMU_CAP_FEATURE_I2S_WAV: return "i2s-wav";
        case EMU_CAP_FEATURE_UART_TCP: return "uart-tcp";
        case EMU_CAP_FEATURE_CONTROL_TCP: return "control-tcp";
        case EMU_CAP_FEATURE_WIFI: return "wifi";
        case EMU_CAP_FEATURE_BLE_HCI: return "ble-hci";
        default: return "unknown";
    }
}

const char *emu_cap_state_name(emu_cap_state_t state) {
    switch (state) {
        case EMU_CAP_STATE_CONDITIONAL: return "conditional(version/tool)";
        case EMU_CAP_STATE_SUPPORTED_NOW: return "supported-now";
        case EMU_CAP_STATE_NEEDS_RUNTIME_HOST: return "needs-runtime-host";
        case EMU_CAP_STATE_BLOCKED_BY_EMU: return "blocked-by-emu";
        default: return "unknown";
    }
}

int emu_caps_probe_summary(const emu_caps_probe_t *p, char *out, size_t outsz) {
    char line[EMU_CAPS_MAX_PATH + 128];
    if (!out || outsz == 0) return 0;
    out[0] = '\0';
    if (!p) return 0;

    snprintf(line, sizeof(line), "emu_caps: path=%s probed=%s status=%d version=%s\n",
             p->emu_path[0] ? p->emu_path : "(unset)",
             p->probed ? "yes" : "no",
             p->probe_status,
             p->version_known ? p->version : "(unknown)");
    append_str(out, outsz, line);

    for (int i = 0; i < (int)EMU_CAP_FEATURE_COUNT; i++) {
        emu_cap_feature_t f = (emu_cap_feature_t)i;
        snprintf(line, sizeof(line), " - %s: flag=%s state=%s\n",
                 emu_cap_feature_name(f),
                 emu_caps_probe_has_flag(p, f) ? "yes" : "no",
                 emu_cap_state_name(emu_caps_probe_feature_state(p, f)));
        append_str(out, outsz, line);
    }
    if (!p->version_known && p->probed) {
        append_str(out, outsz, "note: version unknown; SD state follows the --sd flag only\n");
    }
    return (int)strlen(out);
}
