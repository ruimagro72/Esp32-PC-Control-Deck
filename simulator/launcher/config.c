#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *k_board_ids[LAUNCHER_BOARD_COUNT] = {
    "es3c28p",
    "esp32-s3-devkitc-1-n16r8",
    "lilygo-t-deck",
    "lilygo-t-hmi",
    "m5stack-cardputer",
};

static const char *k_board_dirs[LAUNCHER_BOARD_COUNT] = {
    "es3c28p",
    "esp32-s3-devkitc-1-n16r8",
    "lilygo-t-deck",
    "lilygo-t-hmi",
    "m5stack-cardputer",
};

static const char *k_fw_names[LAUNCHER_FW_COUNT] = {
    "local_es3c28p_emu",
    "kryonos_v3",
    "asset_es3c28p",
    "custom",
};

static void set_str(char *dst, size_t dstsz, const char *src);

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void ui_defaults(launcher_config_t *cfg) {
    if (!cfg) return;
    cfg->ui_window_has_pos = false;
    cfg->ui_window_x = 0;
    cfg->ui_window_y = 0;
    cfg->ui_window_w = 0;
    cfg->ui_window_h = 0;
    cfg->ui_window_maximized = false;
    cfg->ui_paned_main_pos = -1;
    cfg->ui_paned_parts_pos = -1;
    cfg->ui_paned_bottom_pos = -1;
    cfg->ui_tab_config = -1;
    cfg->ui_tab_bottom = -1;
    cfg->ui_tab_log = -1;
    cfg->ui_log_min_level = -1;
    cfg->ui_log_level_mask = 0;   /* 0 = no filter (all) */
    cfg->ui_log_origin_mask = 0;  /* 0 = no filter (all) */
    set_str(cfg->ui_log_source_filter, sizeof(cfg->ui_log_source_filter), "");
    set_str(cfg->ui_log_search, sizeof(cfg->ui_log_search), "");
    cfg->ui_log_follow = true;
    cfg->ui_view_cfg = false;
    cfg->ui_view_bottom = false;
    cfg->ui_view_status = true;
    cfg->ui_view_logs = false;
    set_str(cfg->ui_lang, sizeof(cfg->ui_lang), "auto");
    set_str(cfg->ui_selected_profile_hint, sizeof(cfg->ui_selected_profile_hint), "");
    set_str(cfg->ui_selected_firmware_hint, sizeof(cfg->ui_selected_firmware_hint), "");
}

bool launcher_path_exists(const char *path) {
    if (!path || !path[0]) return false;
    return access(path, F_OK) == 0;
}

const char *launcher_board_id(launcher_board_t board) {
    if (board < 0 || board >= LAUNCHER_BOARD_COUNT) return "es3c28p";
    return k_board_ids[board];
}

const char *launcher_board_dirname(launcher_board_t board) {
    if (board < 0 || board >= LAUNCHER_BOARD_COUNT) return "es3c28p";
    return k_board_dirs[board];
}

int launcher_board_from_id(const char *id, launcher_board_t *out) {
    if (!id || !out) return -1;
    for (int i = 0; i < LAUNCHER_BOARD_COUNT; i++) {
        if (strcmp(id, k_board_ids[i]) == 0) { *out = (launcher_board_t)i; return 0; }
    }
    return -1;
}

const char *launcher_firmware_name(launcher_firmware_t fw) {
    if (fw < 0 || fw >= LAUNCHER_FW_COUNT) return "local_es3c28p_emu";
    return k_fw_names[fw];
}

int launcher_firmware_from_name(const char *name, launcher_firmware_t *out) {
    if (!name || !out) return -1;
    for (int i = 0; i < LAUNCHER_FW_COUNT; i++) {
        if (strcmp(name, k_fw_names[i]) == 0) { *out = (launcher_firmware_t)i; return 0; }
    }
    return -1;
}

static void set_str(char *dst, size_t dstsz, const char *src) {
    if (!dst || dstsz == 0) return;
    if (!src) src = "";
    snprintf(dst, dstsz, "%s", src);
}

