#include "audio_net.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

static void an_copy(char *dst, size_t dstlen, const char *src) {
    if (!dst || dstlen == 0) return;
    if (!src) src = "";
    snprintf(dst, dstlen, "%s", src);
}

static bool an_has_text(const char *s) {
    return s && s[0] != '\0';
}

static bool an_path_basic_ok(const char *path) {
    if (!path || !path[0]) return false;
    /* Basic sanity: reject control chars/newlines; existence checked by caller/tools. */
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p) {
        if (*p < 0x20) return false;
    }
    return true;
}

static bool an_token_simple(const char *s) {
    if (!an_has_text(s)) return false;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        if (isspace(*p)) return false;
    }
    return true;
}

/* Minimal hostfwd rule shape check: expects proto:hostaddr:hostport-guestaddr:guestport
 * (as commonly used by esp-emu/qemu user-net). We keep it permissive but require colons/dash. */
static bool an_hostfwd_rule_ok(const char *rule) {
    if (!an_token_simple(rule)) return false;
    const char *c1 = strchr(rule, ':');
    if (!c1) return false;
    const char *c2 = strchr(c1 + 1, ':');
    if (!c2) return false;
    const char *dash = strchr(c2 + 1, '-');
    if (!dash) return false;
    const char *c3 = strchr(dash + 1, ':');
    return c3 != NULL;
}

void audio_net_init(audio_net_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    audio_net_set_defaults(cfg);
}

void audio_net_set_defaults(audio_net_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->i2s_enabled = false;
    cfg->i2s_realtime = false;
    cfg->i2s_instance = -1;
    an_copy(cfg->i2s_backend_hint, sizeof(cfg->i2s_backend_hint), "wav-file");

    cfg->net_enabled = false;
    an_copy(cfg->net_mode, sizeof(cfg->net_mode), "user");
    cfg->hostfwd_count = 0;

    cfg->wifi_enabled = false;
    cfg->wifi_auth = AUDIO_NET_WIFI_AUTH_UNKNOWN;

    cfg->ble_enabled = false;
}

void audio_net_set_i2s_enabled(audio_net_config_t *cfg, bool enabled) { if (cfg) cfg->i2s_enabled = enabled; }
bool audio_net_get_i2s_enabled(const audio_net_config_t *cfg) { return cfg ? cfg->i2s_enabled : false; }

int audio_net_set_i2s_wav_out(audio_net_config_t *cfg, const char *path) {
    if (!cfg) return -1;
    an_copy(cfg->i2s_wav_out, sizeof(cfg->i2s_wav_out), path);
    return 0;
}
const char *audio_net_get_i2s_wav_out(const audio_net_config_t *cfg) { return cfg ? cfg->i2s_wav_out : ""; }

int audio_net_set_i2s_wav_in(audio_net_config_t *cfg, const char *path) {
    if (!cfg) return -1;
    an_copy(cfg->i2s_wav_in, sizeof(cfg->i2s_wav_in), path);
    return 0;
}
const char *audio_net_get_i2s_wav_in(const audio_net_config_t *cfg) { return cfg ? cfg->i2s_wav_in : ""; }

void audio_net_set_i2s_realtime(audio_net_config_t *cfg, bool realtime) { if (cfg) cfg->i2s_realtime = realtime; }
bool audio_net_get_i2s_realtime(const audio_net_config_t *cfg) { return cfg ? cfg->i2s_realtime : false; }

void audio_net_set_i2s_instance(audio_net_config_t *cfg, int instance) { if (cfg) cfg->i2s_instance = instance; }
int audio_net_get_i2s_instance(const audio_net_config_t *cfg) { return cfg ? cfg->i2s_instance : -1; }

int audio_net_set_i2s_backend_hint(audio_net_config_t *cfg, const char *hint) {
    if (!cfg) return -1;
    an_copy(cfg->i2s_backend_hint, sizeof(cfg->i2s_backend_hint), hint);
    return 0;
}
const char *audio_net_get_i2s_backend_hint(const audio_net_config_t *cfg) { return cfg ? cfg->i2s_backend_hint : ""; }

