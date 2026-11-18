#define _GNU_SOURCE
#include "validate.h"
#include "config.h"
#include "i18n.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void v_set(char *dst, size_t dstsz, const char *src) {
    if (!dst || dstsz == 0) return;
    if (!src) src = "";
    snprintf(dst, dstsz, "%s", src);
}

void validate_report_init(validate_report_t *report) {
    if (!report) return;
    memset(report, 0, sizeof(*report));
}

void validate_report_clear(validate_report_t *report) {
    validate_report_init(report);
}

int validate_report_add(validate_report_t *report, const char *code,
                        validate_severity_t severity, const char *field,
                        const char *message, const char *suggestion) {
    if (!report) return -1;
    if (report->count >= VALIDATE_MAX_RESULTS) {
        report->truncated = true;
        return -1;
    }
    validate_result_t *r = &report->items[report->count++];
    memset(r, 0, sizeof(*r));
    v_set(r->code, sizeof(r->code), code);
    r->severity = severity;
    v_set(r->field, sizeof(r->field), field);
    v_set(r->message, sizeof(r->message), message);
    v_set(r->suggestion, sizeof(r->suggestion), suggestion);
    return report->count - 1;
}

int validate_report_count(const validate_report_t *report) {
    return report ? report->count : 0;
}

const validate_result_t *validate_report_get(const validate_report_t *report, int index) {
    if (!report || index < 0 || index >= report->count) return NULL;
    return &report->items[index];
}

bool validate_report_has_errors(const validate_report_t *report) {
    if (!report) return false;
    for (int i = 0; i < report->count; i++) {
        if (report->items[i].severity == VALIDATE_SEV_ERROR) return true;
    }
    return false;
}

bool validate_report_has_warnings(const validate_report_t *report) {
    if (!report) return false;
    for (int i = 0; i < report->count; i++) {
        if (report->items[i].severity == VALIDATE_SEV_WARN) return true;
    }
    return false;
}

const char *validate_severity_name(validate_severity_t severity) {
    switch (severity) {
        case VALIDATE_SEV_ERROR: return "error";
        case VALIDATE_SEV_WARN:  return "warn";
        case VALIDATE_SEV_INFO:  return "info";
        default: return "info";
    }
}

bool validate_path_exists(const char *path) {
    if (!path || !path[0]) return false;
    struct stat st;
    return stat(path, &st) == 0;
}

static bool is_abs_path(const char *path) {
    return path && path[0] == '/';
}

bool validate_path_exists_in_repo(const char *repo_root, const char *path, char *resolved, size_t resolved_len) {
    if (!path || !path[0]) return false;
    char buf[PATH_MAX];
    if (is_abs_path(path) || !repo_root || !repo_root[0]) {
        snprintf(buf, sizeof(buf), "%s", path);
    } else {
        snprintf(buf, sizeof(buf), "%s/%s", repo_root, path);
    }
    if (resolved && resolved_len > 0) {
        snprintf(resolved, resolved_len, "%s", buf);
    }
    return validate_path_exists(buf);
}

static bool is_executable_file(const char *path) {
    if (!path || !path[0]) return false;
    return access(path, X_OK) == 0;
}

bool validate_find_in_path(const char *name, char *out, size_t out_len) {
    if (!name || !name[0]) return false;
    if (strchr(name, '/')) {
        if (is_executable_file(name)) {
            if (out && out_len) snprintf(out, out_len, "%s", name);
            return true;
        }
        return false;
    }
    const char *path_env = getenv("PATH");
    if (!path_env || !path_env[0]) return false;
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path_env);
    char *save = NULL;
    for (char *tok = strtok_r(tmp, ":", &save); tok; tok = strtok_r(NULL, ":", &save)) {
        char cand[PATH_MAX];
        snprintf(cand, sizeof(cand), "%s/%s", tok, name);
        if (is_executable_file(cand)) {
            if (out && out_len) snprintf(out, out_len, "%s", cand);
            return true;
        }
    }
    return false;
}

static char *ltrim(char *s) {
    while (s && *s && isspace((unsigned char)*s)) s++;
    return s;
}