int sim_ports_for_instance(int instance, sim_ports_t *out) {
    if (!out) return -1;
    int ok = (instance >= 0 && instance <= SIM_INSTANCE_MAX);
    int n = ok ? instance : 0;
    out->instance = n;
    out->cosim = 5591 + 10 * n;
    out->uart = 5555 + 10 * n;
    out->control = 5556 + 10 * n;
    out->gdb = 3333 + 10 * n;
    return ok ? 0 : -1;
}

void launcher_config_defaults(launcher_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->board = LAUNCHER_BOARD_ES3C28P;
    cfg->firmware = LAUNCHER_FW_LOCAL_ES3C28P_EMU;
    cfg->scale = 2;
    cfg->hw_rotation = 0;
    cfg->audio = true;
    cfg->net_user = false;
    cfg->use_elf = true;
    cfg->lcd_w = 0;
    cfg->lcd_h = 0;
    cfg->instance = 0;
    set_str(cfg->merged_image_path, sizeof(cfg->merged_image_path), "simulator/build/merged-emu.bin");
    set_str(cfg->elf_path, sizeof(cfg->elf_path), "firmware/.pio/build/es3c28p_emu/firmware.elf");
    set_str(cfg->cosim_board_path, sizeof(cfg->cosim_board_path), "");
    set_str(cfg->profile_path, sizeof(cfg->profile_path), "");
    /* T20 helper defaults: LittleFS disabled/conditional; SD modeled as SDMMC via esp-emu 1.1.0 --sd. */
    slfs_config_defaults(&cfg->storage);
    audio_net_set_defaults(&cfg->audio_net);
    /* The Wi-Fi passphrase is never stored in launcher.conf or the profile: only the environment (SIM_WIFI_PASSWORD) sets it. */
    {
        const char *env_pw = getenv("SIM_WIFI_PASSWORD");
        if (env_pw) audio_net_set_wifi_password(&cfg->audio_net, env_pw);
    }
    cfg->audio_net.i2s_enabled = cfg->audio;
    set_str(cfg->emu_caps_path, sizeof(cfg->emu_caps_path), "");
    cfg->sd_enabled = true;
    set_str(cfg->sd_image_path, sizeof(cfg->sd_image_path), LAUNCHER_SD_DEFAULT_IMAGE);
    set_str(cfg->sd_dir, sizeof(cfg->sd_dir), "");
    cfg->run_mode = 0;
    cfg->cosim_use_unix = false;
    set_str(cfg->cosim_unix_path, sizeof(cfg->cosim_unix_path), "");
    ui_defaults(cfg);
    /* If local elf not present, keep path but callers may treat as missing; resolve can adjust use_elf. */
    if (!launcher_path_exists(cfg->elf_path)) {
        /* keep configured default path for reference; do not force-disable use_elf unless desired */
    }
    launcher_config_resolve_paths(cfg);
}

int launcher_config_sd_pre_args(const launcher_config_t *cfg, char *buf, size_t buflen) {
    if (!cfg || !buf || buflen < 2) return -1;
    buf[0] = '\0';
    /* start.sh emu run adds --sd <default image> itself; a custom image replaces it (--no-sd + --sd after "--"). */
    if (!cfg->sd_enabled || (cfg->sd_image_path[0] && strcmp(cfg->sd_image_path, LAUNCHER_SD_DEFAULT_IMAGE) != 0))
        snprintf(buf, buflen, " --no-sd");
    return 0;
}

/* POSIX sh single-quote into dst: 'abc', a'b -> 'a'\''b'. Returns 0 or -1 (too small). */
static int shell_quote_into(char *dst, size_t dstsz, const char *src) {
    size_t o = 0;
    if (!dst || !src || dstsz < 3) return -1;
    dst[o++] = '\'';
    for (; *src; src++) {
        size_t need = (*src == '\'') ? 4 : 1;
        if (o + need + 2 > dstsz) return -1;
        if (*src == '\'') {
            memcpy(dst + o, "'\\''", 4);
            o += 4;
        } else {
            dst[o++] = *src;
        }
    }
    if (o + 2 > dstsz) return -1;
    dst[o++] = '\'';
    dst[o] = '\0';
    return 0;
}

