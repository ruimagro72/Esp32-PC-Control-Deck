#ifndef SIM_LAUNCHER_PROFILE_H
#define SIM_LAUNCHER_PROFILE_H

#include <stdbool.h>
#include <stddef.h>

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SIM_PROFILE_SCHEMA_VERSION "simulator.profile/v1"

#define SIM_PROFILE_MAX_ID      64
#define SIM_PROFILE_MAX_NAME    128
#define SIM_PROFILE_MAX_DESC    512
#define SIM_PROFILE_MAX_PATH    LAUNCHER_MAX_PATH
#define SIM_PROFILE_MAX_SHORT   64
#define SIM_PROFILE_MAX_ARGS    LAUNCHER_MAX_ARGS
#define SIM_PROFILE_MAX_TAGS    16
#define SIM_PROFILE_MAX_TAGLEN  32
#define SIM_PROFILE_MAX_HOSTFWD 8
#define SIM_PROFILE_MAX_PINS    8

typedef enum {
    SIM_PROFILE_DIAG_ERROR = 0,
    SIM_PROFILE_DIAG_WARNING,
    SIM_PROFILE_DIAG_INFO
} sim_profile_diag_level_t;

typedef struct {
    sim_profile_diag_level_t level;
    char path[128];   /* JSON-ish path or field name */
    char message[256];
} sim_profile_diag_t;

typedef struct {
    sim_profile_diag_t *items;
    size_t count;
    size_t cap;
} sim_profile_diags_t;

typedef struct {
    char id[SIM_PROFILE_MAX_ID];
    char name[SIM_PROFILE_MAX_NAME];
    char description[SIM_PROFILE_MAX_DESC];
    char tags[SIM_PROFILE_MAX_TAGS][SIM_PROFILE_MAX_TAGLEN];
    size_t tag_count;
    char board_preset[SIM_PROFILE_MAX_SHORT];
    char created_at[64];
    char updated_at[64];
} sim_profile_metadata_t;

typedef struct {
    char chip[SIM_PROFILE_MAX_SHORT];      /* esp32s3 etc */
    char psram[SIM_PROFILE_MAX_SHORT];     /* "2M", "8M", "0" or "" */
    char flash[SIM_PROFILE_MAX_SHORT];     /* "16M" or "" */
} sim_profile_mcu_t;

typedef struct {
    int width;
    int height;
    int orientation; /* 0-3 */
    int hw_rotation; /* from display.overrides.hw_rotation, 0-3, -1 unset */
    char panel_name[SIM_PROFILE_MAX_SHORT];
} sim_profile_display_t;

typedef struct {
    char key[SIM_PROFILE_MAX_SHORT];    /* e.g. "sda" */
    char value[SIM_PROFILE_MAX_SHORT];  /* GPIO number as text, or string; "" = null */
} sim_profile_pin_t;

typedef struct {
    char controller[SIM_PROFILE_MAX_SHORT];
    char bus[SIM_PROFILE_MAX_SHORT];
    sim_profile_pin_t pins[SIM_PROFILE_MAX_PINS]; /* JSON touch.pins; extra entries beyond max are dropped */
    size_t pin_count;
} sim_profile_touch_t;

typedef struct {
    char mode[SIM_PROFILE_MAX_SHORT];      /* none|board_json|tcp|unix */
    char board_json[SIM_PROFILE_MAX_PATH];
    char tcp_host[SIM_PROFILE_MAX_SHORT];
    int tcp_port; /* 0 if unset */
    char unix_path[SIM_PROFILE_MAX_PATH];
} sim_profile_cosim_t;

typedef struct {
    char board_id[SIM_PROFILE_MAX_SHORT];
    sim_profile_mcu_t mcu;
    sim_profile_display_t display;
    sim_profile_touch_t touch;
    sim_profile_cosim_t cosim;
} sim_profile_board_hw_t;

typedef struct {
    char merged[SIM_PROFILE_MAX_PATH];
    char boot[SIM_PROFILE_MAX_PATH];
    char part[SIM_PROFILE_MAX_PATH];
    char app[SIM_PROFILE_MAX_PATH];
    char elf[SIM_PROFILE_MAX_PATH];
    bool use_elf;
    char littlefs[SIM_PROFILE_MAX_PATH];
    char uf2[SIM_PROFILE_MAX_PATH];
} sim_profile_fw_artifacts_t;

typedef struct {
    char mode[SIM_PROFILE_MAX_SHORT];
    char freq[SIM_PROFILE_MAX_SHORT];
    char size[SIM_PROFILE_MAX_SHORT];
    char bootloader_offset[SIM_PROFILE_MAX_SHORT];
    char partitions_offset[SIM_PROFILE_MAX_SHORT];
    char app_offset[SIM_PROFILE_MAX_SHORT];
} sim_profile_fw_flash_t;

typedef struct {
    char selection[SIM_PROFILE_MAX_SHORT];
    char format[SIM_PROFILE_MAX_SHORT];
    sim_profile_fw_artifacts_t artifacts;
    sim_profile_fw_flash_t flash;
    char build_env[SIM_PROFILE_MAX_SHORT];
} sim_profile_firmware_t;

