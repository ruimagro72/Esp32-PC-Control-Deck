#ifndef SIM_LAUNCHER_AUDIO_NET_H
#define SIM_LAUNCHER_AUDIO_NET_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_NET_MAX_PATH      512
#define AUDIO_NET_MAX_TEXT      256
#define AUDIO_NET_MAX_ARGS      64
#define AUDIO_NET_MAX_HOSTFWD   16

/* Capability states aligned with docs/simulator/SIMULATOR_CAPABILITIES_MATRIX.md */
typedef enum {
    AUDIO_NET_CAP_SUPPORTED_NOW = 0,
    AUDIO_NET_CAP_NEEDS_RUNTIME_HOST,
    AUDIO_NET_CAP_BLOCKED_BY_EMU,
    AUDIO_NET_CAP_CONDITIONAL /* version/tool/firmware/host dependent */
} audio_net_cap_state_t;

typedef enum {
    AUDIO_NET_FEAT_I2S_ENABLE = 0,
    AUDIO_NET_FEAT_I2S_WAV_OUT,
    AUDIO_NET_FEAT_I2S_WAV_IN,
    AUDIO_NET_FEAT_I2S_INSTANCE,
    AUDIO_NET_FEAT_I2S_REALTIME,
    AUDIO_NET_FEAT_I2S_BACKEND_HINT,
    AUDIO_NET_FEAT_NET_USER,
    AUDIO_NET_FEAT_NET_HOSTFWD,
    AUDIO_NET_FEAT_WIFI_PLACEHOLDER,
    AUDIO_NET_FEAT_BLE_HCI,
    AUDIO_NET_FEAT_COUNT
} audio_net_feature_t;

typedef enum {
    AUDIO_NET_WIFI_AUTH_OPEN = 0,
    AUDIO_NET_WIFI_AUTH_WPA_PSK,
    AUDIO_NET_WIFI_AUTH_WPA2_PSK,
    AUDIO_NET_WIFI_AUTH_WPA3_PSK,
    AUDIO_NET_WIFI_AUTH_UNKNOWN
} audio_net_wifi_auth_t;

typedef struct {
    audio_net_cap_state_t state;
    bool supported_now;          /* convenience: state == SUPPORTED_NOW */
    bool requires_runtime_host;  /* convenience */
    bool blocked;                /* convenience */
    bool conditional;            /* convenience */
    char condition[AUDIO_NET_MAX_TEXT]; /* version/tool/firmware/host note */
    char note[AUDIO_NET_MAX_TEXT];
} audio_net_capability_t;

typedef struct {
    /* Audio (I2S) */
    bool i2s_enabled;
    char i2s_wav_out[AUDIO_NET_MAX_PATH];
    char i2s_wav_in[AUDIO_NET_MAX_PATH];
    bool i2s_realtime;           /* conditional/PCM realtime intent */
    int  i2s_instance;           /* -1 = unset/default */
    char i2s_backend_hint[AUDIO_NET_MAX_TEXT]; /* e.g. wav-file, cosim, pipe (hint only) */

    /* Network / radio */
    bool net_enabled;
    char net_mode[AUDIO_NET_MAX_TEXT]; /* e.g. user, tap, vmnet */
    char hostfwd[AUDIO_NET_MAX_HOSTFWD][AUDIO_NET_MAX_TEXT]; /* esp-emu hostfwd rule strings */
    int hostfwd_count;

    /* Wi-Fi placeholders (firmware/runtime dependent) */
    bool wifi_enabled;
    char wifi_ssid[AUDIO_NET_MAX_TEXT];
    char wifi_password[AUDIO_NET_MAX_TEXT];
    audio_net_wifi_auth_t wifi_auth;

    /* BLE (conditional/version/tool) */
    bool ble_enabled;
    char ble_hci[AUDIO_NET_MAX_TEXT]; /* e.g. tcp:127.0.0.1:1234 or hci0 */
} audio_net_config_t;

typedef struct {
    int argc;
    char argv[AUDIO_NET_MAX_ARGS][AUDIO_NET_MAX_PATH];
} audio_net_args_t;