int launcher_config_sd_tail_args(const launcher_config_t *cfg, char *buf, size_t buflen) {
    char quoted[LAUNCHER_MAX_PATH * 4 + 4];
    if (!cfg || !buf || buflen < 2) return -1;
    buf[0] = '\0';
    if (cfg->sd_enabled && cfg->sd_image_path[0] && strcmp(cfg->sd_image_path, LAUNCHER_SD_DEFAULT_IMAGE) != 0) {
        int n;
        if (shell_quote_into(quoted, sizeof(quoted), cfg->sd_image_path) != 0) return -1;
        n = snprintf(buf, buflen, " --sd %s", quoted);
        if (n < 0 || (size_t)n >= buflen) return -1;
    }
    return 0;
}

int launcher_config_resolve_paths(launcher_config_t *cfg) {
    if (!cfg) return -1;

    if (cfg->firmware == LAUNCHER_FW_CUSTOM) {
        if (cfg->custom_firmware_path[0]) {
            set_str(cfg->merged_image_path, sizeof(cfg->merged_image_path), cfg->custom_firmware_path);
        }
        /* elf_path/use_elf left as configured by user */
        return 0;
    }

    if (cfg->firmware == LAUNCHER_FW_KRYONOS_V3) {
        const char *bdir = launcher_board_dirname(cfg->board);
        snprintf(cfg->merged_image_path, sizeof(cfg->merged_image_path),
                 "simulator/emu/firmware/kryonos-v3/%s/merged.bin", bdir);
        cfg->elf_path[0] = '\0';
        cfg->use_elf = false;
        return 0;
    }

    if (cfg->firmware == LAUNCHER_FW_ASSET_ES3C28P) {
        set_str(cfg->merged_image_path, sizeof(cfg->merged_image_path),
                "simulator/emu/firmware/es3c28p/merged.bin");
        set_str(cfg->elf_path, sizeof(cfg->elf_path),
                "simulator/emu/firmware/es3c28p/firmware.elf");
        /* asset elf optional */
        if (!launcher_path_exists(cfg->elf_path)) {
            /* keep use_elf as-is? prefer graceful: disable if missing */
            if (cfg->use_elf) cfg->use_elf = launcher_path_exists(cfg->elf_path);
        }
        return 0;
    }

    /* LOCAL_ES3C28P_EMU (default for es3c28p local emu workflow) */
    set_str(cfg->merged_image_path, sizeof(cfg->merged_image_path), "simulator/build/merged-emu.bin");
    set_str(cfg->elf_path, sizeof(cfg->elf_path), "firmware/.pio/build/es3c28p_emu/firmware.elf");
    if (cfg->use_elf && !launcher_path_exists(cfg->elf_path)) {
        /* default requested with elf when present; if absent keep flag but path may be missing */
    }
    return 0;
}

static char *trim(char *s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) { e--; }
    *e = '\0';
    return s;
}

int launcher_config_set_hostfwd(launcher_config_t *cfg, const char *list) {
    if (!cfg) return -1;
    audio_net_clear_hostfwd(&cfg->audio_net);
    if (!list || !list[0]) return 0;
    char tmp[AUDIO_NET_MAX_HOSTFWD * AUDIO_NET_MAX_TEXT];
    snprintf(tmp, sizeof(tmp), "%s", list);
    char *save = NULL;
    for (char *tok = strtok_r(tmp, ";\n", &save); tok; tok = strtok_r(NULL, ";\n", &save)) {
        tok = trim(tok);
        if (!tok[0]) continue;
        if (audio_net_add_hostfwd(&cfg->audio_net, tok) != 0) return -1;
    }
    return 0;
}

int launcher_config_get_hostfwd(const launcher_config_t *cfg, char *out, size_t outsz) {
    if (!cfg || !out || outsz == 0) return -1;
    out[0] = '\0';
    size_t off = 0;
    for (int i = 0; i < audio_net_get_hostfwd_count(&cfg->audio_net); ++i) {
        const char *rule = audio_net_get_hostfwd(&cfg->audio_net, i);
        if (!rule || !rule[0]) continue;
        int n = snprintf(out + off, outsz - off, "%s%s", off ? ";" : "", rule);
        if (n < 0 || (size_t)n >= outsz - off) return -1;
        off += (size_t)n;
    }
    return 0;
}