void audio_net_set_net_enabled(audio_net_config_t *cfg, bool enabled) { if (cfg) cfg->net_enabled = enabled; }
bool audio_net_get_net_enabled(const audio_net_config_t *cfg) { return cfg ? cfg->net_enabled : false; }

int audio_net_set_net_mode(audio_net_config_t *cfg, const char *mode) {
    if (!cfg) return -1;
    an_copy(cfg->net_mode, sizeof(cfg->net_mode), mode);
    return 0;
}
const char *audio_net_get_net_mode(const audio_net_config_t *cfg) { return cfg ? cfg->net_mode : ""; }

int audio_net_add_hostfwd(audio_net_config_t *cfg, const char *rule) {
    if (!cfg || !an_has_text(rule)) return -1;
    if (cfg->hostfwd_count >= AUDIO_NET_MAX_HOSTFWD) return -2;
    an_copy(cfg->hostfwd[cfg->hostfwd_count], sizeof(cfg->hostfwd[cfg->hostfwd_count]), rule);
    cfg->hostfwd_count++;
    return 0;
}
void audio_net_clear_hostfwd(audio_net_config_t *cfg) { if (cfg) cfg->hostfwd_count = 0; }
int audio_net_get_hostfwd_count(const audio_net_config_t *cfg) { return cfg ? cfg->hostfwd_count : 0; }
const char *audio_net_get_hostfwd(const audio_net_config_t *cfg, int index) {
    if (!cfg || index < 0 || index >= cfg->hostfwd_count) return "";
    return cfg->hostfwd[index];
}

void audio_net_set_wifi_enabled(audio_net_config_t *cfg, bool enabled) { if (cfg) cfg->wifi_enabled = enabled; }
bool audio_net_get_wifi_enabled(const audio_net_config_t *cfg) { return cfg ? cfg->wifi_enabled : false; }

int audio_net_set_wifi_ssid(audio_net_config_t *cfg, const char *ssid) {
    if (!cfg) return -1;
    an_copy(cfg->wifi_ssid, sizeof(cfg->wifi_ssid), ssid);
    return 0;
}
const char *audio_net_get_wifi_ssid(const audio_net_config_t *cfg) { return cfg ? cfg->wifi_ssid : ""; }

int audio_net_set_wifi_password(audio_net_config_t *cfg, const char *password) {
    if (!cfg) return -1;
    an_copy(cfg->wifi_password, sizeof(cfg->wifi_password), password);
    return 0;
}
const char *audio_net_get_wifi_password(const audio_net_config_t *cfg) { return cfg ? cfg->wifi_password : ""; }

void audio_net_set_wifi_auth(audio_net_config_t *cfg, audio_net_wifi_auth_t auth) { if (cfg) cfg->wifi_auth = auth; }
audio_net_wifi_auth_t audio_net_get_wifi_auth(const audio_net_config_t *cfg) { return cfg ? cfg->wifi_auth : AUDIO_NET_WIFI_AUTH_UNKNOWN; }

void audio_net_set_ble_enabled(audio_net_config_t *cfg, bool enabled) { if (cfg) cfg->ble_enabled = enabled; }
bool audio_net_get_ble_enabled(const audio_net_config_t *cfg) { return cfg ? cfg->ble_enabled : false; }

int audio_net_set_ble_hci(audio_net_config_t *cfg, const char *hci) {
    if (!cfg) return -1;
    an_copy(cfg->ble_hci, sizeof(cfg->ble_hci), hci);
    return 0;
}
const char *audio_net_get_ble_hci(const audio_net_config_t *cfg) { return cfg ? cfg->ble_hci : ""; }