/* Lifecycle/defaults */
void audio_net_init(audio_net_config_t *cfg);
void audio_net_set_defaults(audio_net_config_t *cfg);

/* Setters/getters */
void audio_net_set_i2s_enabled(audio_net_config_t *cfg, bool enabled);
bool audio_net_get_i2s_enabled(const audio_net_config_t *cfg);
int  audio_net_set_i2s_wav_out(audio_net_config_t *cfg, const char *path);
const char *audio_net_get_i2s_wav_out(const audio_net_config_t *cfg);
int  audio_net_set_i2s_wav_in(audio_net_config_t *cfg, const char *path);
const char *audio_net_get_i2s_wav_in(const audio_net_config_t *cfg);
void audio_net_set_i2s_realtime(audio_net_config_t *cfg, bool realtime);
bool audio_net_get_i2s_realtime(const audio_net_config_t *cfg);
void audio_net_set_i2s_instance(audio_net_config_t *cfg, int instance);
int  audio_net_get_i2s_instance(const audio_net_config_t *cfg);
int  audio_net_set_i2s_backend_hint(audio_net_config_t *cfg, const char *hint);
const char *audio_net_get_i2s_backend_hint(const audio_net_config_t *cfg);

void audio_net_set_net_enabled(audio_net_config_t *cfg, bool enabled);
bool audio_net_get_net_enabled(const audio_net_config_t *cfg);
int  audio_net_set_net_mode(audio_net_config_t *cfg, const char *mode);
const char *audio_net_get_net_mode(const audio_net_config_t *cfg);
int  audio_net_add_hostfwd(audio_net_config_t *cfg, const char *rule);
void audio_net_clear_hostfwd(audio_net_config_t *cfg);
int  audio_net_get_hostfwd_count(const audio_net_config_t *cfg);
const char *audio_net_get_hostfwd(const audio_net_config_t *cfg, int index);

void audio_net_set_wifi_enabled(audio_net_config_t *cfg, bool enabled);
bool audio_net_get_wifi_enabled(const audio_net_config_t *cfg);
int  audio_net_set_wifi_ssid(audio_net_config_t *cfg, const char *ssid);
const char *audio_net_get_wifi_ssid(const audio_net_config_t *cfg);
int  audio_net_set_wifi_password(audio_net_config_t *cfg, const char *password);
const char *audio_net_get_wifi_password(const audio_net_config_t *cfg);
void audio_net_set_wifi_auth(audio_net_config_t *cfg, audio_net_wifi_auth_t auth);
audio_net_wifi_auth_t audio_net_get_wifi_auth(const audio_net_config_t *cfg);

void audio_net_set_ble_enabled(audio_net_config_t *cfg, bool enabled);
bool audio_net_get_ble_enabled(const audio_net_config_t *cfg);
int  audio_net_set_ble_hci(audio_net_config_t *cfg, const char *hci);
const char *audio_net_get_ble_hci(const audio_net_config_t *cfg);

/* Basic validation (paths/rules/flags). Returns true if no errors. */
bool audio_net_validate(const audio_net_config_t *cfg, char *err, size_t errlen);

/* Capability per feature for current local baseline (esp-emu 1.1.0 S3) unless
 * caller notes different runtime/tool in summaries/UI. */
audio_net_capability_t audio_net_capability(audio_net_feature_t feature,
                                            const audio_net_config_t *cfg);

/* Format esp-emu args/options supported now (best-effort, known flags only).
 * Unknown/conditional realtime backend hints are reported as notes, not args. */
int audio_net_format_emu_args(const audio_net_config_t *cfg, audio_net_args_t *out);

/* Human-readable summary for UI/log, including capability states. */
int audio_net_format_summary(const audio_net_config_t *cfg, char *buf, size_t buflen);

/* Helpers */
const char *audio_net_cap_state_str(audio_net_cap_state_t state);
const char *audio_net_feature_str(audio_net_feature_t feature);
const char *audio_net_wifi_auth_str(audio_net_wifi_auth_t auth);

#ifdef __cplusplus
}
#endif

#endif /* SIM_LAUNCHER_AUDIO_NET_H */
