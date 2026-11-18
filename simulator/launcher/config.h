#ifndef SIM_LAUNCHER_CONFIG_H
#define SIM_LAUNCHER_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#include "audio_net.h"
#include "storage_littlefs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LAUNCHER_MAX_PATH       512
#define LAUNCHER_MAX_SHORT      64
#define LAUNCHER_MAX_ARGS       512
#define LAUNCHER_MAX_CMD        2048
#define LAUNCHER_MAX_ARGV       64
#define LAUNCHER_MAX_LOG_TEXT   128
#define LAUNCHER_MAX_UI_LABEL   128
#define SIM_INSTANCE_MAX        99   /* emulator instance number range 0..99 */

/* Default esp-emu binary, relative to repo root. Single source for all launcher call sites. */
#define EMU_DEFAULT_PATH        "simulator/esp-emu-1.1.0-x86_64-unknown-linux-gnu/esp-emu"

typedef enum {
    LAUNCHER_BOARD_ES3C28P = 0,
    LAUNCHER_BOARD_ESP32_S3_DEVKITC1_N16R8,
    LAUNCHER_BOARD_LILYGO_T_DECK,
    LAUNCHER_BOARD_LILYGO_T_HMI,
    LAUNCHER_BOARD_M5STACK_CARDPUTER,
    LAUNCHER_BOARD_COUNT
} launcher_board_t;

typedef enum {
    LAUNCHER_FW_LOCAL_ES3C28P_EMU = 0, /* simulator/build/merged-emu.bin (+ local emu elf when present) */
    LAUNCHER_FW_KRYONOS_V3,            /* simulator/emu/firmware/kryonos-v3/<board>/merged.bin, no elf */
    LAUNCHER_FW_ASSET_ES3C28P,         /* simulator/emu/firmware/es3c28p/merged.bin, optional elf from asset if present */
    LAUNCHER_FW_CUSTOM,                /* custom_firmware_path / merged_image_path override */
    LAUNCHER_FW_COUNT
} launcher_firmware_t;

typedef struct {
    launcher_board_t board;
    launcher_firmware_t firmware;
    char merged_image_path[LAUNCHER_MAX_PATH];
    bool use_elf;
    char elf_path[LAUNCHER_MAX_PATH]; /* optional */
    int scale;                        /* 1-4 */
    int hw_rotation;                  /* 0-3 */
    int instance; /* emulator instance 0..SIM_INSTANCE_MAX; ports come from sim_ports_for_instance */
    char extra_args[LAUNCHER_MAX_ARGS];
    bool net_user;
    bool audio;
    char custom_firmware_path[LAUNCHER_MAX_PATH];
    /* Hardware overrides / editable params */
    char cosim_board_path[LAUNCHER_MAX_PATH]; /* passed as --board <path> to start.sh emu ui (cosim board json) */
    int lcd_w; /* 0 = board/profile default */
    int lcd_h; /* 0 = board/profile default */
    /* Optional profile path (JSON). If set, launcher may prefer loading/migrating from it. */
    char profile_path[LAUNCHER_MAX_PATH];
    /* --- T20: storage/audio/net helper state (delegated types) --- */
    slfs_config_t storage;        /* LittleFS image/partition/mount + SD model (SD via esp-emu 1.1.0 --sd) */
    /* SD card image mounted with esp-emu --sd (1.1.0). Default image is emu/storage/sd.img (start.sh adds it by itself). */
    bool sd_enabled;                          /* default 1; 0 => start.sh emu run --no-sd */
    char sd_image_path[LAUNCHER_MAX_PATH];    /* default LAUNCHER_SD_DEFAULT_IMAGE */
    audio_net_config_t audio_net; /* I2S WAV in/out, realtime hint, net/hostfwd/wifi placeholders/ble hci */
    char emu_caps_path[LAUNCHER_MAX_PATH]; /* optional esp-emu binary for caps probe; empty = autodetect */
    int run_mode; /* 0=ui+embedded host, 1=backend-only (--no-host/cosim) */
    bool cosim_use_unix; /* host connects via Unix socket bridge (socat); esp-emu 1.1.0 stays on TCP (conditional tool) */
    char cosim_unix_path[LAUNCHER_MAX_PATH]; /* e.g. /tmp/sim-cosim.sock; auto if empty when enabled */

    /* --- T12/T13c/T17 UI/layout/profile-workflow persistence --- */
    bool ui_window_has_pos;
    int ui_window_x;
    int ui_window_y;
    int ui_window_w;
    int ui_window_h;
    bool ui_window_maximized;
    int ui_paned_main_pos;      /* main horizontal split position, -1 = unset */
    int ui_paned_parts_pos;     /* parts vertical split position, -1 = unset */
    int ui_paned_bottom_pos;    /* bottom/secondary split position, -1 = unset */
    int ui_tab_config;          /* config notebook active page, -1 = unset */
    int ui_tab_bottom;          /* bottom notebook active page, -1 = unset */
    int ui_tab_log;             /* log notebook/subtab active page, -1 = unset */
    /* Log filter state (UI hints; console_log masks/text/follow) */
    int ui_log_min_level;       /* 0..N, -1 = unset/default */
    unsigned int ui_log_level_mask;
    unsigned int ui_log_origin_mask;
    char ui_log_source_filter[LAUNCHER_MAX_LOG_TEXT];
    char ui_log_search[LAUNCHER_MAX_LOG_TEXT];
    bool ui_log_follow;
    /* View menu state (Ver): config panel, bottom dock, status bar, logs */
    bool ui_view_cfg;
    bool ui_view_bottom;
    bool ui_view_status;
    bool ui_view_logs;
    /* UI language: "auto" (environment), "en" or "pt". Applied at start-up (restart needed). */
    char ui_lang[LAUNCHER_MAX_SHORT];
    /* Profile/firmware workflow UI hints (selection mostly tracked elsewhere) */
    char ui_selected_profile_hint[LAUNCHER_MAX_UI_LABEL];
    char ui_selected_firmware_hint[LAUNCHER_MAX_UI_LABEL];
} launcher_config_t;