static const char *wifi_auth_key(audio_net_wifi_auth_t auth) {
    switch (auth) {
        case AUDIO_NET_WIFI_AUTH_OPEN: return "open";
        case AUDIO_NET_WIFI_AUTH_WPA_PSK: return "wpa-psk";
        case AUDIO_NET_WIFI_AUTH_WPA2_PSK: return "wpa2-psk";
        case AUDIO_NET_WIFI_AUTH_WPA3_PSK: return "wpa3-psk";
        case AUDIO_NET_WIFI_AUTH_UNKNOWN: default: return "unknown";
    }
}

static audio_net_wifi_auth_t wifi_auth_from_key(const char *key) {
    if (!key) return AUDIO_NET_WIFI_AUTH_UNKNOWN;
    if (strcmp(key, "open") == 0) return AUDIO_NET_WIFI_AUTH_OPEN;
    if (strcmp(key, "wpa-psk") == 0 || strcmp(key, "wpa_psk") == 0) return AUDIO_NET_WIFI_AUTH_WPA_PSK;
    if (strcmp(key, "wpa2-psk") == 0 || strcmp(key, "wpa2_psk") == 0) return AUDIO_NET_WIFI_AUTH_WPA2_PSK;
    if (strcmp(key, "wpa3-psk") == 0 || strcmp(key, "wpa3_psk") == 0) return AUDIO_NET_WIFI_AUTH_WPA3_PSK;
    return AUDIO_NET_WIFI_AUTH_UNKNOWN;
}