static void rtrim_inplace(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

bool validate_parse_host_port(const char *host_port, char *host, size_t host_len, int *out_port) {
    if (host && host_len) host[0] = '\0';
    if (out_port) *out_port = -1;
    if (!host_port || !host_port[0]) return false;
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s", host_port);
    char *s = ltrim(tmp);
    rtrim_inplace(s);
    char *colon = strrchr(s, ':');
    if (!colon || colon == s || *(colon + 1) == '\0') return false;
    *colon = '\0';
    char *port_s = colon + 1;
    char *end = NULL;
    errno = 0;
    long p = strtol(port_s, &end, 10);
    if (errno != 0 || end == port_s || (end && *ltrim(end) != '\0')) return false;
    if (p < 1 || p > 65535) return false;
    if (host && host_len) snprintf(host, host_len, "%s", s);
    if (out_port) *out_port = (int)p;
    return true;
}

bool validate_port_plausible_str(const char *host_port, int *out_port) {
    int port = -1;
    if (!validate_parse_host_port(host_port, NULL, 0, &port)) return false;
    if (out_port) *out_port = port;
    return port >= 1 && port <= 65535;
}

static void resolve_repo_path(const char *repo_root, const char *path, char *out, size_t out_len) {
    if (!out || out_len == 0) return;
    out[0] = '\0';
    if (!path || !path[0]) return;
    if (is_abs_path(path) || !repo_root || !repo_root[0]) {
        snprintf(out, out_len, "%s", path);
    } else {
        snprintf(out, out_len, "%s/%s", repo_root, path);
    }
}

static void add_tool_found(validate_report_t *report, const char *field, const char *name, const char *path) {
    char msg[VALIDATE_MAX_TEXT];
    snprintf(msg, sizeof(msg), "%s found: %.200s", name, path ? path : "(ok)");
    validate_report_add(report, "TOOL_FOUND", VALIDATE_SEV_INFO, field, msg, "");
}

static void add_tool_missing(validate_report_t *report, const char *code, const char *field, const char *name, const char *suggestion) {
    char msg[VALIDATE_MAX_TEXT];
    snprintf(msg, sizeof(msg), "%s not found (optional by context)", name);
    validate_report_add(report, code, VALIDATE_SEV_WARN, field, msg, suggestion);
}

static int run_cmd_status(const char *cmd) {
    if (!cmd || !cmd[0]) return -1;
    int st = system(cmd);
    if (st == -1) return -1;
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    return -1;
}

static bool check_sdl2_pkgconfig(void) {
    /* Suppress output; presence/version query is enough as optional capability check. */
    int rc = run_cmd_status("pkg-config --exists sdl2 >/dev/null 2>&1");
    return rc == 0;
}

/* --- T20: storage/audio/net/capabilities helpers --- */

static void slfs_caps_version(const validate_extra_t *extra, int *maj, int *min, int *pat, bool *known) {
    *maj = 1; *min = 1; *pat = 0; *known = false;
    if (extra && extra->emu_caps && emu_caps_probe_is_probed(extra->emu_caps) &&
        extra->emu_caps->version_known) {
        *maj = extra->emu_caps->version_major;
        *min = extra->emu_caps->version_minor;
        *pat = extra->emu_caps->version_patch;
        *known = true;
    }
}

static const char *slfs_state_for_report(slfs_capability_t cap) {
    return slfs_capability_str(cap);
}

static void validate_storage_section(const launcher_config_t *cfg, const validate_extra_t *extra,
                                     const char *repo_root, unsigned flags,
                                     validate_report_t *report) {
    slfs_config_t sc = cfg->storage;
    int maj, min, pat; bool ver_known;
    slfs_caps_version(extra, &maj, &min, &pat, &ver_known);
    /* All launcher board presets are ESP32-S3 targets in this repo. */
    (void)slfs_apply_capabilities(&sc, maj, min, pat, true);

    if (sc.littlefs.enabled) {
        char state[VALIDATE_MAX_TEXT];
        snprintf(state, sizeof(state), "LittleFS: %s", slfs_state_for_report(sc.littlefs.capability));
        validate_report_add(report, "SLFS_LITTLEFS_STATE", VALIDATE_SEV_INFO,
                            "storage.littlefs.image_path", state,
                            "Image/offset/size must match the partition table (conditional)");
    }

    /* Resolve image path against repo root for existence checks (helper validates absolute/relative). */
    slfs_config_t sc_check = sc;
    char resolved[PATH_MAX];
    if (sc_check.littlefs.image_path[0]) {
        if (is_abs_path(sc_check.littlefs.image_path) || !repo_root || !repo_root[0])
            snprintf(resolved, sizeof(resolved), "%s", sc_check.littlefs.image_path);
        else
            snprintf(resolved, sizeof(resolved), "%s/%s", repo_root, sc_check.littlefs.image_path);
        snprintf(sc_check.littlefs.image_path, sizeof(sc_check.littlefs.image_path), "%.*s",
                 (int)(sizeof(sc_check.littlefs.image_path) - 1), resolved);
    }
    if (sc_check.sd.image_path[0]) {
        if (is_abs_path(sc_check.sd.image_path) || !repo_root || !repo_root[0])
            snprintf(resolved, sizeof(resolved), "%s", sc_check.sd.image_path);
        else
            snprintf(resolved, sizeof(resolved), "%s/%s", repo_root, sc_check.sd.image_path);
        snprintf(sc_check.sd.image_path, sizeof(sc_check.sd.image_path), "%.*s",
                 (int)(sizeof(sc_check.sd.image_path) - 1), resolved);
    }

    slfs_report_t sr;
    bool check_paths = (flags & VALIDATE_CHECK_FILES) != 0;
    if (slfs_validate(&sc_check, check_paths, &sr) == 0) {
        if (sr.errors[0]) {
            char msg[VALIDATE_MAX_TEXT];
            snprintf(msg, sizeof(msg), "LittleFS/SD: %.200s", sr.errors);
            validate_report_add(report, "SLFS_ERROR", VALIDATE_SEV_ERROR,
                                "storage.littlefs.image_path", msg,
                                "Fix LittleFS image/partition/mount or disable LittleFS");
        }
        if (sr.warnings[0]) {
            char msg[VALIDATE_MAX_TEXT];
            snprintf(msg, sizeof(msg), "LittleFS/SD: %.200s", sr.warnings);
            validate_report_add(report, "SLFS_WARN", VALIDATE_SEV_WARN,
                                "storage.littlefs.image_path", msg,
                                "Provide a valid image or disable LittleFS; SD needs newer emu/tool");
        }
        if (sr.notes[0]) {
            char msg[VALIDATE_MAX_TEXT];
            snprintf(msg, sizeof(msg), "LittleFS/SD note: %.200s", sr.notes);
            validate_report_add(report, "SLFS_NOTE", VALIDATE_SEV_INFO,
                                "storage.littlefs", msg, "");
        }
    }

    if (sc.littlefs.enabled && sc.littlefs.image_path[0] && check_paths) {
        char rp[PATH_MAX];
        if (validate_path_exists_in_repo(repo_root, sc.littlefs.image_path, rp, sizeof(rp))) {
            char msg[VALIDATE_MAX_TEXT];
            snprintf(msg, sizeof(msg), "LittleFS image exists: %.200s", sc.littlefs.image_path);
            validate_report_add(report, "SLFS_IMAGE_OK", VALIDATE_SEV_INFO,
                                "storage.littlefs.image_path", msg, "");
        }
    }

    bool sd_requested = (extra && extra->sd_requested) || sc.sd.enabled;
    if (sd_requested) {
        char msg[VALIDATE_MAX_TEXT];
        const char *note = sc.sd.capability_note[0] ? sc.sd.capability_note : "no note";
        snprintf(msg, sizeof(msg), "SD state: %s (%.180s)",
                 slfs_state_for_report(sc.sd.capability), note);
        validate_report_add(report, "SLFS_SD_STATE", VALIDATE_SEV_INFO,
                            "storage.sd_card", msg,
                            sc.sd.capability == SLFS_CAP_CONDITIONAL ?
                                "Probe esp-emu (--help) to confirm --sd support" : "");
    }

    if (ver_known) {
        char msg[VALIDATE_MAX_TEXT];
        snprintf(msg, sizeof(msg), "esp-emu probe version: %d.%d.%d (caps derived from probe)",
                 maj, min, pat);
        validate_report_add(report, "CAP_PROBE_VERSION", VALIDATE_SEV_INFO,
                            "emu_runtime.esp_emu_path", msg, "");
    } else {
        validate_report_add(report, "CAP_PROBE_FALLBACK", VALIDATE_SEV_INFO,
                            "emu_runtime.esp_emu_path",
                            "No esp-emu probe version; capability defaults assume ESP32-S3 / esp-emu 1.1.0",
                            "Probe esp-emu (--help/--version) or set emu_caps_path for accurate caps");
    }
}

static void add_audio_net_cap_state(validate_report_t *report, const char *code,
                                    const char *field, const char *label,
                                    audio_net_cap_state_t state, const char *note) {
    char msg[VALIDATE_MAX_TEXT];
    snprintf(msg, sizeof(msg), "%s: %s%s%s", label,
             audio_net_cap_state_str(state),
             note && note[0] ? " | " : "", note && note[0] ? note : "");
    validate_report_add(report, code,
                        state == AUDIO_NET_CAP_BLOCKED_BY_EMU ? VALIDATE_SEV_WARN : VALIDATE_SEV_INFO,
                        field, msg, "");
}

static void validate_audio_net_section(const launcher_config_t *cfg, const validate_extra_t *extra,
                                       const char *repo_root, unsigned flags,
                                       validate_report_t *report) {
    audio_net_config_t an = cfg->audio_net;
    if (!cfg->audio) {
        an.i2s_enabled = false;
        an.i2s_wav_out[0] = '\0';
        an.i2s_wav_in[0] = '\0';
        an.i2s_instance = -1;
    }
    if (cfg->net_user && !an.net_enabled) {
        an.net_enabled = true;
        snprintf(an.net_mode, sizeof(an.net_mode), "user");
    }

    char err[VALIDATE_MAX_TEXT];
    if (!audio_net_validate(&an, err, sizeof(err))) {
        char msg[VALIDATE_MAX_TEXT];
        snprintf(msg, sizeof(msg), "Audio/net config invalid: %.200s", err[0] ? err : "unknown");
        validate_report_add(report, "AUDIO_NET_INVALID", VALIDATE_SEV_ERROR,
                            "audio", msg, "Fix I2S WAV paths/instance or net mode/hostfwd/wifi/ble values");
    }

    const emu_caps_probe_t *p = (extra && extra->emu_caps && emu_caps_probe_is_probed(extra->emu_caps))
                                    ? extra->emu_caps : NULL;
    bool i2s_requested = an.i2s_enabled || an.i2s_wav_out[0] || an.i2s_wav_in[0];
    if (i2s_requested) {
        audio_net_capability_t c = audio_net_capability(AUDIO_NET_FEAT_I2S_WAV_OUT, &an);
        add_audio_net_cap_state(report, "AUDIO_I2S_STATE", "audio.i2s", "I2S WAV",
                                c.state, c.condition);
        if (p && !p->flag_i2s_wav) {
            validate_report_add(report, "AUDIO_I2S_FLAG_MISSING", VALIDATE_SEV_WARN,
                                "audio.i2s",
                                "esp-emu --help does not advertise --i2s-wav; audio args will be omitted",
                                "Use an esp-emu build with --i2s-wav or disable I2S WAV output");
        }
        if (an.i2s_wav_in[0]) {
            audio_net_capability_t ci = audio_net_capability(AUDIO_NET_FEAT_I2S_WAV_IN, &an);
            add_audio_net_cap_state(report, "AUDIO_I2S_IN_STATE", "audio.i2s_in", "I2S WAV in",
                                    ci.state, ci.condition);
            if (flags & VALIDATE_CHECK_FILES) {
                char rp[PATH_MAX];
                if (!validate_path_exists_in_repo(repo_root, an.i2s_wav_in, rp, sizeof(rp))) {
                    char msg[VALIDATE_MAX_TEXT];
                    snprintf(msg, sizeof(msg), "I2S WAV input not found: %.180s", an.i2s_wav_in);
                    validate_report_add(report, "AUDIO_IN_MISSING", VALIDATE_SEV_WARN,
                                        "audio.i2s_in", msg,
                                        "Provide an existing WAV input or clear I2S WAV in");
                }
            }
        }
        if (an.i2s_instance >= 0) {
            char msg[VALIDATE_MAX_TEXT];
            snprintf(msg, sizeof(msg), "I2S instance %d (conditional: flag/help support)", an.i2s_instance);
            validate_report_add(report, "AUDIO_I2S_INSTANCE", VALIDATE_SEV_INFO,
                                "audio.i2s_instance", msg, "");
        }
        if (an.i2s_realtime) {
            audio_net_capability_t cr = audio_net_capability(AUDIO_NET_FEAT_I2S_REALTIME, &an);
            add_audio_net_cap_state(report, "AUDIO_I2S_REALTIME", "audio.realtime",
                                    "I2S realtime", cr.state, cr.note);
        }
    }

    if (an.net_enabled) {
        audio_net_capability_t c = audio_net_capability(AUDIO_NET_FEAT_NET_USER, &an);
        add_audio_net_cap_state(report, "AUDIO_NET_STATE", "network_radio.net",
                                "net", c.state, c.condition);
        if (p && !p->flag_net) {
            validate_report_add(report, "AUDIO_NET_FLAG_MISSING", VALIDATE_SEV_WARN,
                                "network_radio.net",
                                "esp-emu --help does not advertise --net; net args will be omitted",
                                "Use an esp-emu build with --net");
        }
    }

    if (an.hostfwd_count > 0) {
        audio_net_capability_t c = audio_net_capability(AUDIO_NET_FEAT_NET_HOSTFWD, &an);
        add_audio_net_cap_state(report, "AUDIO_HOSTFWD_STATE", "network_radio.hostfwd",
                                "hostfwd", c.state, c.condition);
        if (p && !strstr(p->help_sample, "--hostfwd")) {
            validate_report_add(report, "AUDIO_HOSTFWD_UNSUPPORTED", VALIDATE_SEV_WARN,
                                "network_radio.hostfwd",
                                "esp-emu --help does not advertise --hostfwd; hostfwd rules are kept but not emitted",
                                "Remove hostfwd rules or use a newer esp-emu that supports --hostfwd");
        }
    }

    if (an.wifi_enabled) {
        audio_net_capability_t c = audio_net_capability(AUDIO_NET_FEAT_WIFI_PLACEHOLDER, &an);
        add_audio_net_cap_state(report, "AUDIO_WIFI_STATE", "network_radio.wifi",
                                "Wi-Fi placeholders", c.state, c.note);
        if (p && !p->flag_wifi) {
            validate_report_add(report, "AUDIO_WIFI_FLAGS_MISSING", VALIDATE_SEV_WARN,
                                "network_radio.wifi",
                                "esp-emu --help does not advertise --wifi-*; Wi-Fi args will be omitted",
                                "Use an esp-emu build with Wi-Fi flags or disable Wi-Fi placeholders");
        }
        size_t pw_len = strlen(an.wifi_password);
        if (an.wifi_auth != AUDIO_NET_WIFI_AUTH_OPEN && an.wifi_auth != AUDIO_NET_WIFI_AUTH_UNKNOWN &&
            (pw_len < 8 || pw_len > 63)) {
            validate_report_add(report, "AUDIO_WIFI_PASS_LEN", VALIDATE_SEV_ERROR, "network_radio.wifi",
                                _("Wi-Fi passphrase must be 8-63 characters (esp-emu)"),
                                _("Use an 8-63 character passphrase or set the network to open"));
        }
        if (an.wifi_auth == AUDIO_NET_WIFI_AUTH_OPEN && an.wifi_password[0]) {
            validate_report_add(report, "AUDIO_WIFI_OPEN_PASS", VALIDATE_SEV_WARN, "network_radio.wifi",
                                _("Open network: the passphrase is ignored"), "");
        }
    }

    if (an.ble_enabled) {
        audio_net_capability_t c = audio_net_capability(AUDIO_NET_FEAT_BLE_HCI, &an);
        add_audio_net_cap_state(report, "AUDIO_BLE_STATE", "network_radio.ble",
                                "BLE HCI", c.state, c.condition);
        if (p && !p->flag_ble_hci) {
            validate_report_add(report, "AUDIO_BLE_FLAG_MISSING", VALIDATE_SEV_WARN,
                                "network_radio.ble",
                                "esp-emu --help does not advertise --ble-hci; BLE args will be omitted",
                                "Use an esp-emu build with --ble-hci or disable BLE");
        }
    }

    (void)flags;
}

int validate_tools(const char *repo_root, unsigned flags, validate_report_t *report) {
    if (!report) return -1;
    if (!(flags & VALIDATE_CHECK_TOOLS)) return 0;

    char p[PATH_MAX];

    /* esp-emu (repo default location) */
    resolve_repo_path(repo_root, EMU_DEFAULT_PATH, p, sizeof(p));
    if (is_executable_file(p) || validate_path_exists(p)) {
        add_tool_found(report, "emu_runtime.esp_emu_path", "esp-emu", p);
    } else {
        add_tool_missing(report, "TOOL_MISSING_ESP_EMU", "emu_runtime.esp_emu_path", "esp-emu",
                         "Install/provide esp-emu binary or set emu_runtime.esp_emu_path");
    }

    /* lcd_touch_host */
    resolve_repo_path(repo_root, "simulator/emu/lcd_touch_host", p, sizeof(p));
    if (is_executable_file(p) || validate_path_exists(p)) {
        add_tool_found(report, "host.lcd_touch_host", "lcd_touch_host", p);
    } else {
        add_tool_missing(report, "TOOL_MISSING_LCD_TOUCH_HOST", "host.lcd_touch_host", "lcd_touch_host",
                         "Build host: ./start.sh emu build-host (SDL cosim UI)");
    }

    /* esptool: ~/.platformio/penv/bin/esptool or PATH */
    {
        const char *home = getenv("HOME");
        bool found = false;
        if (home && home[0]) {
            snprintf(p, sizeof(p), "%s/.platformio/penv/bin/esptool", home);
            if (is_executable_file(p)) { add_tool_found(report, "tools.esptool", "esptool", p); found = true; }
        }
        if (!found && validate_find_in_path("esptool", p, sizeof(p))) {
            add_tool_found(report, "tools.esptool", "esptool", p); found = true;
        }
        if (!found && validate_find_in_path("esptool.py", p, sizeof(p))) {
            add_tool_found(report, "tools.esptool", "esptool.py", p); found = true;
        }
        if (!found) {
            add_tool_missing(report, "TOOL_MISSING_ESPTOOL", "tools.esptool", "esptool",
                             "Install esptool in ~/.platformio/penv/bin or PATH (needed for merge/flash workflows)");
        }
    }

    /* socat or nc */
    {
        bool socat_ok = validate_find_in_path("socat", p, sizeof(p));
        bool nc_ok = false;
        char pnc[PATH_MAX] = "";
        if (socat_ok) add_tool_found(report, "tools.socat", "socat", p);
        nc_ok = validate_find_in_path("nc", pnc, sizeof(pnc));
        if (nc_ok) add_tool_found(report, "tools.nc", "nc", pnc);
        if (!socat_ok && !nc_ok) {
            add_tool_missing(report, "TOOL_MISSING_SOCAT_NC", "tools.socat_nc", "socat/nc",
                             "Install socat or nc for UART TCP console bridge (emu console/ui)");
        } else if (!socat_ok || !nc_ok) {
            char msg[VALIDATE_MAX_TEXT];
            snprintf(msg, sizeof(msg), "%s missing; %s available (optional)", socat_ok ? "nc" : "socat", socat_ok ? "socat" : "nc");
            validate_report_add(report, "TOOL_PARTIAL_SOCAT_NC", VALIDATE_SEV_INFO, "tools.socat_nc", msg, "");
        }
    }

    if (validate_find_in_path("gcc", p, sizeof(p))) add_tool_found(report, "tools.gcc", "gcc", p);
    else add_tool_missing(report, "TOOL_MISSING_GCC", "tools.gcc", "gcc", "Install gcc to build local host/tools");

    if (validate_find_in_path("pkg-config", p, sizeof(p))) add_tool_found(report, "tools.pkg-config", "pkg-config", p);
    else add_tool_missing(report, "TOOL_MISSING_PKG_CONFIG", "tools.pkg-config", "pkg-config", "Install pkg-config (needed to detect SDL2)");

    if (check_sdl2_pkgconfig()) {
        validate_report_add(report, "TOOL_FOUND", VALIDATE_SEV_INFO, "tools.sdl2", "SDL2 found via pkg-config", "");
    } else {
        add_tool_missing(report, "TOOL_MISSING_SDL2", "tools.sdl2", "SDL2 (pkg-config sdl2)",
                         "Install SDL2 dev package (e.g. libsdl2-dev) for lcd_touch_host UI");
    }

    return 0;
}

static void check_file_field(const char *repo_root, const char *path, bool required, const char *field,
                             const char *missing_code, const char *label, const char *suggestion,
                             validate_report_t *report) {
    if (!path || !path[0]) {
        if (required) {
            char msg[VALIDATE_MAX_TEXT];
            snprintf(msg, sizeof(msg), "%s path is empty", label);
            validate_report_add(report, missing_code, VALIDATE_SEV_ERROR, field, msg, suggestion);
        }
        return;
    }
    char resolved[PATH_MAX];
    if (validate_path_exists_in_repo(repo_root, path, resolved, sizeof(resolved))) {
        char msg[VALIDATE_MAX_TEXT];
        snprintf(msg, sizeof(msg), "%s exists", label);
        validate_report_add(report, "FILE_OK", VALIDATE_SEV_INFO, field, msg, "");
    } else {
        char msg[VALIDATE_MAX_TEXT];
        snprintf(msg, sizeof(msg), "%s not found: %s", label, path);
        validate_report_add(report, missing_code, required ? VALIDATE_SEV_ERROR : VALIDATE_SEV_WARN, field, msg, suggestion);
    }
}

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
                           validate_report_t *report) {
    if (!report) return -1;

    if (flags & VALIDATE_CHECK_FILES) {
        check_file_field(repo_root, merged_or_firmware_path, true, "firmware.artifacts.merged",
                         "FILE_MISSING_FIRMWARE", "Firmware/merged image",
                         "Set valid merged firmware path (or build/merge image)", report);
        if (use_elf || (elf_path && elf_path[0])) {
            check_file_field(repo_root, elf_path, use_elf, "firmware.artifacts.elf",
                             "FILE_MISSING_ELF", "ELF",
                             use_elf ? "Provide ELF path or disable use_elf" : "Provide ELF path if symbols/debug needed", report);
        }
        if (cosim_board_path && cosim_board_path[0]) {
            check_file_field(repo_root, cosim_board_path, false, "board_hardware.cosim.board_json",
                             "FILE_MISSING_COSIM_BOARD", "Cosim board JSON",
                             "Check --board/board_json path or remove override", report);
        }
        if (littlefs_path && littlefs_path[0]) {
            check_file_field(repo_root, littlefs_path, false, "storage.littlefs.image_path",
                             "FILE_MISSING_LITTLEFS", "LittleFS image",
                             "Provide valid LittleFS image or disable LittleFS", report);
        }
    }

    if (flags & VALIDATE_CHECK_RANGES) {
        if (hw_rotation < 0 || hw_rotation > 3) {
            char msg[VALIDATE_MAX_TEXT];
            snprintf(msg, sizeof(msg), "hw_rotation out of range: %d (expected 0-3)", hw_rotation);
            validate_report_add(report, "RANGE_HW_ROTATION", VALIDATE_SEV_ERROR, "ui_state.hw_rotation", msg,
                                "Set hw_rotation to 0, 1, 2 or 3");
        } else {
            validate_report_add(report, "RANGE_OK", VALIDATE_SEV_INFO, "ui_state.hw_rotation", "hw_rotation in range (0-3)", "");
        }

        if (scale < 1 || scale > 4) {
            char msg[VALIDATE_MAX_TEXT];
            snprintf(msg, sizeof(msg), "scale out of range: %d (expected 1-4)", scale);
            validate_report_add(report, "RANGE_SCALE", VALIDATE_SEV_ERROR, "ui_state.scale", msg,
                                "Set scale between 1 and 4");
        } else {
            validate_report_add(report, "RANGE_OK", VALIDATE_SEV_INFO, "ui_state.scale", "scale in range (1-4)", "");
        }

        if (lcd_w < 0 || lcd_h < 0) {
            char msg[VALIDATE_MAX_TEXT];
            snprintf(msg, sizeof(msg), "LCD dimensions must be >= 0 (0 = default): %dx%d", lcd_w, lcd_h);
            validate_report_add(report, "RANGE_LCD_DIM", VALIDATE_SEV_ERROR, "board_hardware.display", msg,
                                "Use 0 for default or positive width/height");
        } else {
            validate_report_add(report, "RANGE_OK", VALIDATE_SEV_INFO, "board_hardware.display",
                                "LCD width/height >= 0 (0 means default)", "");
        }

        if (instance < 0 || instance > SIM_INSTANCE_MAX) {
            validate_report_add(report, "PORT_INVALID_INSTANCE", VALIDATE_SEV_ERROR, "emu_runtime.instance",
                                "instance must be 0-99 (ports derive from it)",
                                "Use 0 for the default ports 5591, 5555 and 3333");
        } else {
            validate_report_add(report, "PORT_OK", VALIDATE_SEV_INFO, "emu_runtime.instance",
                                "instance is in range 0-99; ports are derived from it", "");
        }
    }

    if (flags & VALIDATE_CHECK_CONFLICTS) {
        if (use_elf && (!elf_path || !elf_path[0])) {
            validate_report_add(report, "CONFLICT_ELF_ENABLED_NO_PATH", VALIDATE_SEV_WARN, "firmware.artifacts.elf",
                                "use_elf enabled but ELF path is empty",
                                "Provide firmware.artifacts.elf or disable use_elf");
        }
        if (!use_elf && release_no_elf) {
            /* expected case, no conflict */
        }
        if (lcd_w == 0 && lcd_h != 0) {
            validate_report_add(report, "CONFLICT_LCD_PARTIAL_OVERRIDE", VALIDATE_SEV_WARN, "board_hardware.display",
                                "LCD width is default (0) but height is overridden",
                                "Set both lcd_w/lcd_h to 0 (default) or override both");
        } else if (lcd_w != 0 && lcd_h == 0) {
            validate_report_add(report, "CONFLICT_LCD_PARTIAL_OVERRIDE", VALIDATE_SEV_WARN, "board_hardware.display",
                                "LCD height is default (0) but width is overridden",
                                "Set both lcd_w/lcd_h to 0 (default) or override both");
        }
    }

    if (flags & VALIDATE_CHECK_CAPS) {
        /* SD (--sd) support is decided in validate_config, which has the emu probe. */
        (void)sd_requested;

        /* pinos/resolução host runtime = needs-runtime-host */
        if (lcd_w > 0 || lcd_h > 0) {
            validate_report_add(report, "CAP_LCD_RUNTIME_HOST", VALIDATE_SEV_WARN, "board_hardware.display",
                                "LCD resolution override is needs-runtime-host (host SDL runtime support partial; may require rebuild/host support)",
                                "Prefer compiled/profile defaults or ensure host supports runtime W/H override");
        }
        if (cosim_board_path && cosim_board_path[0]) {
            validate_report_add(report, "CAP_COSIM_RUNTIME_HOST", VALIDATE_SEV_INFO, "board_hardware.cosim.board_json",
                                "Pin/peripheral mapping depends on cosim board JSON and host/emu support (needs-runtime-host for unsupported parts)",
                                "Validate board JSON devices and host cosim capabilities for target peripherals");
        } else {
            validate_report_add(report, "CAP_PINS_RUNTIME_HOST", VALIDATE_SEV_INFO, "board_hardware.buses_pins",
                                "Pin/resolution runtime behavior is needs-runtime-host unless covered by compiled host/profile defaults",
                                "Use supported board preset/cosim JSON; rebuild host if changing compiled panel/pins");
        }

        /* release sem ELF = warn */
        if (release_no_elf || !use_elf) {
            validate_report_add(report, "CAP_RELEASE_NO_ELF", VALIDATE_SEV_WARN, "firmware.artifacts.elf",
                                "Running without ELF reduces symbol/debug/BLE-hook visibility (release/no-ELF mode)",
                                "Provide matching ELF when available for better diagnostics (GDB/symbols/hooks)");
        } else {
            validate_report_add(report, "CAP_ELF_OK", VALIDATE_SEV_INFO, "firmware.artifacts.elf",
                                "ELF enabled (better symbols/debug/hooks when supported)", "");
        }

        /* features dependentes de versão esp-emu (gdb/trace/sd/parts avançados) = conditional */
        if (!esp_emu_has_advanced) {
            validate_report_add(report, "CAP_EMU_FEATURES_CONDITIONAL", VALIDATE_SEV_WARN, "emu_runtime.features",
                                "Advanced esp-emu features (GDB/trace/SD/parts/read-region, etc.) are conditional on esp-emu version/tooling",
                                "Check esp-emu --help/--version; treat unsupported flags/features as unavailable");
        } else {
            validate_report_add(report, "CAP_EMU_FEATURES_CONDITIONAL", VALIDATE_SEV_INFO, "emu_runtime.features",
                                "Advanced esp-emu features marked conditional by version/tool availability", "");
        }

        /* net user/uart/i2s-wav/cosim-tcp = suportados conforme contexto local */
        validate_report_add(report, "CAP_NET_USER_SUPPORTED_CONTEXT", VALIDATE_SEV_INFO, "network_radio.net_user",
                            "net user is supported in local context when esp-emu/firmware support it (conditional by target/validation)",
                            "Validate end-to-end connectivity for selected firmware/profile");
        validate_report_add(report, "CAP_UART_TCP_SUPPORTED_CONTEXT", VALIDATE_SEV_INFO, "emu_runtime.instance",
                            "UART TCP is supported in local context (requires esp-emu flag/client tooling)",
                            "Use socat/nc client and confirm firmware console backend");
        validate_report_add(report, "CAP_I2S_WAV_SUPPORTED_CONTEXT", VALIDATE_SEV_INFO, "audio",
                            "I2S WAV in/out (file-based) supported in local context depending on esp-emu flags and firmware",
                            "Validate --i2s-wav/--i2s-wav-in behavior with current binary/firmware");
        validate_report_add(report, "CAP_COSIM_TCP_SUPPORTED_CONTEXT", VALIDATE_SEV_INFO, "board_hardware.cosim",
                            "cosim-tcp is supported in local context (esp-emu external host flow), subject to board/part support",
                            "Ensure host/board JSON and endpoint match; use bridge if needed");
    }

    return 0;
}