typedef struct {
    bool littlefs_enabled;
    char littlefs_image_path[SIM_PROFILE_MAX_PATH];
    char littlefs_mount_path[SIM_PROFILE_MAX_PATH];
    bool sd_enabled;
    char sd_image_path[SIM_PROFILE_MAX_PATH];
    char sd_mount_path[SIM_PROFILE_MAX_PATH];
    bool card_enabled;                       /* esp-emu --sd image mounted (JSON storage.sd_enabled, default true) */
    char card_image_path[SIM_PROFILE_MAX_PATH]; /* JSON storage.sd_image_path (default simulator/emu/storage/sd.img) */
} sim_profile_storage_t;

typedef struct {
    bool enabled;
    bool realtime;
    char out[SIM_PROFILE_MAX_PATH];
    char in[SIM_PROFILE_MAX_PATH];
} sim_profile_audio_t;

typedef struct {
    bool net_user;
    char net_mode[SIM_PROFILE_MAX_SHORT];  /* esp-emu net mode (user, tap, vmnet); "" = network off */
    char hostfwd[SIM_PROFILE_MAX_HOSTFWD][SIM_PROFILE_MAX_ARGS];
    size_t hostfwd_count;
    bool wifi_enabled;
    char wifi_ssid[SIM_PROFILE_MAX_SHORT]; /* SSID only; the Wi-Fi passphrase is never stored in the profile */
    char wifi_auth[SIM_PROFILE_MAX_SHORT]; /* open | wpa2-wpa3 | wpa2-psk | wpa3-sae | unknown */
    bool ble_enabled;
    char ble_hci[SIM_PROFILE_MAX_ARGS];    /* e.g. tcp:localhost:9544 */
} sim_profile_network_t;

typedef struct {
    char esp_emu_path[SIM_PROFILE_MAX_PATH];
    char chip[SIM_PROFILE_MAX_SHORT];
    char psram[SIM_PROFILE_MAX_SHORT];
    int timeout_sec; /* -1 unset/null */
    int instance; /* emulator instance 0..99; ports derived (sim_ports_for_instance) */
    char extra_args[SIM_PROFILE_MAX_ARGS];
    char console_mode[SIM_PROFILE_MAX_SHORT];
} sim_profile_emu_runtime_t;

typedef struct {
    double scale;
    bool has_window;
    int window_x, window_y, window_w, window_h;
    bool window_maximized;
} sim_profile_ui_state_t;

typedef struct {
    char schema_version[SIM_PROFILE_MAX_SHORT];
    char schema_ref[SIM_PROFILE_MAX_PATH]; /* top-level "$schema" as read; "" = none, written back only if set */
    sim_profile_metadata_t metadata;
    sim_profile_board_hw_t board_hardware;
    sim_profile_firmware_t firmware;
    sim_profile_storage_t storage;
    sim_profile_audio_t audio;
    sim_profile_network_t network_radio;
    sim_profile_emu_runtime_t emu_runtime;
    sim_profile_ui_state_t ui_state;
    char *extensions_json; /* owned, raw JSON object text e.g. {"ns":...} or NULL treated as {} */
} sim_profile_t;

/* Diagnostics helpers */
void sim_profile_diags_init(sim_profile_diags_t *d);
void sim_profile_diags_free(sim_profile_diags_t *d);
void sim_profile_diags_add(sim_profile_diags_t *d, sim_profile_diag_level_t level,
                           const char *path, const char *fmt, ...);
bool sim_profile_diags_has_errors(const sim_profile_diags_t *d);

/* Profile lifecycle/defaults */
void sim_profile_init(sim_profile_t *p);
void sim_profile_free(sim_profile_t *p);
void sim_profile_defaults(sim_profile_t *p);

/* JSON IO.
 * profile_path: path to json file (used for load/save context if desired). May be NULL for pure parse context.
 * On load: p is initialized/reset on success or failure as appropriate; caller owns p and must free.
 * Returns 0 on success (diags may contain warnings), <0 on fatal parse/IO error.
 */
int sim_profile_load_json(sim_profile_t *p, const char *profile_path, sim_profile_diags_t *diags);
int sim_profile_save_json(const sim_profile_t *p, const char *profile_path, sim_profile_diags_t *diags);

/* Validate profile structure/values and optionally check referenced paths.
 * base_dir_for_paths: if non-NULL, relative paths are resolved against it for existence checks.
 * Returns 0 if no errors, <0 if errors present (also see diags).
 */
int sim_profile_validate(const sim_profile_t *p, const char *base_dir_for_paths, sim_profile_diags_t *diags);

/* Import/migrate legacy launcher.conf (key=value) into profile structure.
 * conf_path: path to launcher.conf. repo_root may be NULL.
 * Returns 0 on success, <0 on IO/parse fatal error.
 */
int sim_profile_import_launcher_conf(sim_profile_t *p, const char *conf_path,
                                     const char *repo_root, sim_profile_diags_t *diags);

/* Path helpers.
 * - sim_profile_resolve_path: join base_dir + in when in is relative; copy as-is when absolute/empty.
 *   Returns 0 on success, <0 on truncation/invalid args.
 * - sim_profile_relativize_path: produce path relative to base_dir when possible, else copy input.
 */
int sim_profile_resolve_path(const char *base_dir, const char *in, char *out, size_t outsz);
int sim_profile_relativize_path(const char *base_dir, const char *in, char *out, size_t outsz);

/* Mapping to/from legacy launcher_config_t (launcher/config.h). */
void sim_profile_from_launcher_config(sim_profile_t *p, const launcher_config_t *cfg);
void sim_profile_to_launcher_config(const sim_profile_t *p, launcher_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* SIM_LAUNCHER_PROFILE_H */