bool audio_net_validate(const audio_net_config_t *cfg, char *err, size_t errlen) {
    if (err && errlen) err[0] = '\0';
    if (!cfg) {
        if (err && errlen) snprintf(err, errlen, "null config");
        return false;
    }

    if (an_has_text(cfg->i2s_wav_out) && !an_path_basic_ok(cfg->i2s_wav_out)) {
        if (err && errlen) snprintf(err, errlen, "invalid i2s wav out path");
        return false;
    }
    if (an_has_text(cfg->i2s_wav_in) && !an_path_basic_ok(cfg->i2s_wav_in)) {
        if (err && errlen) snprintf(err, errlen, "invalid i2s wav in path");
        return false;
    }
    if (cfg->i2s_instance < -1 || cfg->i2s_instance > 8) {
        if (err && errlen) snprintf(err, errlen, "invalid i2s instance (use -1 or 0..8)");
        return false;
    }

    if (cfg->net_enabled || an_has_text(cfg->net_mode)) {
        if (!an_token_simple(cfg->net_mode)) {
            if (err && errlen) snprintf(err, errlen, "invalid net mode");
            return false;
        }
    }
    if (cfg->hostfwd_count < 0 || cfg->hostfwd_count > AUDIO_NET_MAX_HOSTFWD) {
        if (err && errlen) snprintf(err, errlen, "invalid hostfwd count");
        return false;
    }
    for (int i = 0; i < cfg->hostfwd_count; ++i) {
        if (!an_hostfwd_rule_ok(cfg->hostfwd[i])) {
            if (err && errlen) snprintf(err, errlen, "invalid hostfwd rule: %s", cfg->hostfwd[i]);
            return false;
        }
    }

    if (cfg->wifi_enabled) {
        /* placeholders allowed, but if provided keep simple/no control chars */
        if (an_has_text(cfg->wifi_ssid)) {
            for (const unsigned char *p = (const unsigned char *)cfg->wifi_ssid; *p; ++p) {
                if (*p < 0x20) { if (err && errlen) snprintf(err, errlen, "invalid wifi ssid"); return false; }
            }
        }
        if (an_has_text(cfg->wifi_password)) {
            for (const unsigned char *p = (const unsigned char *)cfg->wifi_password; *p; ++p) {
                if (*p < 0x20) { if (err && errlen) snprintf(err, errlen, "invalid wifi password"); return false; }
            }
        }
        if (cfg->wifi_auth < AUDIO_NET_WIFI_AUTH_OPEN || cfg->wifi_auth > AUDIO_NET_WIFI_AUTH_UNKNOWN) {
            if (err && errlen) snprintf(err, errlen, "invalid wifi auth");
            return false;
        }
    }

    if (cfg->ble_enabled && an_has_text(cfg->ble_hci)) {
        /* Accept tcp:host:port or simple hci name/token */
        if (strchr(cfg->ble_hci, ' ') != NULL) {
            if (err && errlen) snprintf(err, errlen, "invalid ble hci value");
            return false;
        }
    }

    return true;
}

static audio_net_capability_t an_cap(audio_net_cap_state_t st, const char *condition, const char *note) {
    audio_net_capability_t c;
    memset(&c, 0, sizeof(c));
    c.state = st;
    c.supported_now = (st == AUDIO_NET_CAP_SUPPORTED_NOW);
    c.requires_runtime_host = (st == AUDIO_NET_CAP_NEEDS_RUNTIME_HOST);
    c.blocked = (st == AUDIO_NET_CAP_BLOCKED_BY_EMU);
    c.conditional = (st == AUDIO_NET_CAP_CONDITIONAL);
    an_copy(c.condition, sizeof(c.condition), condition ? condition : "");
    an_copy(c.note, sizeof(c.note), note ? note : "");
    return c;
}