#define LAUNCHER_SD_DEFAULT_IMAGE "simulator/emu/storage/sd.img"

/* SD card args for the "start.sh emu run" line. Pre = before the "--" separator (" --no-sd" or ""),
 * tail = after it (" --sd 'path'" only when a non-default image is selected). Both return 0 or -1 (buffer too small). */
int launcher_config_sd_pre_args(const launcher_config_t *cfg, char *buf, size_t buflen);
int launcher_config_sd_tail_args(const launcher_config_t *cfg, char *buf, size_t buflen);

const char *launcher_board_id(launcher_board_t board);
const char *launcher_board_dirname(launcher_board_t board);
int launcher_board_from_id(const char *id, launcher_board_t *out);

const char *launcher_firmware_name(launcher_firmware_t fw);
int launcher_firmware_from_name(const char *name, launcher_firmware_t *out);

/* Ports of one emulator instance N (0..SIM_INSTANCE_MAX):
 * cosim 5591+10N, uart 5555+10N, control 5556+10N, gdb 3333+10N.
 * The launcher never stores these ports; it always derives them from N.
 */
typedef struct {
    int instance;
    int cosim;   /* --cosim-tcp 127.0.0.1:<cosim> */
    int uart;    /* --uart-tcp 127.0.0.1:<uart> */
    int control; /* reserved, no --control-tcp is emitted by the launcher */
    int gdb;     /* --gdb <gdb> when the GDB stub is enabled */
} sim_ports_t;

/* Fills *out for instance N. Returns 0 when N is in 0..SIM_INSTANCE_MAX,
 * -1 otherwise (out then holds instance 0). Returns -1 when out is NULL. */
int sim_ports_for_instance(int instance, sim_ports_t *out);

/* Set defaults:
 * board es3c28p, local emu firmware simulator/build/merged-emu.bin with elf
 * firmware/.pio/build/es3c28p_emu/firmware.elf when present, scale 2, rot 0,
 * instance 0 (uart 127.0.0.1:5555), audio on.
 */
void launcher_config_defaults(launcher_config_t *cfg);

/* Resolve merged/elf paths for known selections and current board.
 * - LOCAL_ES3C28P_EMU -> simulator/build/merged-emu.bin (+ elf above when present/useful)
 * - KRYONOS_V3       -> simulator/emu/firmware/kryonos-v3/<board>/merged.bin (no elf)
 * - ASSET_ES3C28P    -> simulator/emu/firmware/es3c28p/merged.bin (+ asset elf if present)
 * - CUSTOM           -> uses custom_firmware_path as merged (if set)
 * Returns 0 on success (paths may still be missing files; check exists), <0 on bad args.
 */
int launcher_config_resolve_paths(launcher_config_t *cfg);

/* Save/load simple key=value config file (e.g. simulator/launcher/launcher.conf).
 * Missing file on load is not fatal (returns -1 but caller may keep defaults). Returns 0 on success.
 * T20 keys (littlefs_*, audio_i2s_*, net_*, wifi_*, ble_*, emu_caps_path) are optional; missing
 * keys keep safe defaults and legacy audio/net_user keys still work.
 */
int launcher_config_save(const launcher_config_t *cfg, const char *path);
int launcher_config_load(launcher_config_t *cfg, const char *path);

/* Hostfwd list helpers: rules are separated by ';' or newlines (esp-emu --hostfwd values).
 * set = replace list; get = join into out (';' separated). Return 0 on success, -1 on error. */
int launcher_config_set_hostfwd(launcher_config_t *cfg, const char *list);
int launcher_config_get_hostfwd(const launcher_config_t *cfg, char *out, size_t outsz);

bool launcher_path_exists(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* SIM_LAUNCHER_CONFIG_H */