int launcher_config_save(const launcher_config_t *cfg, const char *path) {
    if (!cfg || !path) return -1;
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "board=%s\n", launcher_board_id(cfg->board));
    fprintf(f, "firmware=%s\n", launcher_firmware_name(cfg->firmware));
    fprintf(f, "merged_image_path=%s\n", cfg->merged_image_path);
    fprintf(f, "use_elf=%d\n", cfg->use_elf ? 1 : 0);
    fprintf(f, "elf_path=%s\n", cfg->elf_path);
    fprintf(f, "scale=%d\n", cfg->scale);
    fprintf(f, "hw_rotation=%d\n", cfg->hw_rotation);
    fprintf(f, "instance=%d\n", cfg->instance);
    fprintf(f, "extra_args=%s\n", cfg->extra_args);
    fprintf(f, "net_user=%d\n", cfg->net_user ? 1 : 0);
    fprintf(f, "audio=%d\n", cfg->audio ? 1 : 0);
    fprintf(f, "custom_firmware_path=%s\n", cfg->custom_firmware_path);
    fprintf(f, "cosim_board_path=%s\n", cfg->cosim_board_path);
    fprintf(f, "lcd_w=%d\n", cfg->lcd_w);
    fprintf(f, "lcd_h=%d\n", cfg->lcd_h);
    fprintf(f, "profile_path=%s\n", cfg->profile_path);
    fprintf(f, "sd_enabled=%d\n", cfg->sd_enabled ? 1 : 0);
    fprintf(f, "sd_image_path=%s\n", cfg->sd_image_path);
    fprintf(f, "sd_dir=%s\n", cfg->sd_dir);
    /* T20 storage (LittleFS) */
    fprintf(f, "littlefs_enabled=%d\n", cfg->storage.littlefs.enabled ? 1 : 0);
    fprintf(f, "littlefs_image=%s\n", cfg->storage.littlefs.image_path);
    fprintf(f, "littlefs_label=%s\n", cfg->storage.littlefs.partition_label);
    fprintf(f, "littlefs_mount=%s\n", cfg->storage.littlefs.mount_path);
    fprintf(f, "littlefs_offset=%u\n", (unsigned)cfg->storage.littlefs.offset);
    fprintf(f, "littlefs_size=%u\n", (unsigned)cfg->storage.littlefs.size);
    fprintf(f, "littlefs_image_optional=%d\n", cfg->storage.littlefs.image_optional ? 1 : 0);
    /* T20 audio (I2S) */
    fprintf(f, "audio_i2s_enabled=%d\n", cfg->audio_net.i2s_enabled ? 1 : 0);
    fprintf(f, "audio_i2s_out=%s\n", cfg->audio_net.i2s_wav_out);
    fprintf(f, "audio_i2s_in=%s\n", cfg->audio_net.i2s_wav_in);
    fprintf(f, "audio_i2s_realtime=%d\n", cfg->audio_net.i2s_realtime ? 1 : 0);
    fprintf(f, "audio_i2s_instance=%d\n", cfg->audio_net.i2s_instance);
    fprintf(f, "audio_i2s_backend=%s\n", cfg->audio_net.i2s_backend_hint);
    /* T20 net/wifi/ble */
    fprintf(f, "net_enabled=%d\n", cfg->audio_net.net_enabled ? 1 : 0);
    fprintf(f, "net_mode=%s\n", cfg->audio_net.net_mode);
    {
        char hf[AUDIO_NET_MAX_HOSTFWD * AUDIO_NET_MAX_TEXT];
        if (launcher_config_get_hostfwd(cfg, hf, sizeof(hf)) != 0) hf[0] = '\0';
        fprintf(f, "net_hostfwd=%s\n", hf);
    }
    fprintf(f, "wifi_enabled=%d\n", cfg->audio_net.wifi_enabled ? 1 : 0);
    fprintf(f, "wifi_ssid=%s\n", cfg->audio_net.wifi_ssid);
    fprintf(f, "wifi_auth=%s\n", wifi_auth_key(cfg->audio_net.wifi_auth));
    fprintf(f, "ble_enabled=%d\n", cfg->audio_net.ble_enabled ? 1 : 0);
    fprintf(f, "ble_hci=%s\n", cfg->audio_net.ble_hci);
    /* T20 emu caps probe path (results are runtime-only, never persisted) */
    fprintf(f, "emu_caps_path=%s\n", cfg->emu_caps_path);
    fprintf(f, "run_mode=%d\n", cfg->run_mode);
    fprintf(f, "cosim_use_unix=%d\n", cfg->cosim_use_unix ? 1 : 0);
    fprintf(f, "cosim_unix_path=%s\n", cfg->cosim_unix_path);
    /* T12/T13c/T17 UI/layout/profile-workflow persistence */
    fprintf(f, "ui_window_has_pos=%d\n", cfg->ui_window_has_pos ? 1 : 0);
    fprintf(f, "ui_window_x=%d\n", cfg->ui_window_x);
    fprintf(f, "ui_window_y=%d\n", cfg->ui_window_y);
    fprintf(f, "ui_window_w=%d\n", cfg->ui_window_w);
    fprintf(f, "ui_window_h=%d\n", cfg->ui_window_h);
    fprintf(f, "ui_window_maximized=%d\n", cfg->ui_window_maximized ? 1 : 0);
    fprintf(f, "ui_paned_main_pos=%d\n", cfg->ui_paned_main_pos);
    fprintf(f, "ui_paned_parts_pos=%d\n", cfg->ui_paned_parts_pos);
    fprintf(f, "ui_paned_bottom_pos=%d\n", cfg->ui_paned_bottom_pos);
    fprintf(f, "ui_tab_config=%d\n", cfg->ui_tab_config);
    fprintf(f, "ui_tab_bottom=%d\n", cfg->ui_tab_bottom);
    fprintf(f, "ui_tab_log=%d\n", cfg->ui_tab_log);
    fprintf(f, "ui_log_min_level=%d\n", cfg->ui_log_min_level);
    fprintf(f, "ui_log_level_mask=%u\n", cfg->ui_log_level_mask);
    fprintf(f, "ui_log_origin_mask=%u\n", cfg->ui_log_origin_mask);
    fprintf(f, "ui_log_source_filter=%s\n", cfg->ui_log_source_filter);
    fprintf(f, "ui_log_search=%s\n", cfg->ui_log_search);
    fprintf(f, "ui_log_follow=%d\n", cfg->ui_log_follow ? 1 : 0);
    fprintf(f, "ui_view_cfg=%d\n", cfg->ui_view_cfg ? 1 : 0);
    fprintf(f, "ui_view_bottom=%d\n", cfg->ui_view_bottom ? 1 : 0);
    fprintf(f, "ui_view_status=%d\n", cfg->ui_view_status ? 1 : 0);
    fprintf(f, "ui_view_logs=%d\n", cfg->ui_view_logs ? 1 : 0);
    fprintf(f, "ui_lang=%s\n", cfg->ui_lang);
    fprintf(f, "ui_selected_profile_hint=%s\n", cfg->ui_selected_profile_hint);
    fprintf(f, "ui_selected_firmware_hint=%s\n", cfg->ui_selected_firmware_hint);
    fclose(f);
    return 0;
}