audio_net_capability_t audio_net_capability(audio_net_feature_t feature,
                                            const audio_net_config_t *cfg) {
    (void)cfg;
    switch (feature) {
        case AUDIO_NET_FEAT_I2S_ENABLE:
        case AUDIO_NET_FEAT_I2S_WAV_OUT:
            return an_cap(AUDIO_NET_CAP_SUPPORTED_NOW,
                          _("esp-emu 1.1.0 (S3) with --i2s-wav"),
                          _("WAV file capture/playback supported; firmware may need correct init"));
        case AUDIO_NET_FEAT_I2S_WAV_IN:
        case AUDIO_NET_FEAT_I2S_INSTANCE:
            return an_cap(AUDIO_NET_CAP_CONDITIONAL,
                          _("esp-emu version/binary and firmware support"),
                          _("Flags --i2s-wav-in/--i2s-instance exist in docs/help; check on the real binary/flow"));
        case AUDIO_NET_FEAT_I2S_REALTIME:
            return an_cap(AUDIO_NET_CAP_CONDITIONAL,
                          _("version/tool (e.g. 1.1+/external cosim) and firmware"),
                          _("Realtime PCM not guaranteed on the esp-emu 1.1.0 baseline; may require cosim/tools"));
        case AUDIO_NET_FEAT_I2S_BACKEND_HINT:
            return an_cap(AUDIO_NET_CAP_CONDITIONAL,
                          "backend/toolchain/runtime host",
                          _("Hint for UI/log; only --i2s-wav/--i2s-wav-in map to known args"));
        case AUDIO_NET_FEAT_NET_USER:
            return an_cap(AUDIO_NET_CAP_CONDITIONAL,
                          _("firmware/host/permissions and network backend"),
                          _("--net user available in the baseline; STA/AP/services parity depends on firmware/validation"));
        case AUDIO_NET_FEAT_NET_HOSTFWD:
            return an_cap(AUDIO_NET_CAP_CONDITIONAL,
                          _("esp-emu version, --net user mode and supported rules"),
                          _("hostfwd mapped to --hostfwd when given; check syntax/effect on the binary"));
        case AUDIO_NET_FEAT_WIFI_PLACEHOLDER:
            return an_cap(AUDIO_NET_CAP_CONDITIONAL,
                          _("firmware and esp-emu Wi-Fi flags"),
                          _("SSID/pass/auth are placeholders that depend on firmware/flags (--wifi-*); parity not validated"));
        case AUDIO_NET_FEAT_BLE_HCI:
            return an_cap(AUDIO_NET_CAP_CONDITIONAL,
                          _("version/tool, --elf for hooks and external HCI (Bumble/adapter)"),
                          _("Flag --ble-hci present; the full BLE workflow depends on ELF/runtime/host"));
        default:
            break;
    }
    return an_cap(AUDIO_NET_CAP_CONDITIONAL, "desconhecido", "");
}

static int an_push_arg(audio_net_args_t *out, const char *arg) {
    if (!out || !arg) return -1;
    if (out->argc < 0) out->argc = 0;
    if (out->argc >= AUDIO_NET_MAX_ARGS) return -2;
    an_copy(out->argv[out->argc], sizeof(out->argv[out->argc]), arg);
    out->argc++;
    return 0;
}