int validate_config(const launcher_config_t *cfg, const validate_extra_t *extra,
                    const char *repo_root, unsigned flags, validate_report_t *report) {
    if (!report) return -1;
    if (!cfg) {
        validate_report_add(report, "INPUT_NULL_CONFIG", VALIDATE_SEV_ERROR, "config",
                            "launcher_config_t is NULL", "Provide a valid config");
        return -1;
    }

    if (flags & VALIDATE_CHECK_TOOLS) {
        validate_tools(repo_root, flags, report);
    }

    const char *littlefs = NULL;
    if (extra && extra->littlefs_path[0]) {
        littlefs = extra->littlefs_path;
    } else if (cfg->storage.littlefs.enabled && cfg->storage.littlefs.image_path[0]) {
        littlefs = cfg->storage.littlefs.image_path;
    }
    bool sd_requested = (extra && extra->sd_requested) || cfg->storage.sd.enabled;
    bool release_no_elf = extra ? extra->release_no_elf : false;
    bool adv = extra ? extra->esp_emu_has_advanced : false;

    /* Derive likely release/no-elf cases from config if hint absent. */
    if (cfg->firmware == LAUNCHER_FW_KRYONOS_V3) release_no_elf = true;
    if (!cfg->use_elf) release_no_elf = true;

    validate_profile_paths(repo_root,
                           cfg->merged_image_path,
                           cfg->elf_path, cfg->use_elf,
                           cfg->cosim_board_path,
                           littlefs,
                           cfg->hw_rotation, cfg->scale,
                           cfg->lcd_w, cfg->lcd_h,
                           cfg->instance,
                           sd_requested,
                           release_no_elf,
                           adv,
                           flags,
                           report);

    if ((flags & VALIDATE_CHECK_CONFLICTS) && cfg->firmware == LAUNCHER_FW_CUSTOM) {
        if (!cfg->custom_firmware_path[0] && !cfg->merged_image_path[0]) {
            validate_report_add(report, "CONFLICT_CUSTOM_FW_EMPTY", VALIDATE_SEV_ERROR, "firmware.selection",
                                "Custom firmware selected but no custom/merged firmware path set",
                                "Set custom_firmware_path or merged_image_path");
        }
    }

    if (flags & VALIDATE_CHECK_CAPS) {
        /* SD is blocked only when a successful probe of the configured esp-emu has no --sd
         * (esp-emu 1.1.0 is the only supported line). An unprobed or failed probe is not evidence of a block. */
        const emu_caps_probe_t *ep = (extra && extra->emu_caps) ? extra->emu_caps : NULL;
        if (sd_requested && ep && emu_caps_probe_is_probed(ep) && ep->probe_status == 0 &&
            !emu_caps_probe_has_flag(ep, EMU_CAP_FEATURE_SD)) {
            validate_report_add(report, "CAP_SD_BLOCKED_BY_EMU", VALIDATE_SEV_ERROR, "storage.sd_card",
                                "SD card (--sd) not advertised by the probed esp-emu; SD is blocked-by-emu",
                                "Use esp-emu 1.1.0 or newer (--sd), or set emu_caps_path to that binary");
        }
        if (cfg->net_user) {
            validate_report_add(report, "CAP_NET_USER_ENABLED", VALIDATE_SEV_INFO, "network_radio.net_user",
                                "User networking enabled (--net user) in config; support is contextual", "");
        }
    }

    validate_storage_section(cfg, extra, repo_root, flags, report);
    validate_audio_net_section(cfg, extra, repo_root, flags, report);

    return 0;
}