int launcher_config_load(launcher_config_t *cfg, const char *path) {
    if (!cfg || !path) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    launcher_config_defaults(cfg);
    bool seen_i2s_enabled = false;
    bool seen_net_enabled = false;
    bool seen_net_mode = false;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);
        if (strcmp(key, "board") == 0) {
            launcher_board_t b; if (launcher_board_from_id(val, &b) == 0) cfg->board = b;
        } else if (strcmp(key, "firmware") == 0) {
            launcher_firmware_t fw; if (launcher_firmware_from_name(val, &fw) == 0) cfg->firmware = fw;
        } else if (strcmp(key, "merged_image_path") == 0) {
            set_str(cfg->merged_image_path, sizeof(cfg->merged_image_path), val);
        } else if (strcmp(key, "use_elf") == 0) {
            cfg->use_elf = (atoi(val) != 0);
        } else if (strcmp(key, "elf_path") == 0) {
            set_str(cfg->elf_path, sizeof(cfg->elf_path), val);
        } else if (strcmp(key, "scale") == 0) {
            int v = atoi(val); if (v < 1) v = 1; if (v > 4) v = 4; cfg->scale = v;
        } else if (strcmp(key, "hw_rotation") == 0) {
            int v = atoi(val); if (v < 0) v = 0; if (v > 3) v = 3; cfg->hw_rotation = v;
        } else if (strcmp(key, "instance") == 0) {
            cfg->instance = atoi(val);
        } else if (strcmp(key, "extra_args") == 0) {
            set_str(cfg->extra_args, sizeof(cfg->extra_args), val);
        } else if (strcmp(key, "net_user") == 0) {
            cfg->net_user = (atoi(val) != 0);
        } else if (strcmp(key, "audio") == 0) {
            cfg->audio = (atoi(val) != 0);
        } else if (strcmp(key, "custom_firmware_path") == 0) {
            set_str(cfg->custom_firmware_path, sizeof(cfg->custom_firmware_path), val);
        } else if (strcmp(key, "cosim_board_path") == 0) {
            set_str(cfg->cosim_board_path, sizeof(cfg->cosim_board_path), val);
        } else if (strcmp(key, "lcd_w") == 0) {
            cfg->lcd_w = atoi(val);
        } else if (strcmp(key, "lcd_h") == 0) {
            cfg->lcd_h = atoi(val);
        } else if (strcmp(key, "profile_path") == 0 || strcmp(key, "active_profile") == 0) {
            set_str(cfg->profile_path, sizeof(cfg->profile_path), val);
        } else if (strcmp(key, "sd_enabled") == 0) {
            cfg->sd_enabled = (atoi(val) != 0);
        } else if (strcmp(key, "sd_image_path") == 0) {
            if (val[0]) set_str(cfg->sd_image_path, sizeof(cfg->sd_image_path), val);
        } else if (strcmp(key, "sd_dir") == 0) {
            set_str(cfg->sd_dir, sizeof(cfg->sd_dir), val);
        } else if (strcmp(key, "littlefs_enabled") == 0) {
            cfg->storage.littlefs.enabled = (atoi(val) != 0);
        } else if (strcmp(key, "littlefs_image") == 0) {
            slfs_littlefs_set_image(&cfg->storage.littlefs, val);
        } else if (strcmp(key, "littlefs_label") == 0) {
            set_str(cfg->storage.littlefs.partition_label, sizeof(cfg->storage.littlefs.partition_label), val);
        } else if (strcmp(key, "littlefs_mount") == 0) {
            slfs_littlefs_set_mount(&cfg->storage.littlefs, val);
        } else if (strcmp(key, "littlefs_offset") == 0) {
            cfg->storage.littlefs.offset = (uint32_t)strtoul(val, NULL, 0);
        } else if (strcmp(key, "littlefs_size") == 0) {
            cfg->storage.littlefs.size = (uint32_t)strtoul(val, NULL, 0);
        } else if (strcmp(key, "littlefs_image_optional") == 0) {
            cfg->storage.littlefs.image_optional = (atoi(val) != 0);
        } else if (strcmp(key, "audio_i2s_enabled") == 0) {
            cfg->audio_net.i2s_enabled = (atoi(val) != 0);
            seen_i2s_enabled = true;
        } else if (strcmp(key, "audio_i2s_out") == 0) {
            audio_net_set_i2s_wav_out(&cfg->audio_net, val);
        } else if (strcmp(key, "audio_i2s_in") == 0) {
            audio_net_set_i2s_wav_in(&cfg->audio_net, val);
        } else if (strcmp(key, "audio_i2s_realtime") == 0) {
            cfg->audio_net.i2s_realtime = (atoi(val) != 0);
        } else if (strcmp(key, "audio_i2s_instance") == 0) {
            int v = atoi(val);
            if (v < -1 || v > 8) v = -1;
            cfg->audio_net.i2s_instance = v;
        } else if (strcmp(key, "audio_i2s_backend") == 0) {
            audio_net_set_i2s_backend_hint(&cfg->audio_net, val);
        } else if (strcmp(key, "net_enabled") == 0) {
            cfg->audio_net.net_enabled = (atoi(val) != 0);
            seen_net_enabled = true;
        } else if (strcmp(key, "net_mode") == 0) {
            if (val[0]) audio_net_set_net_mode(&cfg->audio_net, val);
            seen_net_mode = true;
        } else if (strcmp(key, "net_hostfwd") == 0) {
            (void)launcher_config_set_hostfwd(cfg, val);
        } else if (strcmp(key, "wifi_enabled") == 0) {
            cfg->audio_net.wifi_enabled = (atoi(val) != 0);
        } else if (strcmp(key, "wifi_ssid") == 0) {
            audio_net_set_wifi_ssid(&cfg->audio_net, val);
        } else if (strcmp(key, "wifi_password") == 0) {
            /* ignored: an old launcher.conf may still hold a passphrase; it is never loaded */
        } else if (strcmp(key, "wifi_auth") == 0) {
            cfg->audio_net.wifi_auth = wifi_auth_from_key(val);
        } else if (strcmp(key, "ble_enabled") == 0) {
            cfg->audio_net.ble_enabled = (atoi(val) != 0);
        } else if (strcmp(key, "ble_hci") == 0) {
            audio_net_set_ble_hci(&cfg->audio_net, val);
        } else if (strcmp(key, "emu_caps_path") == 0) {
            set_str(cfg->emu_caps_path, sizeof(cfg->emu_caps_path), val);
        } else if (strcmp(key, "run_mode") == 0) {
            cfg->run_mode = atoi(val);
            if (cfg->run_mode < 0) cfg->run_mode = 0;
            if (cfg->run_mode > 1) cfg->run_mode = 1;
        } else if (strcmp(key, "cosim_use_unix") == 0) {
            cfg->cosim_use_unix = (atoi(val) != 0);
        } else if (strcmp(key, "cosim_unix_path") == 0) {
            set_str(cfg->cosim_unix_path, sizeof(cfg->cosim_unix_path), val);
        } else if (strcmp(key, "ui_window_has_pos") == 0) {
            cfg->ui_window_has_pos = (atoi(val) != 0);
        } else if (strcmp(key, "ui_window_x") == 0) {
            cfg->ui_window_x = clamp_int(atoi(val), -10000, 10000);
        } else if (strcmp(key, "ui_window_y") == 0) {
            cfg->ui_window_y = clamp_int(atoi(val), -10000, 10000);
        } else if (strcmp(key, "ui_window_w") == 0) {
            int v = atoi(val);
            cfg->ui_window_w = (v <= 0) ? 0 : clamp_int(v, 1, 16384);
        } else if (strcmp(key, "ui_window_h") == 0) {
            int v = atoi(val);
            cfg->ui_window_h = (v <= 0) ? 0 : clamp_int(v, 1, 16384);
        } else if (strcmp(key, "ui_window_maximized") == 0) {
            cfg->ui_window_maximized = (atoi(val) != 0);
        } else if (strcmp(key, "ui_paned_main_pos") == 0) {
            int v = atoi(val);
            cfg->ui_paned_main_pos = (v < 0) ? -1 : clamp_int(v, 0, 16384);
        } else if (strcmp(key, "ui_paned_parts_pos") == 0) {
            int v = atoi(val);
            cfg->ui_paned_parts_pos = (v < 0) ? -1 : clamp_int(v, 0, 16384);
        } else if (strcmp(key, "ui_paned_bottom_pos") == 0) {
            int v = atoi(val);
            cfg->ui_paned_bottom_pos = (v < 0) ? -1 : clamp_int(v, 0, 16384);
        } else if (strcmp(key, "ui_tab_config") == 0) {
            int v = atoi(val);
            cfg->ui_tab_config = (v < 0) ? -1 : clamp_int(v, 0, 256);
        } else if (strcmp(key, "ui_tab_bottom") == 0) {
            int v = atoi(val);
            cfg->ui_tab_bottom = (v < 0) ? -1 : clamp_int(v, 0, 256);
        } else if (strcmp(key, "ui_tab_log") == 0) {
            int v = atoi(val);
            cfg->ui_tab_log = (v < 0) ? -1 : clamp_int(v, 0, 256);
        } else if (strcmp(key, "ui_log_min_level") == 0) {
            int v = atoi(val);
            cfg->ui_log_min_level = (v < 0) ? -1 : clamp_int(v, 0, 64);
        } else if (strcmp(key, "ui_log_level_mask") == 0) {
            cfg->ui_log_level_mask = (unsigned int)strtoul(val, NULL, 0);
        } else if (strcmp(key, "ui_log_origin_mask") == 0) {
            cfg->ui_log_origin_mask = (unsigned int)strtoul(val, NULL, 0);
        } else if (strcmp(key, "ui_log_source_filter") == 0) {
            set_str(cfg->ui_log_source_filter, sizeof(cfg->ui_log_source_filter), val);
        } else if (strcmp(key, "ui_log_search") == 0) {
            set_str(cfg->ui_log_search, sizeof(cfg->ui_log_search), val);
        } else if (strcmp(key, "ui_log_follow") == 0) {
            cfg->ui_log_follow = (atoi(val) != 0);
        } else if (strcmp(key, "ui_view_cfg") == 0) {
            cfg->ui_view_cfg = (atoi(val) != 0);
        } else if (strcmp(key, "ui_view_bottom") == 0) {
            cfg->ui_view_bottom = (atoi(val) != 0);
        } else if (strcmp(key, "ui_view_status") == 0) {
            cfg->ui_view_status = (atoi(val) != 0);
        } else if (strcmp(key, "ui_view_logs") == 0) {
            cfg->ui_view_logs = (atoi(val) != 0);
        } else if (strcmp(key, "ui_lang") == 0) {
            set_str(cfg->ui_lang, sizeof(cfg->ui_lang), val);
        } else if (strcmp(key, "ui_selected_profile_hint") == 0) {
            set_str(cfg->ui_selected_profile_hint, sizeof(cfg->ui_selected_profile_hint), val);
        } else if (strcmp(key, "ui_selected_firmware_hint") == 0) {
            set_str(cfg->ui_selected_firmware_hint, sizeof(cfg->ui_selected_firmware_hint), val);
        }
    }
    fclose(f);
    /* Legacy compatibility: old conf has only audio/net_user; new conf may have both. */
    if (!seen_i2s_enabled) cfg->audio_net.i2s_enabled = cfg->audio;
    else cfg->audio = cfg->audio_net.i2s_enabled;
    if (!seen_net_enabled) {
        cfg->audio_net.net_enabled = cfg->net_user;
        if (cfg->net_user && !seen_net_mode) audio_net_set_net_mode(&cfg->audio_net, "user");
    }
    if (!cfg->audio_net.net_mode[0] && cfg->audio_net.net_enabled)
        audio_net_set_net_mode(&cfg->audio_net, "user");
    cfg->net_user = cfg->audio_net.net_enabled &&
                    (strcmp(cfg->audio_net.net_mode, "user") == 0 || !cfg->audio_net.net_mode[0]);
    /* Re-resolve known selections to keep paths consistent with selection/board; custom keeps merged/custom as loaded. */
    if (cfg->firmware != LAUNCHER_FW_CUSTOM) {
        launcher_config_resolve_paths(cfg);
    } else if (cfg->custom_firmware_path[0] && !cfg->merged_image_path[0]) {
        set_str(cfg->merged_image_path, sizeof(cfg->merged_image_path), cfg->custom_firmware_path);
    }
    return 0;
}