int audio_net_format_emu_args(const audio_net_config_t *cfg, audio_net_args_t *out) {
    if (!cfg || !out) return -1;
    memset(out, 0, sizeof(*out));

    /* I2S: map known/supported-now flags first. */
    if (cfg->i2s_enabled || an_has_text(cfg->i2s_wav_out)) {
        if (an_has_text(cfg->i2s_wav_out)) {
            if (an_push_arg(out, "--i2s-wav") != 0) return -2;
            if (an_push_arg(out, cfg->i2s_wav_out) != 0) return -2;
        }
    }
    if (an_has_text(cfg->i2s_wav_in)) {
        if (an_push_arg(out, "--i2s-wav-in") != 0) return -2;
        if (an_push_arg(out, cfg->i2s_wav_in) != 0) return -2;
    }
    if (cfg->i2s_instance >= 0) {
        char buf[32];
        if (an_push_arg(out, "--i2s-instance") != 0) return -2;
        snprintf(buf, sizeof(buf), "%d", cfg->i2s_instance);
        if (an_push_arg(out, buf) != 0) return -2;
    }
    /* Realtime: no stable/confirmed esp-emu 1.1.0 flag; emitted as note only. */

    /* Network */
    if (cfg->net_enabled && an_has_text(cfg->net_mode)) {
        if (an_push_arg(out, "--net") != 0) return -2;
        if (an_push_arg(out, cfg->net_mode) != 0) return -2;
    }
    for (int i = 0; i < cfg->hostfwd_count; ++i) {
        if (an_has_text(cfg->hostfwd[i])) {
            if (an_push_arg(out, "--hostfwd") != 0) return -2;
            if (an_push_arg(out, cfg->hostfwd[i]) != 0) return -2;
        }
    }

    /* Wi-Fi placeholders: map to common esp-emu flags when provided (conditional). */
    if (cfg->wifi_enabled) {
        if (an_has_text(cfg->wifi_ssid)) {
            if (an_push_arg(out, "--wifi-ssid") != 0) return -2;
            if (an_push_arg(out, cfg->wifi_ssid) != 0) return -2;
        }
        /* esp-emu: "" means an open AP, so an open network always sends an empty passphrase */
        if (cfg->wifi_auth == AUDIO_NET_WIFI_AUTH_OPEN || an_has_text(cfg->wifi_password)) {
            if (an_push_arg(out, "--wifi-password") != 0) return -2;
            if (an_push_arg(out, cfg->wifi_auth == AUDIO_NET_WIFI_AUTH_OPEN ? "" : cfg->wifi_password) != 0) return -2;
        }
        if (cfg->wifi_auth != AUDIO_NET_WIFI_AUTH_UNKNOWN) {
            if (an_push_arg(out, "--wifi-auth") != 0) return -2;
            if (an_push_arg(out, audio_net_wifi_auth_str(cfg->wifi_auth)) != 0) return -2;
        }
    }

    /* BLE */
    if (cfg->ble_enabled && an_has_text(cfg->ble_hci)) {
        if (an_push_arg(out, "--ble-hci") != 0) return -2;
        if (an_push_arg(out, cfg->ble_hci) != 0) return -2;
    }

    return out->argc;
}

static int an_append(char *buf, size_t buflen, size_t *off, const char *fmt, ...) {
    if (!buf || buflen == 0 || !off || *off >= buflen) return -1;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *off, buflen - *off, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    size_t rem = buflen - *off - 1;
    size_t adv = ((size_t)n > rem) ? rem : (size_t)n;
    *off += adv;
    return 0;
}

int audio_net_format_summary(const audio_net_config_t *cfg, char *buf, size_t buflen) {
    if (!buf || buflen == 0) return -1;
    buf[0] = '\0';
    if (!cfg) {
        snprintf(buf, buflen, "audio/net: (null)");
        return 0;
    }

    size_t off = 0;
    audio_net_capability_t cap_i2s = audio_net_capability(AUDIO_NET_FEAT_I2S_WAV_OUT, cfg);
    audio_net_capability_t cap_in = audio_net_capability(AUDIO_NET_FEAT_I2S_WAV_IN, cfg);
    audio_net_capability_t cap_rt = audio_net_capability(AUDIO_NET_FEAT_I2S_REALTIME, cfg);
    audio_net_capability_t cap_net = audio_net_capability(AUDIO_NET_FEAT_NET_USER, cfg);
    audio_net_capability_t cap_fw = audio_net_capability(AUDIO_NET_FEAT_NET_HOSTFWD, cfg);
    audio_net_capability_t cap_wifi = audio_net_capability(AUDIO_NET_FEAT_WIFI_PLACEHOLDER, cfg);
    audio_net_capability_t cap_ble = audio_net_capability(AUDIO_NET_FEAT_BLE_HCI, cfg);

    an_append(buf, buflen, &off,
              "audio: enabled=%s out=%s in=%s inst=%d realtime=%s backend=%s caps[out=%s,in=%s,rt=%s]\n",
              cfg->i2s_enabled ? "yes" : "no",
              an_has_text(cfg->i2s_wav_out) ? cfg->i2s_wav_out : "-",
              an_has_text(cfg->i2s_wav_in) ? cfg->i2s_wav_in : "-",
              cfg->i2s_instance,
              cfg->i2s_realtime ? "yes(conditional)" : "no",
              an_has_text(cfg->i2s_backend_hint) ? cfg->i2s_backend_hint : "-",
              audio_net_cap_state_str(cap_i2s.state),
              audio_net_cap_state_str(cap_in.state),
              audio_net_cap_state_str(cap_rt.state));

    an_append(buf, buflen, &off,
              "net: enabled=%s mode=%s hostfwd=%d caps[net=%s,hostfwd=%s]\n",
              cfg->net_enabled ? "yes" : "no",
              an_has_text(cfg->net_mode) ? cfg->net_mode : "-",
              cfg->hostfwd_count,
              audio_net_cap_state_str(cap_net.state),
              audio_net_cap_state_str(cap_fw.state));

    for (int i = 0; i < cfg->hostfwd_count; ++i) {
        an_append(buf, buflen, &off, "  hostfwd[%d]: %s\n", i, cfg->hostfwd[i]);
    }

    an_append(buf, buflen, &off,
              "wifi: enabled=%s ssid=%s auth=%s cap=%s (placeholder/firmware-dependent)\n",
              cfg->wifi_enabled ? "yes" : "no",
              an_has_text(cfg->wifi_ssid) ? cfg->wifi_ssid : "-",
              audio_net_wifi_auth_str(cfg->wifi_auth),
              audio_net_cap_state_str(cap_wifi.state));

    an_append(buf, buflen, &off,
              "ble: enabled=%s hci=%s cap=%s (%s)\n",
              cfg->ble_enabled ? "yes" : "no",
              an_has_text(cfg->ble_hci) ? cfg->ble_hci : "-",
              audio_net_cap_state_str(cap_ble.state),
              cap_ble.condition[0] ? cap_ble.condition : "conditional");

    if (cfg->i2s_realtime) {
        an_append(buf, buflen, &off, "note: i2s realtime requested but not mapped to stable 1.1.0 arg; %s\n", cap_rt.note);
    }
    an_append(buf, buflen, &off, "note: wifi/ble parity depends on firmware, ELF/hooks and runtime host/tools\n");

    return (int)off;
}

const char *audio_net_cap_state_str(audio_net_cap_state_t state) {
    switch (state) {
        case AUDIO_NET_CAP_SUPPORTED_NOW: return "supported-now";
        case AUDIO_NET_CAP_NEEDS_RUNTIME_HOST: return "needs-runtime-host";
        case AUDIO_NET_CAP_BLOCKED_BY_EMU: return "blocked-by-emu";
        case AUDIO_NET_CAP_CONDITIONAL: return "conditional(version/tool)";
        default: return "conditional(version/tool)";
    }
}

const char *audio_net_feature_str(audio_net_feature_t feature) {
    switch (feature) {
        case AUDIO_NET_FEAT_I2S_ENABLE: return "i2s.enable";
        case AUDIO_NET_FEAT_I2S_WAV_OUT: return "i2s.wav_out";
        case AUDIO_NET_FEAT_I2S_WAV_IN: return "i2s.wav_in";
        case AUDIO_NET_FEAT_I2S_INSTANCE: return "i2s.instance";
        case AUDIO_NET_FEAT_I2S_REALTIME: return "i2s.realtime";
        case AUDIO_NET_FEAT_I2S_BACKEND_HINT: return "i2s.backend_hint";
        case AUDIO_NET_FEAT_NET_USER: return "net.user";
        case AUDIO_NET_FEAT_NET_HOSTFWD: return "net.hostfwd";
        case AUDIO_NET_FEAT_WIFI_PLACEHOLDER: return "wifi.placeholder";
        case AUDIO_NET_FEAT_BLE_HCI: return "ble.hci";
        default: return "unknown";
    }
}

const char *audio_net_wifi_auth_str(audio_net_wifi_auth_t auth) {
    switch (auth) {
        case AUDIO_NET_WIFI_AUTH_OPEN: return "open";
        case AUDIO_NET_WIFI_AUTH_WPA_PSK: return "wpa2-wpa3";
        case AUDIO_NET_WIFI_AUTH_WPA2_PSK: return "wpa2-psk";
        case AUDIO_NET_WIFI_AUTH_WPA3_PSK: return "wpa3-sae";
        case AUDIO_NET_WIFI_AUTH_UNKNOWN:
        default: return "unknown";
    }
}
