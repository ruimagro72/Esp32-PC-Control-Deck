#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <SDL2/SDL.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "board_catalog.h"
#include "config.h"
#include "console_log.h"
#include "firmware_mgr.h"
#include "help_ui.h"
#include "i18n.h"
#include "parts_ui.h"
#include "proc.h"
#include "profile.h"
#include "sim_host.h"
#include "telemetry.h"
#include "theme.h"
#include "validate.h"

#define LAUNCHER_CONF_PATH "simulator/launcher/launcher.conf"
#define ACTIVE_PROFILE_PATH "simulator/launcher/profiles/active.json"
#define PROFILE_SCHEMA_PATH "simulator/launcher/profile.schema.json"
#define BOARDS_DIR_PATH "simulator/launcher/boards"
#define HW_ROT_STATE_PATH  "simulator/emu/.hw_rotation"
#define HW_BOARD_STATE_PATH "simulator/emu/.hw_board"

typedef struct {
    GtkWidget *window;
    GtkWidget *entry_profile_path;
    GtkWidget *combo_board;
    GtkWidget *combo_chip;
    GtkWidget *combo_firmware;
    GtkWidget *entry_merged;
    GtkWidget *entry_elf;
    GtkWidget *check_use_elf;
    GtkWidget *entry_custom;
    GtkWidget *entry_cosim_board;
    GtkWidget *entry_littlefs;
    GtkWidget *spin_lcd_w;
    GtkWidget *spin_lcd_h;
    GtkWidget *spin_scale;
    GtkWidget *spin_rot;
    GtkWidget *entry_uart;     /* read-only, derived from instance */
    GtkWidget *spin_instance;  /* emulator instance 0..SIM_INSTANCE_MAX */
    GtkWidget *entry_console_mode;
    GtkWidget *entry_extra;
    GtkWidget *entry_emu_path;
    GtkWidget *check_audio;
    GtkWidget *check_audio_realtime;
    GtkWidget *entry_audio_out;
    GtkWidget *entry_audio_in;
    GtkWidget *check_net_user;
    GtkWidget *check_wifi;
    GtkWidget *check_ble;
    GtkWidget *check_littlefs_enabled;
    /* T20 storage/audio/net/caps fields */
    GtkWidget *entry_littlefs_label;
    GtkWidget *entry_littlefs_mount;
    GtkWidget *entry_littlefs_offset;
    GtkWidget *entry_littlefs_size;
    GtkWidget *check_sd_enabled;
    GtkWidget *entry_sd_image;
    GtkWidget *entry_sd_slot;
    GtkWidget *entry_sd_cs;
    GtkWidget *combo_sd_mode;
    GtkWidget *spin_audio_instance;
    GtkWidget *entry_audio_backend;
    GtkWidget *check_net_enabled;
    GtkWidget *entry_net_mode;
    GtkWidget *entry_hostfwd;
    GtkWidget *entry_wifi_ssid;
    GtkWidget *entry_wifi_password;
    GtkWidget *combo_wifi_auth;
    GtkWidget *entry_ble_hci;
    GtkWidget *entry_emu_caps;
    /* Debug/trace (T16/T17) */
    GtkWidget *check_gdb_enabled;
    GtkWidget *spin_gdb_port;
    GtkWidget *check_gdb_halt;
    GtkWidget *check_trace_enabled;
    GtkWidget *entry_trace_path;
    GtkWidget *status_label;
    GtkWidget *path_label;
    GtkWidget *caps_label;
    GtkWidget *canvas_label;
    GtkWidget *canvas;
    GtkTextBuffer *emu_log_buf;
    GtkTextBuffer *host_log_buf;
    GtkTextBuffer *validation_buf;
    GtkTextBuffer *cmd_buf;
    GtkTextBuffer *gpio_buf;
    GtkTextBuffer *debug_buf;
    GtkTextBuffer *workflow_buf;
    GtkWidget *btn_start;
    GtkWidget *btn_stop;
    GtkWidget *btn_restart;
    GtkWidget *combo_run_mode;
    GtkWidget *check_cosim_unix;
    GtkWidget *entry_cosim_unix;
    GtkTextBuffer *telemetry_buf;
    GtkWidget *entry_parts_dir;
    GtkWidget *entry_parts_instance_id;
    GtkWidget *entry_parts_part_name;
    GtkTextBuffer *parts_list_buf;
    GtkTextBuffer *parts_report_buf;
    GtkWidget *entry_log_filter;
    GtkWidget *combo_log_level;
    GtkWidget *combo_log_origin;
    GtkWidget *check_log_follow;
    GtkWidget *notebook_cfg;
    GtkWidget *notebook_bottom;
    GtkWidget *notebook_logs;
    GtkWidget *paned_main;
    GtkWidget *paned_right;
    GtkWidget *paned_parts;
    GtkWidget *paned_left_bottom;
    GtkWidget *revealer_debug;
    GtkWidget *revealer_gpio;
    GtkWidget *revealer_workflow;
    GtkWidget *btn_toggle_debug;
    GtkWidget *btn_toggle_gpio;
    GtkWidget *btn_toggle_workflow;
    /* Main-window layout (menu bar / toolbar / panels): panels are shown on demand from the Ver menu */
    GtkWidget *w_cfg_panel;       /* left: config notebook (hidden by default) */
    GtkWidget *w_dock;            /* bottom dock: 6 tool tabs + 3 log tabs + log filter row (hidden by default) */
    GtkWidget *w_log_bar;         /* log filter row inside the dock */
    GtkWidget *w_statusbar;       /* bottom status bar (badges line) */
    GtkWidget *dock_pages_tools[6];
    GtkWidget *dock_pages_logs[3];
    GtkWidget *mi_start;
    GtkWidget *mi_stop;
    GtkWidget *mi_eject;
    GtkWidget *mi_view_cfg;
    GtkWidget *mi_view_bottom;
    GtkWidget *mi_view_status;
    GtkWidget *mi_view_logs;
    GtkWidget *gpio_page;         /* GPIO/Pins tab page: Ver > Janela de GPIO/Pins shows the dock and focuses it */
    /* Redesign: toolbar badge/timer, nav rail + drawer, canvas summary card, status bar dock toggle */
    GtkWidget *lbl_profile;       /* toolbar: profile name */
    GtkWidget *lbl_board;         /* toolbar: board + resolution */
    GtkWidget *lbl_badge;         /* toolbar: STOPPED / STARTING… / RUNNING */
    GtkWidget *lbl_timer;         /* toolbar: run timer 00:00:00 */
    GtkWidget *rail_btn[4];       /* nav rail items (one per config page) */
    GtkWidget *lbl_drawer_title;  /* drawer header */
    GtkWidget *w_summary;         /* configuration summary card drawn over the canvas while no host is attached */
    GtkWidget *w_summary_hint;    /* discreet hint line under the card */
    GtkWidget *lbl_sum_title;
    GtkWidget *lbl_sum_val[12];
    GtkWidget *btn_dock;          /* status bar toggle for the bottom dock */
    GtkWidget *btn_drawer_close;
    gboolean nav_syncing;         /* guards the menu/rail/dock button feedback loops */
    int cfg_panel_w;              /* last config panel width, restored when it is shown again */
    int dock_h;                   /* last dock height, restored when it is shown again */
    guint log_refresh_id;         /* pending coalesced log view refresh (rebuilding the buffers on every line starved the UI) */
    /* Workflow / code-build-run */
    GtkWidget *entry_build_cmd;
    GtkWidget *entry_workflow_file;
    GtkTextBuffer *workflow_edit_buf;

    SimProc *emu_proc;
    SimProc *bridge_proc;
    SimProc *build_proc;
    SimHost *host;
    telem_state_t telem;
    parts_ui_context_t parts_ctx;
    parts_ui_report_t parts_report;
    launcher_config_t cfg;
    sim_profile_t profile;
    sim_board_catalog_t boards;
    emu_caps_probe_t caps;
    clog_log_t *clog;
    char repo_root[LAUNCHER_MAX_PATH];
    char profile_path[LAUNCHER_MAX_PATH];
    char cosim_unix_actual[LAUNCHER_MAX_PATH];
    gboolean profile_loaded;
    gboolean restart_pending;
    gboolean host_attached;
    gboolean net_started;
    gboolean host_connected;
    gboolean ui_syncing;
    gboolean dirty;
    gboolean restoring_layout;
    guint render_tick_id;
    guint telemetry_tick_id;
    guint layout_save_idle_id;
    gint64 emu_start_us;
    int run_mode; /* 0=ui+host, 1=backend-only (--no-host) */
} App;

/* forward declarations */
static void apply_ui_to_cfg(App *app);
static void sync_ui_from_state(App *app);
static void on_cfg_changed(GtkWidget *w, gpointer data);
static void set_dirty(App *app, gboolean dirty);
static void update_status(App *app);
static void on_notebook_switch(GtkNotebook *nb, GtkWidget *page, guint num, gpointer data) { (void)nb; (void)page; (void)num; App *a = (App*)data; gboolean was = a->dirty; on_cfg_changed(NULL, data); set_dirty(a, was); update_status(a); /* switching a tab is not a config edit */ }
static void update_summary(App *app);
static void nav_sync(App *app);
static void render_validation(App *app, bool run_apply);
static void update_panels(App *app);
static void update_status(App *app);
static const char *card_state(const App *app);
static char *choose_path_dialog(App *app, const char *title, GtkFileChooserAction action, const char *suggested);
static void on_save(GtkButton *b, gpointer data);
static void schedule_layout_save(App *app);
static void save_ui_layout_to_cfg(App *app);
static void restore_ui_layout(App *app);
static void refresh_parts_runtime_hints(App *app);
static void render_debug_gpio_panels(App *app);
static const char *chip_id_for_index(int idx);
static int chip_index_for_id(const char *id);

static void set_buf_text(GtkTextBuffer *buf, const char *txt) { if (buf) gtk_text_buffer_set_text(buf, txt ? txt : "", -1); }

static void set_dirty(App *app, gboolean dirty) {
    if (!app) return;
    app->dirty = dirty;
    if (app->window) {
        char title[256];
        snprintf(title, sizeof(title), "ESP32 Simulator (Unified)%s", dirty ? " *" : "");
        gtk_window_set_title(GTK_WINDOW(app->window), title);
    }
}

typedef struct { GString *gs; clog_origin_t origin; } log_collect_t;
static bool collect_origin_line(const clog_line_t *line, void *user_data) {
    log_collect_t *c = (log_collect_t*)user_data;
    if (!c || !line || line->origin != c->origin) return true;
    char fmt[CLOG_MAX_LINE_TEXT + 64];
    if (clog_format_line(line, fmt, sizeof(fmt)) > 0) g_string_append_printf(c->gs, "%s\n", fmt);
    return true;
}
static void refresh_log_views_now(App *app) {
    if (!app || !app->clog) return;
    /* render by origin using current clog filter; visible subset per tab */
    log_collect_t e = { g_string_new(NULL), CLOG_ORIGIN_EMU };
    log_collect_t h = { g_string_new(NULL), CLOG_ORIGIN_HOST };
    log_collect_t l = { g_string_new(NULL), CLOG_ORIGIN_LAUNCHER };
    log_collect_t v = { g_string_new(NULL), CLOG_ORIGIN_VALIDATION };
    (void)clog_foreach_visible(app->clog, collect_origin_line, &e);
    (void)clog_foreach_visible(app->clog, collect_origin_line, &h);
    (void)clog_foreach_visible(app->clog, collect_origin_line, &l);
    (void)clog_foreach_visible(app->clog, collect_origin_line, &v);
    /* include launcher/validation lines in emu tab for operator visibility */
    g_string_append(e.gs, l.gs->str);
    g_string_append(e.gs, v.gs->str);
    set_buf_text(app->emu_log_buf, e.gs->str);
    set_buf_text(app->host_log_buf, h.gs->str);
    g_string_free(e.gs, TRUE); g_string_free(h.gs, TRUE); g_string_free(l.gs, TRUE); g_string_free(v.gs, TRUE);
}

/* Log views are rebuilt at most every 200 ms and only while the bottom dock is visible (view_apply_dock refreshes on show). */
static gboolean refresh_log_views_cb(gpointer data) {
    App *app = (App*)data; app->log_refresh_id = 0;
    if (app->w_dock && !gtk_widget_get_visible(app->w_dock)) return G_SOURCE_REMOVE;
    refresh_log_views_now(app);
    return G_SOURCE_REMOVE;
}
static void refresh_log_views(App *app) {
    if (!app || !app->clog) return;
    if (app->log_refresh_id == 0) app->log_refresh_id = g_timeout_add_full(G_PRIORITY_LOW, 200, refresh_log_views_cb, app, NULL);
}

static void append_clog(App *app, clog_level_t level, clog_origin_t origin, const char *msg) {
    if (!app || !app->clog || !msg) return;
    (void)clog_append_now(app->clog, level, origin, msg);
    refresh_log_views(app);
}
static void append_emu_log(App *app, const char *msg) { append_clog(app, CLOG_LEVEL_INFO, CLOG_ORIGIN_EMU, msg); }
static void append_host_log(App *app, const char *msg) { append_clog(app, CLOG_LEVEL_HOST, CLOG_ORIGIN_HOST, msg); }
static void append_launcher_log(App *app, const char *msg) { append_clog(app, CLOG_LEVEL_INFO, CLOG_ORIGIN_LAUNCHER, msg); }
static void appendf_log(App *app, gboolean host, const char *fmt, ...) {
    char buf[1024]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    if (host) append_host_log(app, buf); else append_launcher_log(app, buf);
}
static const char *etext(GtkWidget *e) { return (e && GTK_IS_ENTRY(e)) ? gtk_entry_get_text(GTK_ENTRY(e)) : ""; }
static void eset(GtkWidget *e, const char *t) { if (e && GTK_IS_ENTRY(e)) gtk_entry_set_text(GTK_ENTRY(e), t ? t : ""); }

/* Ports come only from the instance: the Runtime spin when built, else cfg.instance. */
static void app_ports(const App *app, sim_ports_t *p) {
    int n = app->spin_instance ? gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->spin_instance)) : app->cfg.instance;
    (void)sim_ports_for_instance(n, p);
}

/* Shows the derived UART and GDB ports (read-only widgets). Guarded so it never re-enters the UI sync. */
static void update_derived_port_widgets(App *app) {
    sim_ports_t p; app_ports(app, &p);
    gboolean was_syncing = app->ui_syncing;
    app->ui_syncing = TRUE;
    char ub[32]; snprintf(ub, sizeof(ub), "127.0.0.1:%d", p.uart);
    eset(app->entry_uart, ub);
    if (app->spin_gdb_port) gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin_gdb_port), p.gdb);
    app->ui_syncing = was_syncing;
}
static bool path_is_abs(const char *p) { return p && p[0] == '/'; }
static void resolve_repo_path(const App *app, const char *in, char *out, size_t outsz) {
    if (!out || !outsz) return;
    out[0] = '\0';
    if (!in || !in[0]) return;
    if (path_is_abs(in) || !app || !app->repo_root[0]) snprintf(out, outsz, "%s", in);
    else snprintf(out, outsz, "%s/%s", app->repo_root, in);
}
static bool path_exists_repo(const App *app, const char *p) {
    char rp[PATH_MAX]; if (!p || !p[0]) return false; resolve_repo_path(app, p, rp, sizeof(rp)); return access(rp, F_OK) == 0;
}

static const char *k_chip_ids[] = {
    "esp32s3", "esp32c3", "esp32c5", "esp32c6", "esp32h2", "esp32p4", "esp32s31"
};
static const char *chip_id_for_index(int idx) {
    if (idx < 0 || idx >= (int)(sizeof(k_chip_ids)/sizeof(k_chip_ids[0]))) return "esp32s3";
    return k_chip_ids[idx];
}
static int chip_index_for_id(const char *id) {
    if (!id || !id[0]) return 0;
    for (int i = 0; i < (int)(sizeof(k_chip_ids)/sizeof(k_chip_ids[0])); i++) if (strcmp(id, k_chip_ids[i]) == 0) return i;
    return 0;
}

/* Profile auth string -> audio_net enum (inverse of audio_net_wifi_auth_str). */
static audio_net_wifi_auth_t wifi_auth_from_str(const char *s) {
    if (!s) return AUDIO_NET_WIFI_AUTH_UNKNOWN;
    if (strcmp(s, "open") == 0) return AUDIO_NET_WIFI_AUTH_OPEN;
    if (strcmp(s, "wpa2-wpa3") == 0) return AUDIO_NET_WIFI_AUTH_WPA_PSK;
    if (strcmp(s, "wpa2-psk") == 0) return AUDIO_NET_WIFI_AUTH_WPA2_PSK;
    if (strcmp(s, "wpa3-sae") == 0) return AUDIO_NET_WIFI_AUTH_WPA3_PSK;
    return AUDIO_NET_WIFI_AUTH_UNKNOWN;
}

static void cfg_apply_profile_shared(App *app, const launcher_config_t *prev) {
    launcher_config_t *c = &app->cfg;
    const sim_profile_t *p = &app->profile;
    if (!c || !p) return;

    /* Shared profile fields become authoritative; conf-only extras stay from prev. */
    c->storage.littlefs.enabled = p->storage.littlefs_enabled;
    if (p->storage.littlefs_image_path[0])
        slfs_littlefs_set_image(&c->storage.littlefs, p->storage.littlefs_image_path);
    else
        c->storage.littlefs.image_path[0] = '\0';
    if (p->storage.littlefs_mount_path[0])
        slfs_littlefs_set_mount(&c->storage.littlefs, p->storage.littlefs_mount_path);
    else
        c->storage.littlefs.mount_path[0] = '\0';
    c->storage.sd.enabled = p->storage.sd_enabled;
    if (p->storage.sd_image_path[0])
        slfs_sd_set_image(&c->storage.sd, p->storage.sd_image_path);
    else
        c->storage.sd.image_path[0] = '\0';

    c->audio_net.i2s_enabled = c->audio;
    c->audio_net.i2s_realtime = p->audio.realtime;
    snprintf(c->audio_net.i2s_wav_out, sizeof(c->audio_net.i2s_wav_out), "%s", p->audio.out);
    snprintf(c->audio_net.i2s_wav_in, sizeof(c->audio_net.i2s_wav_in), "%s", p->audio.in);

    c->audio_net.net_enabled = p->network_radio.net_user || c->net_user;
    audio_net_clear_hostfwd(&c->audio_net);
    for (size_t i = 0; i < p->network_radio.hostfwd_count && i < AUDIO_NET_MAX_HOSTFWD; ++i)
        (void)audio_net_add_hostfwd(&c->audio_net, p->network_radio.hostfwd[i]);
    c->audio_net.wifi_enabled = p->network_radio.wifi_enabled;
    snprintf(c->audio_net.wifi_ssid, sizeof(c->audio_net.wifi_ssid), "%s", p->network_radio.wifi_ssid);
    audio_net_set_wifi_auth(&c->audio_net, wifi_auth_from_str(p->network_radio.wifi_auth));
    c->audio_net.ble_enabled = p->network_radio.ble_enabled;
    snprintf(c->audio_net.ble_hci, sizeof(c->audio_net.ble_hci), "%s", p->network_radio.ble_hci);
    snprintf(c->audio_net.net_mode, sizeof(c->audio_net.net_mode), "%s", p->network_radio.net_mode);

    /* Legacy conf import keeps helper fields absent from the profile schema. */
    if (prev && strcmp(p->metadata.id, "imported-launcher-conf") == 0) {
        c->storage = prev->storage;
        c->audio_net = prev->audio_net;
        snprintf(c->emu_caps_path, sizeof(c->emu_caps_path), "%s", prev->emu_caps_path);
        c->audio = prev->audio;
        c->net_user = prev->net_user;
    }

    if (prev) {
        /* LittleFS partition details and SD model hints are not in profile schema. */
        snprintf(c->storage.littlefs.partition_label, sizeof(c->storage.littlefs.partition_label),
                 "%s", prev->storage.littlefs.partition_label);
        c->storage.littlefs.offset = prev->storage.littlefs.offset;
        c->storage.littlefs.size = prev->storage.littlefs.size;
        c->storage.littlefs.image_optional = prev->storage.littlefs.image_optional;
        c->storage.sd.mode = prev->storage.sd.mode;
        c->storage.sd.read_only = prev->storage.sd.read_only;
        c->storage.sd.slot = prev->storage.sd.slot;
        c->storage.sd.cs_pin = prev->storage.sd.cs_pin;
        /* Audio/net extras not represented in profile. */
        c->audio_net.i2s_instance = prev->audio_net.i2s_instance;
        snprintf(c->audio_net.i2s_backend_hint, sizeof(c->audio_net.i2s_backend_hint),
                 "%s", prev->audio_net.i2s_backend_hint);
        /* SSID, auth, BLE HCI and net mode come from the profile; only the passphrase stays in launcher.conf. */
        snprintf(c->audio_net.wifi_password, sizeof(c->audio_net.wifi_password), "%s", prev->audio_net.wifi_password);
        snprintf(c->emu_caps_path, sizeof(c->emu_caps_path), "%s", prev->emu_caps_path);
    }

    if (c->audio_net.net_enabled && !c->audio_net.net_mode[0])
        audio_net_set_net_mode(&c->audio_net, "user");
    c->net_user = c->audio_net.net_enabled &&
                  (strcmp(c->audio_net.net_mode, "user") == 0 || !c->audio_net.net_mode[0]);
    c->audio = c->audio_net.i2s_enabled;
}

static void sync_cfg_from_profile(App *app) {
    if (!app) return;
    launcher_config_t prev = app->cfg;
    sim_profile_to_launcher_config(&app->profile, &app->cfg);
    cfg_apply_profile_shared(app, &prev);
    if (app->profile_path[0]) snprintf(app->cfg.profile_path, sizeof(app->cfg.profile_path), "%s", app->profile_path);
    launcher_config_resolve_paths(&app->cfg);
}

static void ensure_profile_path(App *app) {
    if (!app) return;
    if (app->cfg.profile_path[0]) snprintf(app->profile_path, sizeof(app->profile_path), "%s", app->cfg.profile_path);
    else if (!app->profile_path[0]) snprintf(app->profile_path, sizeof(app->profile_path), "%s", ACTIVE_PROFILE_PATH);
}

static void apply_board_catalog_hints(App *app, bool fill_missing_only) {
    if (!app) return;
    const sim_board_t *b = sim_board_catalog_get(&app->boards, launcher_board_id(app->cfg.board));
    if (!b) return;
    const char *cosim = sim_board_cosim_json(b);
    if (cosim && cosim[0] && (!fill_missing_only || !app->cfg.cosim_board_path[0])) {
        snprintf(app->cfg.cosim_board_path, sizeof(app->cfg.cosim_board_path), "%s", cosim);
        snprintf(app->profile.board_hardware.cosim.board_json, sizeof(app->profile.board_hardware.cosim.board_json), "%s", cosim);
        snprintf(app->profile.board_hardware.cosim.mode, sizeof(app->profile.board_hardware.cosim.mode), "board_json");
    }
    if (!fill_missing_only || app->profile.board_hardware.display.width <= 0) app->profile.board_hardware.display.width = b->display.width;
    if (!fill_missing_only || app->profile.board_hardware.display.height <= 0) app->profile.board_hardware.display.height = b->display.height;
    snprintf(app->profile.board_hardware.board_id, sizeof(app->profile.board_hardware.board_id), "%s", launcher_board_id(app->cfg.board));
    snprintf(app->profile.metadata.board_preset, sizeof(app->profile.metadata.board_preset), "%s", launcher_board_id(app->cfg.board));
    if (app->cfg.firmware == LAUNCHER_FW_KRYONOS_V3) {
        const char *m = sim_board_preset_merged(b, "kryonos_v3"); if (m && m[0]) snprintf(app->cfg.merged_image_path, sizeof(app->cfg.merged_image_path), "%s", m);
        app->cfg.use_elf = false; app->cfg.elf_path[0] = '\0';
    } else if (app->cfg.firmware == LAUNCHER_FW_ASSET_ES3C28P) {
        const char *m = sim_board_preset_merged(b, "asset_es3c28p"); if (m && m[0]) snprintf(app->cfg.merged_image_path, sizeof(app->cfg.merged_image_path), "%s", m);
        const char *e = sim_board_preset_elf(b, "asset_es3c28p"); if (e && e[0]) snprintf(app->cfg.elf_path, sizeof(app->cfg.elf_path), "%s", e);
        app->cfg.use_elf = sim_board_preset_use_elf(b, "asset_es3c28p");
    } else if (app->cfg.firmware == LAUNCHER_FW_LOCAL_ES3C28P_EMU) {
        const char *m = sim_board_preset_merged(b, "local"); if (m && m[0]) snprintf(app->cfg.merged_image_path, sizeof(app->cfg.merged_image_path), "%s", m);
        const char *e = sim_board_preset_elf(b, "local"); if (e && e[0]) snprintf(app->cfg.elf_path, sizeof(app->cfg.elf_path), "%s", e);
        if (e && e[0]) app->cfg.use_elf = sim_board_preset_use_elf(b, "local");
    }
    snprintf(app->profile.firmware.selection, sizeof(app->profile.firmware.selection), "%s", launcher_firmware_name(app->cfg.firmware));
    snprintf(app->profile.firmware.artifacts.merged, sizeof(app->profile.firmware.artifacts.merged), "%s", app->cfg.merged_image_path);
    snprintf(app->profile.firmware.artifacts.elf, sizeof(app->profile.firmware.artifacts.elf), "%s", app->cfg.elf_path);
    app->profile.firmware.artifacts.use_elf = app->cfg.use_elf;
}

static void log_profile_diags(App *app, const sim_profile_diags_t *d, const char *ctx) {
    if (!app || !d) return;
    for (size_t i = 0; i < d->count; i++) {
        const sim_profile_diag_t *it = &d->items[i];
        const char *lvl = it->level == SIM_PROFILE_DIAG_ERROR ? "error" : (it->level == SIM_PROFILE_DIAG_WARNING ? "warn" : "info");
        appendf_log(app, FALSE, "[profile:%s] %s %s: %s", ctx ? ctx : "?", lvl, it->path, it->message);
    }
}

static void load_profile_or_migrate(App *app) {
    if (!app) return;
    sim_profile_diags_t diags; sim_profile_diags_init(&diags);
    sim_board_catalog_init(&app->boards); sim_board_catalog_load_dir(&app->boards, BOARDS_DIR_PATH);
    launcher_config_defaults(&app->cfg); (void)launcher_config_load(&app->cfg, LAUNCHER_CONF_PATH);
    ensure_profile_path(app); launcher_config_resolve_paths(&app->cfg);
    bool loaded = false; char profile_abs[PATH_MAX]; resolve_repo_path(app, app->profile_path, profile_abs, sizeof(profile_abs));
    if (app->profile_path[0] && access(profile_abs, R_OK) == 0) {
        sim_profile_diags_free(&diags); sim_profile_diags_init(&diags);
        if (sim_profile_load_json(&app->profile, profile_abs, &diags) == 0) {
            loaded = true; app->profile_loaded = TRUE; sync_cfg_from_profile(app); log_profile_diags(app, &diags, "load");
            appendf_log(app, FALSE, "[profile] loaded %s", app->profile_path);
        } else { log_profile_diags(app, &diags, "load"); appendf_log(app, FALSE, "[profile] failed to load %s, falling back to launcher.conf import", app->profile_path); }
    }
    if (!loaded) {
        sim_profile_diags_free(&diags); sim_profile_diags_init(&diags);
        if (sim_profile_import_launcher_conf(&app->profile, LAUNCHER_CONF_PATH, app->repo_root, &diags) == 0) {
            app->profile_loaded = FALSE; sync_cfg_from_profile(app); log_profile_diags(app, &diags, "import");
            append_emu_log(app, "[profile] imported legacy launcher.conf (will save as profile on Save)");
        } else {
            sim_profile_defaults(&app->profile); sync_cfg_from_profile(app);
            append_emu_log(app, "[profile] using defaults (launcher.conf/profile missing)");
        }
    }
    launcher_config_resolve_paths(&app->cfg); apply_board_catalog_hints(app, true);
    if (!app->profile.emu_runtime.console_mode[0]) snprintf(app->profile.emu_runtime.console_mode, sizeof(app->profile.emu_runtime.console_mode), "tcp");
    if (!app->profile.audio.out[0]) snprintf(app->profile.audio.out, sizeof(app->profile.audio.out), "simulator/logs/i2s_out.wav");
    sim_profile_diags_free(&diags);
}

static void sync_profile_paths_from_entries(App *app) {
    if (!app) return;
    snprintf(app->profile_path, sizeof(app->profile_path), "%s", etext(app->entry_profile_path));
    if (!app->profile_path[0]) snprintf(app->profile_path, sizeof(app->profile_path), "%s", ACTIVE_PROFILE_PATH);
}

static void persist_active_profile_conf(App *app) {
    if (!app) return;
    ensure_profile_path(app);
    snprintf(app->cfg.profile_path, sizeof(app->cfg.profile_path), "%s", app->profile_path);
    launcher_config_save(&app->cfg, LAUNCHER_CONF_PATH);
}

/* Profile files always end in .json: returns a new string, the input plus ".json" when it has no .json suffix. */
static char *profile_json_name(const char *path) {
    gchar *lower = g_ascii_strdown(path, -1);
    gboolean has_json = g_str_has_suffix(lower, ".json");
    g_free(lower);
    return has_json ? g_strdup(path) : g_strconcat(path, ".json", NULL);
}

static gboolean save_profile_to_path(App *app, const char *path, bool set_active, bool announce) {
    if (!app || !path || !path[0]) return FALSE;
    /* Copy the target first: callers may pass app->profile_path itself. Only set_active moves the active path;
     * an export writes the file and leaves app->profile_path and cfg.profile_path alone. */
    char target[LAUNCHER_MAX_PATH]; snprintf(target, sizeof(target), "%s", path);
    char prev_active[LAUNCHER_MAX_PATH]; snprintf(prev_active, sizeof(prev_active), "%s", app->profile_path);
    apply_ui_to_cfg(app);
    if (set_active) {
        snprintf(app->profile_path, sizeof(app->profile_path), "%s", target);
        snprintf(app->cfg.profile_path, sizeof(app->cfg.profile_path), "%s", target);
    }
    snprintf(app->profile.metadata.board_preset, sizeof(app->profile.metadata.board_preset), "%s", launcher_board_id(app->cfg.board));
    snprintf(app->profile.board_hardware.board_id, sizeof(app->profile.board_hardware.board_id), "%s", launcher_board_id(app->cfg.board));
    snprintf(app->profile.firmware.selection, sizeof(app->profile.firmware.selection), "%s", launcher_firmware_name(app->cfg.firmware));
    snprintf(app->profile.firmware.artifacts.merged, sizeof(app->profile.firmware.artifacts.merged), "%s", app->cfg.merged_image_path);
    snprintf(app->profile.firmware.artifacts.elf, sizeof(app->profile.firmware.artifacts.elf), "%s", app->cfg.elf_path);
    app->profile.firmware.artifacts.use_elf = app->cfg.use_elf;
    snprintf(app->profile.board_hardware.cosim.board_json, sizeof(app->profile.board_hardware.cosim.board_json), "%s", app->cfg.cosim_board_path);
    app->profile.board_hardware.display.hw_rotation = app->cfg.hw_rotation;
    app->profile.ui_state.scale = (double)app->cfg.scale;
    app->profile.emu_runtime.instance = app->cfg.instance;
    snprintf(app->profile.emu_runtime.extra_args, sizeof(app->profile.emu_runtime.extra_args), "%s", app->cfg.extra_args);
    app->profile.network_radio.net_user = app->cfg.net_user; app->profile.audio.enabled = app->cfg.audio;
    {
        /* Net/Wi-Fi/BLE values; the Wi-Fi passphrase is never copied into the profile. */
        const audio_net_config_t *an = &app->cfg.audio_net;
        snprintf(app->profile.network_radio.net_mode, sizeof(app->profile.network_radio.net_mode), "%s", an->net_enabled ? an->net_mode : "");
        app->profile.network_radio.wifi_enabled = an->wifi_enabled;
        snprintf(app->profile.network_radio.wifi_ssid, sizeof(app->profile.network_radio.wifi_ssid), "%s", an->wifi_ssid);
        snprintf(app->profile.network_radio.wifi_auth, sizeof(app->profile.network_radio.wifi_auth), "%s", audio_net_wifi_auth_str(an->wifi_auth));
        app->profile.network_radio.ble_enabled = an->ble_enabled;
        snprintf(app->profile.network_radio.ble_hci, sizeof(app->profile.network_radio.ble_hci), "%s", an->ble_hci);
    }

    sim_profile_diags_t diags; sim_profile_diags_init(&diags);
    char profile_abs[PATH_MAX]; resolve_repo_path(app, target, profile_abs, sizeof(profile_abs));
    char *dir = g_path_get_dirname(profile_abs);
    if (dir && dir[0] && strcmp(dir, ".") != 0) (void)g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    gboolean ok = (sim_profile_save_json(&app->profile, profile_abs, &diags) == 0);
    if (ok) {
        if (set_active) { app->profile_loaded = TRUE; set_dirty(app, FALSE); }
        log_profile_diags(app, &diags, "save");
        if (announce) appendf_log(app, FALSE, _("[profile] saved: %s%s"), target, set_active ? _(" (active)") : " (export)");
    } else {
        log_profile_diags(app, &diags, "save");
        appendf_log(app, FALSE, _("[profile] failed to save %s"), profile_abs);
        if (set_active) { /* a failed Save as / Duplicate keeps the previous active file */
            snprintf(app->profile_path, sizeof(app->profile_path), "%s", prev_active);
            snprintf(app->cfg.profile_path, sizeof(app->cfg.profile_path), "%s", prev_active);
        }
    }
    sim_profile_diags_free(&diags);
    if (set_active) persist_active_profile_conf(app);
    return ok ? TRUE : FALSE;
}

static void save_profile_and_conf(App *app) {
    if (!app) return;
    sync_profile_paths_from_entries(app);
    ensure_profile_path(app);
    (void)save_profile_to_path(app, app->profile_path, true, false);
}

/* Session state only: the UI goes into the in-memory cfg/profile and launcher.conf is written. The profile file is untouched. */
static void save_conf_only(App *app) {
    if (!app) return;
    apply_ui_to_cfg(app);
    persist_active_profile_conf(app);
}

static void effective_audio_net_local(const launcher_config_t *cfg, audio_net_config_t *out) {
    if (!cfg || !out) return;
    *out = cfg->audio_net;
    if (!cfg->audio) {
        out->i2s_enabled = false;
        out->i2s_wav_out[0] = '\0';
        out->i2s_wav_in[0] = '\0';
        out->i2s_instance = -1;
    } else if (!out->i2s_enabled && out->i2s_wav_out[0]) {
        out->i2s_enabled = true;
    }
    if (cfg->net_user && !out->net_enabled) {
        out->net_enabled = true;
        audio_net_set_net_mode(out, "user");
    }
    if (out->net_enabled && !out->net_mode[0]) audio_net_set_net_mode(out, "user");
}

/* Drop helper args the probed esp-emu does not advertise; record why. */
static void filter_audio_net_for_probe(const App *app, audio_net_config_t *an, GString *notes) {
    if (!app || !an) return;
    const emu_caps_probe_t *p = emu_caps_probe_is_probed(&app->caps) ? &app->caps : NULL;
    if (!p) return;
    if (!p->flag_i2s_wav && (an->i2s_enabled || an->i2s_wav_out[0] || an->i2s_wav_in[0])) {
        if (notes) g_string_append(notes, "esp-emu has no --i2s-wav; I2S args omitted\n");
        an->i2s_enabled = false;
        an->i2s_wav_out[0] = '\0';
        an->i2s_wav_in[0] = '\0';
    }
    if (an->i2s_wav_in[0] && !strstr(p->help_sample, "--i2s-wav-in")) {
        an->i2s_wav_in[0] = '\0';
    }
    if (an->i2s_instance >= 0 && !strstr(p->help_sample, "--i2s-instance")) an->i2s_instance = -1;
    if (!p->flag_net && an->net_enabled) {
        if (notes) g_string_append(notes, "esp-emu has no --net; net args omitted\n");
        an->net_enabled = false;
        an->net_mode[0] = '\0';
    }
    if (an->hostfwd_count > 0 && !strstr(p->help_sample, "--hostfwd")) {
        if (notes) g_string_append(notes, "esp-emu has no --hostfwd; hostfwd rules kept but not emitted\n");
        an->hostfwd_count = 0;
    }
    if (!p->flag_wifi && an->wifi_enabled) {
        if (notes) g_string_append(notes, "esp-emu has no --wifi-*; Wi-Fi args omitted\n");
        an->wifi_enabled = false;
    }
    if (!p->flag_ble_hci && an->ble_enabled) {
        if (notes) g_string_append(notes, "esp-emu has no --ble-hci; BLE args omitted\n");
        an->ble_enabled = false;
    }
    (void)p;
}

static int cmd_append(char *buf, size_t buflen, size_t *off, const char *fmt, ...) {
    if (!buf || !off || !buflen || *off >= buflen) return -1;
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf + *off, buflen - *off, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= buflen - *off) return -1;
    *off += (size_t)n;
    return 0;
}

/* Appends lead + value, the value quoted for sh -c (g_shell_quote: '...' with ' escaped). */
static int cmd_append_quoted(char *buf, size_t buflen, size_t *off, const char *lead, const char *value) {
    gchar *q = g_shell_quote(value ? value : "");
    int rc = cmd_append(buf, buflen, off, "%s%s", lead, q);
    g_free(q);
    return rc;
}

/* A flag token is emitted bare only when it is made of [a-z0-9-] (no shell metacharacters). */
static gboolean cmd_is_plain_flag(const char *s) {
    if (!s || s[0] != '-' || s[1] != '-' || !s[2]) return FALSE;
    for (const char *p = s; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-')) return FALSE;
    return TRUE;
}

static void slfs_probe_version(const App *app, int *maj, int *min, int *pat) {
    *maj = 1; *min = 0; *pat = 0;
    if (app && emu_caps_probe_is_probed(&app->caps) && app->caps.version_known) {
        *maj = app->caps.version_major;
        *min = app->caps.version_minor;
        *pat = app->caps.version_patch;
    }
}

static int append_emu_tail(App *app, char *out, size_t outsz, size_t *off, GString *notes) {
    audio_net_config_t an;
    effective_audio_net_local(&app->cfg, &an);
    filter_audio_net_for_probe(app, &an, notes);
    audio_net_args_t args;
    if (audio_net_format_emu_args(&an, &args) < 0) return -1;

    slfs_config_t sc = app->cfg.storage;
    int maj, min, pat; slfs_probe_version(app, &maj, &min, &pat);
    (void)slfs_apply_capabilities(&sc, maj, min, pat, true);
    char sd[SLFS_MAX_ARGS], sd_note[SLFS_MAX_MSG];
    int sd_n = slfs_build_sd_args(&sc, sd, sizeof(sd), sd_note, sizeof(sd_note));
    if (sd_n < 0) return -1;
    if (sd_n == 0 && sc.sd.enabled && notes)
        g_string_append_printf(notes, "SD requested: %s\n", sc.sd.capability_note[0] ? sc.sd.capability_note : "no --sd emitted");

    /* Always forward the cosim-tcp endpoint, then any enabled helper args. */
    if (cmd_append(out, outsz, off, " --") != 0) return -1;
    sim_ports_t ports; app_ports(app, &ports);
    if (cmd_append(out, outsz, off, " --cosim-tcp 127.0.0.1:%d", ports.cosim) != 0) return -1;
    if (cmd_append(out, outsz, off, " --uart-tcp 127.0.0.1:%d", ports.uart) != 0) return -1;
    /* audio_net args are always flag/value pairs; an empty value (open Wi-Fi password) stays as ''. */
    for (int i = 0; i < args.argc; i += 2) {
        const char *flag = args.argv[i];
        if (cmd_is_plain_flag(flag)) { if (cmd_append(out, outsz, off, " %s", flag) != 0) return -1; }
        else if (cmd_append_quoted(out, outsz, off, " ", flag) != 0) return -1;
        if (i + 1 < args.argc &&
            cmd_append_quoted(out, outsz, off, " ", args.argv[i + 1]) != 0) return -1;
    }
    if (sd_n > 0) {
        /* same "--sd PATH[:ro]" as slfs_build_sd_args, with the path quoted */
        gchar *q = g_shell_quote(sc.sd.image_path);
        int rc = cmd_append(out, outsz, off, " --sd %s%s", q, sc.sd.read_only ? ":ro" : "");
        g_free(q);
        if (rc != 0) return -1;
    }
    if (app->check_gdb_enabled && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_gdb_enabled))) {
        if (cmd_append(out, outsz, off, " --gdb %d", ports.gdb) != 0) return -1;
        if (app->check_gdb_halt && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_gdb_halt)))
            if (cmd_append(out, outsz, off, " --gdb-halt") != 0) return -1;
    }
    if (app->check_trace_enabled && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_trace_enabled))) {
        const char *tp = app->entry_trace_path ? gtk_entry_get_text(GTK_ENTRY(app->entry_trace_path)) : "";
        if (tp && tp[0] && cmd_append_quoted(out, outsz, off, " --trace ", tp) != 0) return -1;
    }
    if (app->cfg.extra_args[0] &&
        cmd_append(out, outsz, off, " %s", app->cfg.extra_args) != 0) return -1;
    return 0;
}

static int build_emu_command(App *app, char *out, size_t outsz, GString *notes) {
    if (!app || !out || !outsz) return -1;
    size_t off = 0;
    out[0] = '\0';
    if (cmd_append(out, outsz, &off, "./start.sh emu run") != 0) return -1;
    if (cmd_append_quoted(out, outsz, &off, " --firmware ", app->cfg.merged_image_path) != 0) return -1;
    if (app->cfg.use_elf && app->cfg.elf_path[0]) {
        if (cmd_append_quoted(out, outsz, &off, " --elf ", app->cfg.elf_path) != 0) return -1;
    } else if (cmd_append(out, outsz, &off, " --no-elf") != 0) return -1;
    if (app->cfg.cosim_board_path[0] &&
        cmd_append_quoted(out, outsz, &off, " --board ", app->cfg.cosim_board_path) != 0) return -1;
    /* start.sh adds its default --sd itself: an ejected card or a custom image needs --no-sd first */
    char sd_pre[64];
    if (launcher_config_sd_pre_args(&app->cfg, sd_pre, sizeof(sd_pre)) == 0 && sd_pre[0] &&
        cmd_append(out, outsz, &off, "%s", sd_pre) != 0) return -1;
    if (append_emu_tail(app, out, outsz, &off, notes) != 0) return -1;
    /* a custom card image goes after the "--" of the tail, next to --no-sd; the helper only says whether it applies */
    char sd_tail[PATH_MAX + 16];
    if (launcher_config_sd_tail_args(&app->cfg, sd_tail, sizeof(sd_tail)) == 0 && sd_tail[0] &&
        cmd_append_quoted(out, outsz, &off, " --sd ", app->cfg.sd_image_path) != 0) return -1;
    return 0;
}

static void append_storage_merge_notes(App *app, GString *gs) {
    if (!app || !gs) return;
    slfs_config_t sc = app->cfg.storage;
    int maj, min, pat; slfs_probe_version(app, &maj, &min, &pat);
    (void)slfs_apply_capabilities(&sc, maj, min, pat, true);
    char merge[SLFS_MAX_ARGS], note[SLFS_MAX_MSG];
    int n = slfs_build_merge_args(&sc, merge, sizeof(merge), note, sizeof(note));
    if (n > 0 && merge[0]) g_string_append_printf(gs, "\n# merge step (not a run arg): %s", merge);
    else if (sc.littlefs.enabled) g_string_append_printf(gs, "\n# LittleFS: %s", note[0] ? note : slfs_capability_str(sc.littlefs.capability));
    char sd[SLFS_MAX_ARGS], sd_note[SLFS_MAX_MSG];
    int sn = slfs_build_sd_args(&sc, sd, sizeof(sd), sd_note, sizeof(sd_note));
    if (sn > 0 && sd[0]) g_string_append_printf(gs, "\n# future SD arg: %s", sd);
    else if (sc.sd.enabled) g_string_append_printf(gs, "\n# SD: %s", sd_note[0] ? sd_note : "no --sd emitted");
}

/* Display and log copy of an esp-emu command: the Wi-Fi passphrase shows as '***'.
 * The command that runs keeps the real value. Returns a g_malloc'd string. */
static const char *shell_word_end(const char *s) {
    while (*s && *s != ' ') {
        if (*s == '\'') { s++; while (*s && *s != '\'') s++; if (*s) s++; }
        else if (*s == '"') {
            s++;
            while (*s && *s != '"') { if (*s == '\\' && s[1]) s++; s++; }
            if (*s) s++;
        }
        else if (*s == '\\' && s[1]) s += 2;
        else s++;
    }
    return s;
}

static char *redact_wifi_password(const char *cmd) {
    static const char flag[] = "--wifi-password ";
    GString *g = g_string_new(NULL);
    const char *p = cmd ? cmd : "";
    const char *hit;
    while ((hit = strstr(p, flag)) != NULL) {
        const char *v = hit + sizeof(flag) - 1;
        g_string_append_len(g, p, (gssize)(v - p));
        if (v[0] == '-' && v[1] == '-') { p = v; continue; }   /* no value after the flag */
        const char *end = shell_word_end(v);
        /* the empty passphrase (open AP, '') stays visible as empty; any other value shows as ***. */
        if (end - v == 2 && v[0] == '\'' && v[1] == '\'') g_string_append(g, "''");
        else g_string_append(g, "'***'");
        p = end;
    }
    g_string_append(g, p);
    return g_string_free(g, FALSE);
}

static void build_effective_command_preview(App *app, char *out, size_t outsz) {
    if (!app || !out || !outsz) return;
    GString *notes = g_string_new(NULL);
    if (build_emu_command(app, out, outsz, notes) != 0) {
        snprintf(out, outsz, _("# error building effective command"));
        g_string_free(notes, TRUE);
        return;
    }
    if (notes->len) {
        size_t used = strlen(out);
        snprintf(out + used, outsz > used ? outsz - used : 0, "\n# %s", notes->str);
    }
    g_string_free(notes, TRUE);
    GString *merge = g_string_new(NULL);
    append_storage_merge_notes(app, merge);
    if (merge->len) {
        size_t used = strlen(out);
        snprintf(out + used, outsz > used ? outsz - used : 0, "%s", merge->str);
    }
    g_string_free(merge, TRUE);
    char *shown = redact_wifi_password(out);
    snprintf(out, outsz, "%s", shown);
    g_free(shown);
}

static uint32_t parse_u32_str(const char *s) {
    if (!s || !s[0]) return 0;
    return (uint32_t)strtoul(s, NULL, 0);
}

static int parse_int_str(const char *s, int def) {
    if (!s || !s[0]) return def;
    return (int)strtol(s, NULL, 0);
}

static void apply_ui_to_cfg(App *app) {
    if (!app || app->ui_syncing) return;
    gint bi = gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo_board));
    gint fi = gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo_firmware));
    if (bi >= 0 && bi < LAUNCHER_BOARD_COUNT) app->cfg.board = (launcher_board_t)bi;
    if (fi >= 0 && fi < LAUNCHER_FW_COUNT) app->cfg.firmware = (launcher_firmware_t)fi;
    app->cfg.scale = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->spin_scale));
    app->cfg.hw_rotation = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->spin_rot));
    app->cfg.lcd_w = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->spin_lcd_w));
    app->cfg.lcd_h = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->spin_lcd_h));
    app->cfg.audio = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_audio));
    app->cfg.net_user = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_net_user));
    app->cfg.use_elf = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_use_elf));
    if (app->spin_instance) app->cfg.instance = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->spin_instance));
    update_derived_port_widgets(app);
    snprintf(app->cfg.extra_args, sizeof(app->cfg.extra_args), "%s", etext(app->entry_extra));
    snprintf(app->cfg.custom_firmware_path, sizeof(app->cfg.custom_firmware_path), "%s", etext(app->entry_custom));
    snprintf(app->cfg.cosim_board_path, sizeof(app->cfg.cosim_board_path), "%s", etext(app->entry_cosim_board));
    snprintf(app->cfg.merged_image_path, sizeof(app->cfg.merged_image_path), "%s", etext(app->entry_merged));
    snprintf(app->cfg.elf_path, sizeof(app->cfg.elf_path), "%s", etext(app->entry_elf));
    if (app->cfg.firmware == LAUNCHER_FW_CUSTOM && etext(app->entry_custom)[0] && !etext(app->entry_merged)[0])
        snprintf(app->cfg.merged_image_path, sizeof(app->cfg.merged_image_path), "%s", etext(app->entry_custom));
    launcher_config_resolve_paths(&app->cfg);
    apply_board_catalog_hints(app, true);
    if (etext(app->entry_merged)[0]) snprintf(app->cfg.merged_image_path, sizeof(app->cfg.merged_image_path), "%s", etext(app->entry_merged));
    if (etext(app->entry_elf)[0] || !app->cfg.use_elf) snprintf(app->cfg.elf_path, sizeof(app->cfg.elf_path), "%s", etext(app->entry_elf));
    app->cfg.use_elf = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_use_elf));
    if (etext(app->entry_cosim_board)[0]) snprintf(app->cfg.cosim_board_path, sizeof(app->cfg.cosim_board_path), "%s", etext(app->entry_cosim_board));
    launcher_config_resolve_paths(&app->cfg);

    snprintf(app->profile.firmware.selection, sizeof(app->profile.firmware.selection), "%s", launcher_firmware_name(app->cfg.firmware));
    snprintf(app->profile.firmware.artifacts.merged, sizeof(app->profile.firmware.artifacts.merged), "%s", app->cfg.merged_image_path);
    snprintf(app->profile.firmware.artifacts.elf, sizeof(app->profile.firmware.artifacts.elf), "%s", app->cfg.elf_path);
    app->profile.firmware.artifacts.use_elf = app->cfg.use_elf;
    snprintf(app->profile.board_hardware.cosim.board_json, sizeof(app->profile.board_hardware.cosim.board_json), "%s", app->cfg.cosim_board_path);
    if (app->cfg.cosim_board_path[0]) snprintf(app->profile.board_hardware.cosim.mode, sizeof(app->profile.board_hardware.cosim.mode), "board_json");
    app->profile.board_hardware.display.hw_rotation = app->cfg.hw_rotation;
    if (app->cfg.lcd_w > 0) app->profile.board_hardware.display.width = app->cfg.lcd_w;
    if (app->cfg.lcd_h > 0) app->profile.board_hardware.display.height = app->cfg.lcd_h;
    app->profile.audio.enabled = app->cfg.audio;
    app->profile.audio.realtime = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_audio_realtime));
    snprintf(app->profile.audio.out, sizeof(app->profile.audio.out), "%s", etext(app->entry_audio_out));
    snprintf(app->profile.audio.in, sizeof(app->profile.audio.in), "%s", etext(app->entry_audio_in));
    app->profile.network_radio.net_user = app->cfg.net_user;
    app->profile.network_radio.wifi_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_wifi));
    app->profile.network_radio.ble_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_ble));
    app->profile.emu_runtime.instance = app->cfg.instance;
    snprintf(app->profile.emu_runtime.extra_args, sizeof(app->profile.emu_runtime.extra_args), "%s", etext(app->entry_extra));
    snprintf(app->profile.emu_runtime.console_mode, sizeof(app->profile.emu_runtime.console_mode), "%s", etext(app->entry_console_mode)[0]?etext(app->entry_console_mode):"tcp");
    if (etext(app->entry_emu_path)[0]) snprintf(app->profile.emu_runtime.esp_emu_path, sizeof(app->profile.emu_runtime.esp_emu_path), "%s", etext(app->entry_emu_path));
    app->profile.storage.littlefs_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_littlefs_enabled));
    snprintf(app->profile.storage.littlefs_image_path, sizeof(app->profile.storage.littlefs_image_path), "%s", etext(app->entry_littlefs));
    snprintf(app->profile.firmware.artifacts.littlefs, sizeof(app->profile.firmware.artifacts.littlefs), "%s", etext(app->entry_littlefs));
    /* T20 storage/audio/net helper state from explicit fields. */
    app->cfg.storage.littlefs.enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_littlefs_enabled));
    (void)slfs_littlefs_set_image(&app->cfg.storage.littlefs, etext(app->entry_littlefs));
    (void)slfs_littlefs_set_partition(&app->cfg.storage.littlefs,
                                      etext(app->entry_littlefs_label),
                                      parse_u32_str(etext(app->entry_littlefs_offset)),
                                      parse_u32_str(etext(app->entry_littlefs_size)));
    (void)slfs_littlefs_set_mount(&app->cfg.storage.littlefs, etext(app->entry_littlefs_mount));
    app->cfg.storage.sd.enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_sd_enabled));
    (void)slfs_sd_set_image(&app->cfg.storage.sd, etext(app->entry_sd_image));
    {
        slfs_sd_mode_t mode = SLFS_SD_MODE_SDMMC;
        gint si = gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo_sd_mode));
        if (si == 1) mode = SLFS_SD_MODE_SDSPI;
        else if (si == 2) mode = SLFS_SD_MODE_NONE;
        (void)slfs_sd_set_mode(&app->cfg.storage.sd, mode);
        (void)slfs_sd_set_hints(&app->cfg.storage.sd, false,
                                parse_int_str(etext(app->entry_sd_slot), -1),
                                parse_int_str(etext(app->entry_sd_cs), -1));
    }

    app->cfg.audio = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_audio));
    app->cfg.audio_net.i2s_enabled = app->cfg.audio;
    (void)audio_net_set_i2s_wav_out(&app->cfg.audio_net, etext(app->entry_audio_out));
    (void)audio_net_set_i2s_wav_in(&app->cfg.audio_net, etext(app->entry_audio_in));
    audio_net_set_i2s_realtime(&app->cfg.audio_net, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_audio_realtime)));
    audio_net_set_i2s_instance(&app->cfg.audio_net, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->spin_audio_instance)));
    (void)audio_net_set_i2s_backend_hint(&app->cfg.audio_net, etext(app->entry_audio_backend));

    app->cfg.audio_net.net_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_net_user));
    (void)audio_net_set_net_mode(&app->cfg.audio_net, etext(app->entry_net_mode));
    if (app->cfg.audio_net.net_enabled && !app->cfg.audio_net.net_mode[0])
        audio_net_set_net_mode(&app->cfg.audio_net, "user");
    (void)launcher_config_set_hostfwd(&app->cfg, etext(app->entry_hostfwd));
    audio_net_set_wifi_enabled(&app->cfg.audio_net, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_wifi)));
    (void)audio_net_set_wifi_ssid(&app->cfg.audio_net, etext(app->entry_wifi_ssid));
    (void)audio_net_set_wifi_password(&app->cfg.audio_net, etext(app->entry_wifi_password));
    {
        audio_net_wifi_auth_t auth = AUDIO_NET_WIFI_AUTH_UNKNOWN;
        switch (gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo_wifi_auth))) {
            case 0: auth = AUDIO_NET_WIFI_AUTH_OPEN; break;
            case 1: auth = AUDIO_NET_WIFI_AUTH_WPA_PSK; break;
            case 2: auth = AUDIO_NET_WIFI_AUTH_WPA2_PSK; break;
            case 3: auth = AUDIO_NET_WIFI_AUTH_WPA3_PSK; break;
            default: auth = AUDIO_NET_WIFI_AUTH_UNKNOWN; break;
        }
        audio_net_set_wifi_auth(&app->cfg.audio_net, auth);
    }
    audio_net_set_ble_enabled(&app->cfg.audio_net, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_ble)));
    (void)audio_net_set_ble_hci(&app->cfg.audio_net, etext(app->entry_ble_hci));
    if (app->entry_emu_caps) snprintf(app->cfg.emu_caps_path, sizeof(app->cfg.emu_caps_path), "%s", etext(app->entry_emu_caps));
    if (app->check_cosim_unix) app->cfg.cosim_use_unix = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_cosim_unix));
    if (app->entry_cosim_unix) snprintf(app->cfg.cosim_unix_path, sizeof(app->cfg.cosim_unix_path), "%s", etext(app->entry_cosim_unix));
    if (app->combo_run_mode) { gint rm = gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo_run_mode)); if (rm >= 0) app->cfg.run_mode = rm; }
    app->run_mode = app->cfg.run_mode;
    app->cfg.net_user = app->cfg.audio_net.net_enabled &&
                        strcmp(app->cfg.audio_net.net_mode, "user") == 0;

    snprintf(app->profile.storage.littlefs_mount_path, sizeof(app->profile.storage.littlefs_mount_path), "%s", etext(app->entry_littlefs_mount));
    app->profile.storage.card_enabled = app->cfg.sd_enabled;
    snprintf(app->profile.storage.card_image_path, sizeof(app->profile.storage.card_image_path), "%s", app->cfg.sd_image_path);
    app->profile.storage.sd_enabled = app->cfg.storage.sd.enabled;
    snprintf(app->profile.storage.sd_image_path, sizeof(app->profile.storage.sd_image_path), "%s", etext(app->entry_sd_image));
    app->profile.network_radio.hostfwd_count = 0;
    for (int i = 0; i < audio_net_get_hostfwd_count(&app->cfg.audio_net) && i < SIM_PROFILE_MAX_HOSTFWD; ++i) {
        snprintf(app->profile.network_radio.hostfwd[i], sizeof(app->profile.network_radio.hostfwd[i]), "%s",
                 audio_net_get_hostfwd(&app->cfg.audio_net, i));
        app->profile.network_radio.hostfwd_count++;
    }
    app->profile.ui_state.scale = (double)app->cfg.scale;
    if (app->combo_chip) {
        int ci = gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo_chip));
        snprintf(app->profile.board_hardware.mcu.chip, sizeof(app->profile.board_hardware.mcu.chip), "%s", chip_id_for_index(ci));
    }
    sync_profile_paths_from_entries(app);
}

static void append_caps_summary(App *app, GString *gs) {
    if (!app || !gs) return;
    const sim_board_t *b = sim_board_catalog_get(&app->boards, launcher_board_id(app->cfg.board));
    g_string_append(gs, "Capabilities (resumo):\n");
    if (b) {
        g_string_append_printf(gs, "• serial_headless: %s\n", b->support.serial_headless[0]?b->support.serial_headless:"conditional");
        g_string_append_printf(gs, "• display_live: %s\n", b->support.display_live[0]?b->support.display_live:"needs-runtime-host");
        g_string_append_printf(gs, "• touch_input: %s\n", b->support.touch_input[0]?b->support.touch_input:"conditional");
        g_string_append_printf(gs, "• audio: %s\n", b->support.audio[0]?b->support.audio:"conditional");
        g_string_append_printf(gs, "• sdmmc_emu: %s\n", card_state(app));
        g_string_append_printf(gs, "• cosim_in_process: %s\n", b->support.cosim_in_process[0]?b->support.cosim_in_process:"supported-now");
        g_string_append_printf(gs, "• cosim_tcp_external: %s\n", b->support.cosim_tcp_external[0]?b->support.cosim_tcp_external:"supported-now");
        if (b->support.parallel_needs_runtime_host) g_string_append(gs, "• parallel_display: needs-runtime-host\n");
    } else g_string_append(gs, _("• board metadata unavailable\n"));
    g_string_append(gs, "• release_sem_ELF: conditional(version/tool)\n");
    g_string_append(gs, "• wifi/ble/hostfwd/realtime_audio: conditional(version/tool)\n");
    g_string_append(gs, _("• SDMMC S3: --sd supported in esp-emu 1.1.0 (Card menu)\n"));
    g_string_append_printf(gs, "• run_mode: %s\n", app->run_mode==1?"backend-only (--no-host/cosim)":"ui+embedded host");
    g_string_append_printf(gs, "• cosim transport(host): %s\n", (app->cfg.cosim_use_unix && app->run_mode==0)?"unix bridge via socat (conditional tool, esp-emu usa TCP interno)":"tcp");
    g_string_append(gs, _("• Merge wizard: esptool merge-bin, dry-run and merge (conditional)\n"));
    g_string_append(gs, _("• Run modes and log filter/export: done\n"));
}

/* SD card state of the configured image: "ok" (enabled + file exists), "ejected" (disabled), "missing" (enabled, no file).
 * esp-emu 1.1.0 supports --sd, so the old catalog text "blocked-by-emu" no longer applies. */
static const char *card_state(const App *app) {
    if (!app->cfg.sd_enabled) return "ejected";
    return path_exists_repo(app, app->cfg.sd_image_path) ? "ok" : "missing";
}

static void update_panels(App *app) {
    if (!app) return;
    bool merged_ok = path_exists_repo(app, app->cfg.merged_image_path);
    const char *elf_show = (app->cfg.use_elf && app->cfg.elf_path[0]) ? app->cfg.elf_path : "(none/--no-elf)";
    bool elf_ok = !(app->cfg.use_elf && app->cfg.elf_path[0]) || path_exists_repo(app, app->cfg.elf_path);
    const sim_board_t *b = sim_board_catalog_get(&app->boards, launcher_board_id(app->cfg.board));
    gchar *txt = g_strdup_printf("profile: %s | merged: %s%s | elf: %s%s | cosim: %s%s | sd: %s (%s)",
                                 app->profile_path[0]?app->profile_path:"(none)",
                                 app->cfg.merged_image_path, merged_ok?"":" (missing)",
                                 elf_show, (!elf_ok && app->cfg.use_elf && app->cfg.elf_path[0])?" (missing)":"",
                                 app->cfg.cosim_board_path[0]?app->cfg.cosim_board_path:"(none)",
                                 (path_exists_repo(app, app->cfg.cosim_board_path)||!app->cfg.cosim_board_path[0])?"":" (missing)",
                                 app->cfg.sd_image_path, card_state(app));
    gtk_label_set_text(GTK_LABEL(app->path_label), txt); g_free(txt);
    char cmd[LAUNCHER_MAX_CMD + 512]; build_effective_command_preview(app, cmd, sizeof(cmd)); set_buf_text(app->cmd_buf, cmd);
    if (app->caps_label) {
        GString *gs = g_string_new(NULL);
        if (b) g_string_append_printf(gs, "badges: serial=%s | display=%s | touch=%s | audio=%s | sd=%s",
                                      b->support.serial_headless[0]?b->support.serial_headless:"?",
                                      b->support.display_live[0]?b->support.display_live:"?",
                                      b->support.touch_input[0]?b->support.touch_input:"?",
                                      b->support.audio[0]?b->support.audio:"?",
                                      card_state(app));
        else g_string_append(gs, _("badges: board metadata unavailable"));
        gtk_label_set_text(GTK_LABEL(app->caps_label), gs->str);
        gtk_widget_set_tooltip_text(app->caps_label, gs->str); g_string_free(gs, TRUE);
    }
    update_summary(app); /* every configuration change passes through here: keep the canvas summary in step */
}

static void render_validation(App *app, bool run_apply) {
    if (!app) return;
    if (run_apply) apply_ui_to_cfg(app);
    fw_artifacts_t arts; fw_artifacts_init(&arts);
    bool release_hint = (app->cfg.firmware == LAUNCHER_FW_KRYONOS_V3) || !app->cfg.use_elf;
    if (app->cfg.firmware == LAUNCHER_FW_KRYONOS_V3) { fw_preset_info_t pi; if (fw_resolve_preset(FW_PRESET_KRYONOS_V3, launcher_board_id(app->cfg.board), &pi)==0) arts = pi.artifacts; }
    else if (app->cfg.firmware == LAUNCHER_FW_LOCAL_ES3C28P_EMU) { fw_preset_info_t pi; if (fw_resolve_preset(FW_PRESET_LOCAL_ES3C28P_EMU, NULL, &pi)==0) arts = pi.artifacts; }
    else if (app->cfg.firmware == LAUNCHER_FW_ASSET_ES3C28P) { fw_preset_info_t pi; if (fw_resolve_preset(FW_PRESET_ASSET_ES3C28P, NULL, &pi)==0) arts = pi.artifacts; }
    else if (app->cfg.merged_image_path[0]) (void)fw_detect(app->cfg.merged_image_path, &arts);
    if (app->cfg.merged_image_path[0]) snprintf(arts.merged, sizeof(arts.merged), "%s", app->cfg.merged_image_path);
    if (app->cfg.use_elf && app->cfg.elf_path[0]) snprintf(arts.elf, sizeof(arts.elf), "%s", app->cfg.elf_path); else { arts.elf[0]='\0'; arts.has_elf=false; }
    arts.has_merged = (arts.merged[0] && fw_file_size(arts.merged)>0);
    arts.size_merged = arts.has_merged ? fw_file_size(arts.merged) : 0;
    arts.size_elf = arts.elf[0] ? fw_file_size(arts.elf) : 0; arts.has_elf = (arts.elf[0] && arts.size_elf>0);
    fw_validation_t fwv; fw_validation_init(&fwv); (void)fw_validate(&arts, release_hint, &fwv);
    validate_report_t rep; validate_report_init(&rep); validate_extra_t extra; memset(&extra,0,sizeof(extra)); extra.release_no_elf = release_hint;
    (void)validate_config(&app->cfg, &extra, app->repo_root, VALIDATE_CHECK_ALL, &rep);
    (void)validate_profile_paths(app->repo_root, app->cfg.merged_image_path, app->cfg.elf_path, app->cfg.use_elf,
                                 app->cfg.cosim_board_path, app->profile.storage.littlefs_image_path[0]?app->profile.storage.littlefs_image_path:app->profile.firmware.artifacts.littlefs,
                                 app->cfg.hw_rotation, app->cfg.scale, app->cfg.lcd_w, app->cfg.lcd_h, app->cfg.instance,
                                 false, extra.release_no_elf, false, VALIDATE_CHECK_RANGES|VALIDATE_CHECK_FILES|VALIDATE_CHECK_CAPS|VALIDATE_CHECK_CONFLICTS, &rep);
    const sim_board_t *b = sim_board_catalog_get(&app->boards, launcher_board_id(app->cfg.board));
    if (b) {
        if (app->cfg.sd_enabled && !path_exists_repo(app, app->cfg.sd_image_path)) validate_report_add(&rep,"CAP_SD_IMAGE_MISSING",VALIDATE_SEV_WARN,"storage.sd",_("SD card image not found (esp-emu 1.1.0 supports --sd)"),_("Card > Build image from folder… or ./start.sh card image"));
        if (b->support.parallel_needs_runtime_host) validate_report_add(&rep,"CAP_PARALLEL_HOST",VALIDATE_SEV_WARN,"board_hardware.display",_("Parallel display needs runtime host/dedicated model (needs-runtime-host)"),_("Prefer serial/headless"));
        if (strcmp(b->support.display_live,"needs-runtime-host")==0 || strcmp(b->support.display_live,"conditional")==0)
            validate_report_add(&rep,"CAP_DISPLAY_RUNTIME",VALIDATE_SEV_WARN,"board_hardware.display",_("Live display depends on cosim/runtime host and compiled pins/resolution"),_("Check board JSON, transport and runtime limits"));
    }
    if (app->profile.network_radio.wifi_enabled) validate_report_add(&rep,"CAP_WIFI",VALIDATE_SEV_INFO,"network_radio.wifi",_("Wi-Fi visible but conditional(version/tool/fw)"),_("Check args/firmware/support"));
    if (app->profile.network_radio.ble_enabled) validate_report_add(&rep,"CAP_BLE",VALIDATE_SEV_INFO,"network_radio.ble",_("BLE visible but conditional(version/tool), usually needs ELF/HCI"),_("Check support/args/ELF"));
    if (app->profile.audio.realtime) validate_report_add(&rep,"CAP_AUDIO_RT",VALIDATE_SEV_INFO,"audio.realtime",_("Realtime audio conditional(version/tool)"),_("Prefer I2S WAV from file"));
    GString *gs = g_string_new(NULL);
    if (fwv.errors[0]) g_string_append_printf(gs, "[firmware:error] %s\n", fwv.errors);
    if (fwv.warnings[0]) g_string_append_printf(gs, "[firmware:warn] %s\n", fwv.warnings);
    if (!fwv.errors[0] && !fwv.warnings[0]) g_string_append(gs, "[firmware] ok\n");
    for (int i=0;i<rep.count;i++) {
        const validate_result_t *r=&rep.items[i];
        g_string_append_printf(gs, "[%s] %s (%s): %s%s%s\n", validate_severity_name(r->severity), r->code, r->field[0]?r->field:"-", r->message, r->suggestion[0]?" | sug: ":"", r->suggestion[0]?r->suggestion:"");
    }
    g_string_append(gs, "\n"); append_caps_summary(app, gs);
    set_buf_text(app->validation_buf, gs->str); g_string_free(gs, TRUE);
}

static gboolean prestart_validate(App *app) {
    if (!app) return FALSE;
    apply_ui_to_cfg(app); render_validation(app, false);
    fw_artifacts_t arts; fw_artifacts_init(&arts);
    bool release_hint = (app->cfg.firmware == LAUNCHER_FW_KRYONOS_V3) || !app->cfg.use_elf;
    if (app->cfg.firmware == LAUNCHER_FW_KRYONOS_V3) { fw_preset_info_t pi; if (fw_resolve_preset(FW_PRESET_KRYONOS_V3, launcher_board_id(app->cfg.board), &pi)==0) arts = pi.artifacts; }
    else if (app->cfg.firmware == LAUNCHER_FW_LOCAL_ES3C28P_EMU) { fw_preset_info_t pi; if (fw_resolve_preset(FW_PRESET_LOCAL_ES3C28P_EMU, NULL, &pi)==0) arts = pi.artifacts; }
    else if (app->cfg.firmware == LAUNCHER_FW_ASSET_ES3C28P) { fw_preset_info_t pi; if (fw_resolve_preset(FW_PRESET_ASSET_ES3C28P, NULL, &pi)==0) arts = pi.artifacts; }
    else if (app->cfg.merged_image_path[0]) (void)fw_detect(app->cfg.merged_image_path, &arts);
    if (app->cfg.merged_image_path[0]) snprintf(arts.merged, sizeof(arts.merged), "%s", app->cfg.merged_image_path);
    if (app->cfg.use_elf && app->cfg.elf_path[0]) snprintf(arts.elf, sizeof(arts.elf), "%s", app->cfg.elf_path);
    arts.has_merged = (arts.merged[0] && fw_file_size(arts.merged)>0);
    fw_validation_t fwv; fw_validation_init(&fwv); (void)fw_validate(&arts, release_hint, &fwv);
    validate_report_t rep; validate_report_init(&rep); validate_extra_t extra; memset(&extra,0,sizeof(extra));
    extra.release_no_elf = release_hint;
    extra.sd_requested = app->cfg.storage.sd.enabled;
    extra.emu_caps = &app->caps;
    (void)validate_config(&app->cfg, &extra, app->repo_root, VALIDATE_CHECK_ALL, &rep);
    bool blocked = !fwv.ok || validate_report_has_errors(&rep);
    if (blocked) {
        append_emu_log(app, _("[launcher] pre-start validation FAILED (blocking errors). Start aborted."));
        /* Start must never fail silently: say why in a dialog, not only in the log. */
        char why[VALIDATE_MAX_TEXT * 2 + 8]; why[0] = '\0';
        if (!fwv.ok && fwv.errors[0]) snprintf(why, sizeof(why), "%s", fwv.errors);
        for (int i = 0; !why[0] && i < rep.count; i++) {
            if (rep.items[i].severity != VALIDATE_SEV_ERROR) continue;
            snprintf(why, sizeof(why), "%s%s%s", rep.items[i].message, rep.items[i].suggestion[0] ? "\n" : "", rep.items[i].suggestion);
        }
        GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(app->window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", _("Cannot start the emulator"));
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), "%s", why[0] ? why : _("Pre-start validation failed. See the Validation panel."));
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
    }
    else append_emu_log(app, _("[launcher] pre-start validation OK (see warnings in the Validation panel)."));
    return blocked ? FALSE : TRUE;
}

static void on_cfg_changed(GtkWidget *w, gpointer data) {
    (void)w; App *app = (App*)data; if (!app || app->ui_syncing) return;
    if (app->combo_run_mode) {
        gint rm = gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo_run_mode));
        if (rm >= 0) { app->run_mode = (int)rm; app->cfg.run_mode = app->run_mode; }
    }
    apply_ui_to_cfg(app);
    gtk_widget_set_sensitive(app->entry_custom, app->cfg.firmware == LAUNCHER_FW_CUSTOM);
    gtk_widget_set_sensitive(app->entry_elf, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_use_elf)));
    update_panels(app);
    refresh_parts_runtime_hints(app);
    render_debug_gpio_panels(app);
    set_dirty(app, TRUE);
    update_status(app);
}

static void on_board_changed(GtkComboBox *cb, gpointer data) {
    (void)cb; App *app = (App*)data; if (!app || app->ui_syncing) return;
    gint bi = gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo_board)); if (bi >= 0 && bi < LAUNCHER_BOARD_COUNT) app->cfg.board = (launcher_board_t)bi;
    launcher_config_resolve_paths(&app->cfg); apply_board_catalog_hints(app, false);
    eset(app->entry_merged, app->cfg.merged_image_path); eset(app->entry_elf, app->cfg.elf_path); eset(app->entry_cosim_board, app->cfg.cosim_board_path);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_use_elf), app->cfg.use_elf);
    on_cfg_changed(NULL, app);
}
static void on_firmware_changed(GtkComboBox *cb, gpointer data) {
    (void)cb; App *app = (App*)data; if (!app || app->ui_syncing) return;
    gint fi = gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo_firmware)); if (fi >= 0 && fi < LAUNCHER_FW_COUNT) app->cfg.firmware = (launcher_firmware_t)fi;
    launcher_config_resolve_paths(&app->cfg); apply_board_catalog_hints(app, false);
    eset(app->entry_merged, app->cfg.merged_image_path); eset(app->entry_elf, app->cfg.elf_path);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_use_elf), app->cfg.use_elf);
    on_cfg_changed(NULL, app);
}

static void render_parts_ui(App *app) {
    if (!app) return;
    GString *gs = g_string_new(NULL);
    g_string_append_printf(gs, "Discovered parts (%d):\n", app->parts_ctx.part_count);
    for (int i = 0; i < app->parts_ctx.part_count; i++) {
        const parts_ui_part_meta_t *p = &app->parts_ctx.parts[i];
        g_string_append_printf(gs, "- %s [%s] wasm=%s sidecar=%s abi=%s proto=%s claims=%d meta=%d\n",
                               p->name[0]?p->name:"(unnamed)",
                               parts_ui_support_state_name(parts_ui_classify_support(&app->parts_ctx, p)),
                               p->wasm_path, p->has_sidecar ? p->sidecar_json_path : "(none)",
                               p->abi_version[0]?p->abi_version:"?", p->proto_version[0]?p->proto_version:"?",
                               p->claim_count, p->meta_count);
        if (p->support_note[0]) g_string_append_printf(gs, "  note: %s\n", p->support_note);
        for (int c = 0; c < p->claim_count; c++) {
            const parts_ui_claim_t *cl = &p->claims[c];
            g_string_append_printf(gs, "  claim: %s bus=%s port=%d pin=%d addr=%d cs=%d ch=%d role=%s %s\n",
                                   parts_ui_claim_kind_name(cl->kind), cl->bus, cl->port, cl->pin, cl->addr, cl->cs_pin, cl->channel, cl->role, cl->note);
        }
    }
    g_string_append_printf(gs, "\nInstances (%d):\n", app->parts_ctx.instance_count);
    for (int i = 0; i < app->parts_ctx.instance_count; i++) {
        const parts_ui_instance_t *in = &app->parts_ctx.instances[i];
        g_string_append_printf(gs, "- id=%s part=%s enabled=%d attached=%d state=%s needs_sync=%d status='%s' err='%s'\n",
                               in->id, in->part_name, in->enabled, in->attached,
                               parts_ui_support_state_name(in->state), in->needs_sync, in->status_text, in->last_error);
        for (int c = 0; c < in->wiring_count; c++) {
            const parts_ui_claim_t *cl = &in->wiring[c];
            g_string_append_printf(gs, "  wiring: %s bus=%s port=%d pin=%d addr=%d cs=%d ch=%d role=%s\n",
                                   parts_ui_claim_kind_name(cl->kind), cl->bus, cl->port, cl->pin, cl->addr, cl->cs_pin, cl->channel, cl->role);
        }
    }
    g_string_append(gs, "\nHost/capability hints: cosim_tcp=");
    g_string_append(gs, app->parts_ctx.host_cosim_tcp_available ? "yes" : "no");
    g_string_append(gs, " cosim_board=");
    g_string_append(gs, app->parts_ctx.host_cosim_board_available ? "yes" : "no");
    g_string_append(gs, " esp_emu_wasm_parts=");
    g_string_append(gs, app->parts_ctx.esp_emu_has_wasm_parts ? "yes(conditional)" : "no");
    g_string_append(gs, " control_part_cmds=");
    g_string_append(gs, app->parts_ctx.esp_emu_has_control_part_cmds ? "yes" : "no");
    g_string_append_printf(gs, " ver=%s\n", app->parts_ctx.esp_emu_version[0]?app->parts_ctx.esp_emu_version:"?");
    set_buf_text(app->parts_list_buf, gs->str);
    g_string_free(gs, TRUE);

    gs = g_string_new(NULL);
    g_string_append_printf(gs, "Report: items=%d errors=%d warnings=%d\n", app->parts_report.count, app->parts_report.errors, app->parts_report.warnings);
    for (int i = 0; i < app->parts_report.count; i++) {
        const parts_ui_diag_t *d = &app->parts_report.items[i];
        const char *sev = d->severity == PARTS_UI_DIAG_ERROR ? "error" : (d->severity == PARTS_UI_DIAG_WARN ? "warn" : "info");
        g_string_append_printf(gs, "[%s] %s (%s): %s\n", sev, d->code, d->subject, d->message);
    }
    if (app->parts_report.count == 0) g_string_append(gs, _("(no diagnostics)\n"));
    set_buf_text(app->parts_report_buf, gs->str);
    g_string_free(gs, TRUE);
}

static void refresh_telemetry(App *app) {
    if (!app) return;
    gboolean emu_running = app->emu_proc && sim_proc_is_running(app->emu_proc);
    telem_set_run_state(&app->telem, emu_running ? TELEM_RUN_RUNNING : TELEM_RUN_STOPPED);
    if (emu_running) {
        if (app->emu_start_us <= 0) app->emu_start_us = g_get_monotonic_time();
        telem_set_uptime_ms(&app->telem, (uint64_t)((g_get_monotonic_time() - app->emu_start_us) / 1000));
    } else {
        app->emu_start_us = 0;
        telem_set_uptime_ms(&app->telem, 0);
    }
    telem_set_target_board(&app->telem, launcher_board_id(app->cfg.board), launcher_board_id(app->cfg.board));
    telem_set_target_firmware(&app->telem, launcher_firmware_name(app->cfg.firmware), app->cfg.merged_image_path);
    telem_set_target_profile(&app->telem, app->profile.metadata.name[0]?app->profile.metadata.name:app->profile_path, app->profile_path);

    telem_set_capabilities_defaults(&app->telem);
    /* Hooks/snapshots not wired to a live runtime host yet in integrated launcher flow. */
    telem_set_capability(&app->telem.caps.gpio, TELEM_SUPPORT_NEEDS_RUNTIME_HOST, false, _("gpio snapshot hook not connected to runtime"), NULL);
    telem_set_capability(&app->telem.caps.registers, TELEM_SUPPORT_NEEDS_RUNTIME_HOST, false, _("regs snapshot hook not connected to runtime"), NULL);
    telem_set_capability(&app->telem.caps.memory, TELEM_SUPPORT_NEEDS_RUNTIME_HOST, false, _("mem snapshot hook not connected to runtime"), NULL);
    telem_set_capability(&app->telem.caps.perf, TELEM_SUPPORT_NEEDS_RUNTIME_HOST, false, _("perf hook not connected to runtime"), NULL);
    telem_set_capability(&app->telem.caps.cycles, TELEM_SUPPORT_NEEDS_RUNTIME_HOST, false, _("cycles unavailable without runtime host"), NULL);
    telem_set_capability(&app->telem.caps.mips, TELEM_SUPPORT_NEEDS_RUNTIME_HOST, false, _("mips unavailable without runtime host"), NULL);
    {
        bool trace_on = app->check_trace_enabled && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_trace_enabled));
        bool gdb_on = app->check_gdb_enabled && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_gdb_enabled));
        telem_set_capability(&app->telem.caps.trace, TELEM_SUPPORT_CONDITIONAL, trace_on, trace_on?_("trace configured (--trace)"):_("trace not active"), _("esp-emu 1.1.0 + ELF recommended"));
        telem_set_capability(&app->telem.caps.gdb, TELEM_SUPPORT_CONDITIONAL, gdb_on, gdb_on?_("GDB stub configured (--gdb)"):_("GDB not active"), "esp-emu 1.1.0 + gdb/ELF");
        if (trace_on && app->entry_trace_path) telem_set_trace_file(&app->telem, gtk_entry_get_text(GTK_ENTRY(app->entry_trace_path)), trace_on, false, 0);
        if (gdb_on) { sim_ports_t ports; app_ports(app, &ports); telem_set_gdb_config(&app->telem, true, ports.gdb, app->check_gdb_halt&&gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_gdb_halt)), NULL); }
    }
    telem_clear_host_fps(&app->telem);
    telem_clear_counters(&app->telem);
    telem_set_counter(&app->telem, "host_attached", (uint64_t)(app->host_attached ? 1 : 0));
    telem_set_counter(&app->telem, "cosim_net", (uint64_t)(app->net_started ? 1 : 0));

    char buf[TELEM_MAX_TEXT];
    if (telem_format_panel(&app->telem, buf, sizeof(buf)) > 0) set_buf_text(app->telemetry_buf, buf);
    else set_buf_text(app->telemetry_buf, _("telemetry: (unavailable)"));
}

static gboolean telemetry_tick_cb(gpointer data) {
    App *app = (App*)data;
    if (!app) return G_SOURCE_REMOVE;
    update_status(app); /* also refreshes telemetry and the one-line toolbar status */
    return G_SOURCE_CONTINUE;
}

/* One-line description of the SD card for the toolbar: path (basename), size and enabled state. */
static void card_describe(const App *app, char *out, size_t outsz) {
    const char *st = card_state(app);
    char rp[PATH_MAX]; struct stat sb;
    const char *base = strrchr(app->cfg.sd_image_path, '/');
    base = base ? base + 1 : app->cfg.sd_image_path;
    resolve_repo_path(app, app->cfg.sd_image_path, rp, sizeof(rp));
    if (!app->cfg.sd_enabled) snprintf(out, outsz, _("sd: ejected (%.80s)"), base);
    else if (strcmp(st, "ok") == 0 && stat(rp, &sb) == 0) snprintf(out, outsz, "sd: %.80s (%lld MB)", base, (long long)(sb.st_size / (1024 * 1024)));
    else snprintf(out, outsz, _("sd: %.80s (missing)"), base);
}

/* ---- Canvas summary card: shown over the canvas while no host is attached; refreshed from update_panels() ---- */
#define SUM_ROWS 12
static const char *k_sum_keys[SUM_ROWS] = { N_("Board"), N_("Chip"), N_("Display"), N_("Firmware"), N_("ELF"), N_("Cosim board"), N_("SD card"), N_("Network/Wi-Fi/BLE"), N_("Audio"), N_("Console/UART"), N_("esp-emu"), N_("Command") };
#define SUM_OK  "<span foreground=\"#2dd4a6\">✓</span>"
#define SUM_BAD "<span foreground=\"#e5484d\">✗</span>"
#define SUM_DIM_OPEN "<span foreground=\"#7d8a99\">"
#define SUM_DIM_CLOSE "</span>"
static void sum_set(App *app, int row, const char *fmt, ...) {
    if (!app->lbl_sum_val[row]) return;
    va_list ap; va_start(ap, fmt); gchar *m = g_strdup_vprintf(fmt, ap); va_end(ap);
    gtk_label_set_markup(GTK_LABEL(app->lbl_sum_val[row]), m); gtk_widget_set_tooltip_markup(app->lbl_sum_val[row], m); g_free(m);
}
static gchar *sum_esc(const char *s) { return g_markup_escape_text(s && s[0] ? s : _("(none)"), -1); }
static gchar *sum_mark(bool on) {
    return on ? g_strdup_printf("<span foreground=\"#2dd4a6\">%s</span>", _("on"))
              : g_strdup_printf(SUM_DIM_OPEN "%s" SUM_DIM_CLOSE, _("off"));
}
/* "1.1.0" out of ".../esp-emu-1.1.0-x86_64-.../esp-emu" (the probe is not run at start-up) */
static void emu_version_from_path(const char *path, char *out, size_t outsz) {
    out[0] = '\0';
    const char *p = path ? strstr(path, "esp-emu-") : NULL;
    if (!p) return;
    p += 8; size_t n = 0;
    while (p[n] && (g_ascii_isdigit(p[n]) || p[n] == '.') && n + 1 < outsz) { out[n] = p[n]; n++; }
    out[n] = '\0';
}
static void update_summary(App *app) {
    if (!app || !app->w_summary || !app->lbl_sum_val[0]) return;
    const sim_board_t *b = sim_board_catalog_get(&app->boards, launcher_board_id(app->cfg.board));
    gchar *e;
    e = sum_esc(launcher_board_id(app->cfg.board)); sum_set(app, 0, "%s", e); g_free(e);
    e = sum_esc(app->profile.board_hardware.mcu.chip[0] ? app->profile.board_hardware.mcu.chip : "esp32s3"); sum_set(app, 1, "%s", e); g_free(e);
    {
        int rw = app->cfg.lcd_w > 0 ? app->cfg.lcd_w : (b ? b->display.width : 0), rh = app->cfg.lcd_h > 0 ? app->cfg.lcd_h : (b ? b->display.height : 0);
        sum_set(app, 2, _("%dx%d  ·  rotation %d  ·  scale %d"), rw, rh, app->cfg.hw_rotation, app->cfg.scale);
    }
    bool fw_ok = app->cfg.merged_image_path[0] && path_exists_repo(app, app->cfg.merged_image_path);
    e = sum_esc(app->cfg.merged_image_path); sum_set(app, 3, "%s  %s", e, fw_ok ? SUM_OK : SUM_BAD); g_free(e);
    if (app->cfg.use_elf && app->cfg.elf_path[0]) { e = sum_esc(app->cfg.elf_path); sum_set(app, 4, "%s  %s", e, path_exists_repo(app, app->cfg.elf_path) ? SUM_OK : SUM_BAD); g_free(e); }
    else sum_set(app, 4, "%s%s%s", SUM_DIM_OPEN, _("off (--no-elf)"), SUM_DIM_CLOSE);
    if (app->cfg.cosim_board_path[0]) { e = sum_esc(app->cfg.cosim_board_path); sum_set(app, 5, "%s  %s", e, path_exists_repo(app, app->cfg.cosim_board_path) ? SUM_OK : SUM_BAD); g_free(e); }
    else sum_set(app, 5, "%s%s%s", SUM_DIM_OPEN, _("(none)"), SUM_DIM_CLOSE);
    if (!app->cfg.sd_enabled) { e = sum_esc(app->cfg.sd_image_path); sum_set(app, 6, "%s%s%s  ·  %s", SUM_DIM_OPEN, _("off"), SUM_DIM_CLOSE, e); g_free(e); }
    else {
        char rp[PATH_MAX]; struct stat sb; resolve_repo_path(app, app->cfg.sd_image_path, rp, sizeof(rp));
        e = sum_esc(app->cfg.sd_image_path);
        if (stat(rp, &sb) == 0) sum_set(app, 6, "%s  ·  %lld MB  %s", e, (long long)(sb.st_size / (1024 * 1024)), SUM_OK);
        else sum_set(app, 6, "%s  %s", e, SUM_BAD);
        g_free(e);
    }
    {
        const audio_net_config_t *an = &app->cfg.audio_net;
        gchar *tn = sum_mark(an->net_enabled), *tw = sum_mark(an->wifi_enabled), *tb = sum_mark(an->ble_enabled);
        gchar *en = sum_esc(an->net_mode), *ew = sum_esc(an->wifi_ssid);
        gchar *dn = an->net_enabled ? g_strdup_printf(SUM_DIM_OPEN "  %s" SUM_DIM_CLOSE, en) : g_strdup("");
        gchar *dw = an->wifi_enabled ? g_strdup_printf(SUM_DIM_OPEN "  %s · %s" SUM_DIM_CLOSE, ew, audio_net_wifi_auth_str(an->wifi_auth)) : g_strdup("");
        sum_set(app, 7, "%s %s%s  ·  Wi-Fi %s%s  ·  BLE %s", _("Network"), tn, dn, tw, dw, tb);
        g_free(tn); g_free(tw); g_free(tb); g_free(en); g_free(ew); g_free(dn); g_free(dw);
    }
    { gchar *t = sum_mark(app->cfg.audio); sum_set(app, 8, "%s", t); g_free(t); }
    e = sum_esc(app->profile.emu_runtime.console_mode[0] ? app->profile.emu_runtime.console_mode : "tcp");
    { sim_ports_t ports; app_ports(app, &ports); char ub[32]; snprintf(ub, sizeof(ub), "127.0.0.1:%d", ports.uart);
      gchar *u = sum_esc(ub); sum_set(app, 9, "%s  ·  %s", e, u); g_free(u); } g_free(e);
    {
        const char *ep = app->profile.emu_runtime.esp_emu_path[0] ? app->profile.emu_runtime.esp_emu_path : EMU_DEFAULT_PATH;
        char ver[32]; emu_version_from_path(ep, ver, sizeof(ver));
        e = sum_esc(ep); sum_set(app, 10, "%s%s%s  %s", e, ver[0] ? "  ·  v" : "", ver, path_exists_repo(app, ep) ? SUM_OK : SUM_BAD); g_free(e);
    }
    {
        char cmd[LAUNCHER_MAX_CMD + 512]; build_effective_command_preview(app, cmd, sizeof(cmd));
        char *nl = strchr(cmd, '\n'); if (nl) *nl = '\0';
        e = sum_esc(cmd); sum_set(app, 11, "%s", e); g_free(e);
    }
}
/* Summary visible unless the embedded host is attached (then the SDL view owns the canvas). */
static void update_summary_visibility(App *app) {
    if (!app->w_summary) return;
    gboolean emu_running = app->emu_proc && sim_proc_is_running(app->emu_proc);
    gboolean show = !app->host_attached;
    gtk_label_set_text(GTK_LABEL(app->lbl_sum_title),
        !emu_running ? _("Ready to start") : (app->run_mode == 1 ? _("Backend running (no canvas)") : _("Starting…")));
    gtk_widget_set_visible(app->w_summary, show);
    gtk_widget_set_visible(app->w_summary_hint, show && !emu_running);
    if (show) gtk_widget_queue_draw(app->canvas);
}

static void update_status(App *app) {
    if (!app) return;
    gboolean emu_running = app->emu_proc && sim_proc_is_running(app->emu_proc);
    const char *es = app->emu_proc ? sim_proc_get_status(app->emu_proc) : "Stopped";
    const char *hs = "idle";
    if (app->run_mode == 1) hs = emu_running ? "backend-only (--no-host)" : "backend-only";
    else if (app->host_attached && app->host_connected) hs = "attached(canvas)+cosim";
    else if (app->host_attached) hs = _("attached(canvas), cosim connecting");
    else if (app->host) hs = _("waiting for canvas");
    else hs = app->run_mode==0?"idle(canvas)":"idle";
    char s[320]; snprintf(s, sizeof(s), "emu: %s | host: %s | mode=%s%s", es ? es : "?", hs, app->run_mode==1?"backend-only":"ui+embedded", app->dirty ? " | *unsaved" : "");
    if (app->canvas_label) {
        char cl[256]; snprintf(cl,sizeof(cl),"Canvas: %s | %s%s",
            app->run_mode==1?_("backend-only (--no-host, no embed)"):_("ui+embedded host (sim_host/SDL in GTK X11)"),
            app->host_attached?(app->host_connected?"attached+cosim":_("attached, cosim connecting")):(app->run_mode==0?(emu_running?_("waiting for canvas"):_("stopped")):_("external/conditional")),
            (app->run_mode==0 && app->cfg.cosim_use_unix)?" | unix-bridge":"");
        gtk_label_set_text(GTK_LABEL(app->canvas_label), cl);
    }
    /* Single-line toolbar status (the long text above goes to its tooltip). */
    {
        char line[400], pidbuf[32] = "", cosim[32], res[32] = "", sdd[160];
        const char *state = emu_running ? _("Running") : ((es && strncmp(es, "Stopping", 8) == 0) ? _("Stopping") : _("Stopped"));
        if (emu_running) snprintf(pidbuf, sizeof(pidbuf), " · pid %d", (int)sim_proc_get_pid(app->emu_proc));
        if (app->run_mode == 1) snprintf(cosim, sizeof(cosim), "backend-only");
        else if (app->host_attached && app->host_connected) snprintf(cosim, sizeof(cosim), "cosim OK");
        else if (emu_running) snprintf(cosim, sizeof(cosim), _("cosim connecting…"));
        else snprintf(cosim, sizeof(cosim), "cosim -");
        {
            const sim_board_t *bd = sim_board_catalog_get(&app->boards, launcher_board_id(app->cfg.board));
            int rw = app->cfg.lcd_w > 0 ? app->cfg.lcd_w : (bd ? bd->display.width : 0);
            int rh = app->cfg.lcd_h > 0 ? app->cfg.lcd_h : (bd ? bd->display.height : 0);
            if (rw > 0 && rh > 0) snprintf(res, sizeof(res), " · %dx%d", rw, rh);
        }
        card_describe(app, sdd, sizeof(sdd));
        {
            /* status bar: profile, dirty flag, run state, esp-emu version, SD state */
            const char *ep = app->profile.emu_runtime.esp_emu_path[0] ? app->profile.emu_runtime.esp_emu_path : EMU_DEFAULT_PATH;
            char ver[32]; emu_version_from_path(ep, ver, sizeof(ver));
            const char *pn = app->profile.metadata.name[0] ? app->profile.metadata.name : (app->profile_path[0] ? app->profile_path : _("(no profile)"));
            snprintf(line, sizeof(line), _("profile: %s%s · %s%s · %s%s · esp-emu %s%s · %s"), pn, app->dirty ? " *" : "", state, pidbuf, cosim, res, ver[0] ? "v" : "", ver[0] ? ver : "?", sdd);
            gtk_label_set_text(GTK_LABEL(app->lbl_profile), pn);
            if (app->lbl_profile) gtk_widget_set_tooltip_text(app->lbl_profile, app->profile_path);
            const sim_board_t *bd2 = sim_board_catalog_get(&app->boards, launcher_board_id(app->cfg.board));
            char bl[160]; snprintf(bl, sizeof(bl), "%s%s", launcher_board_id(app->cfg.board), res);
            (void)bd2; gtk_label_set_text(GTK_LABEL(app->lbl_board), bl);
            /* badge: STOPPED red / STARTING… orange (emulator up, host not yet attached+connected) / RUNNING green */
            const char *bt = _("STOPPED"), *bc = "badge-stopped";
            if (emu_running) {
                gboolean up = (app->run_mode == 1) || (app->host_attached && app->host_connected && app->emu_start_us > 0 && (g_get_monotonic_time() - app->emu_start_us) > 2000000); /* STARTING until attached+connected and 2 s of uptime */
                bt = up ? _("RUNNING") : _("STARTING…"); bc = up ? "badge-running" : "badge-starting";
            } else if (es && strncmp(es, "Stopping", 8) == 0) { bt = _("STOPPING…"); bc = "badge-starting"; }
            GtkStyleContext *sc = gtk_widget_get_style_context(app->lbl_badge);
            gtk_style_context_remove_class(sc, "badge-stopped"); gtk_style_context_remove_class(sc, "badge-running"); gtk_style_context_remove_class(sc, "badge-starting");
            gtk_style_context_add_class(sc, bc);
            gtk_label_set_text(GTK_LABEL(app->lbl_badge), bt);
            /* run timer */
            char tm[16]; long sec = (emu_running && app->emu_start_us > 0) ? (long)((g_get_monotonic_time() - app->emu_start_us) / 1000000) : 0;
            snprintf(tm, sizeof(tm), "%02ld:%02ld:%02ld", sec / 3600, (sec / 60) % 60, sec % 60);
            gtk_label_set_text(GTK_LABEL(app->lbl_timer), tm);
        }
        gtk_label_set_text(GTK_LABEL(app->status_label), line);
        gtk_widget_set_tooltip_text(app->status_label, s);
    }
    gtk_widget_set_sensitive(app->btn_start, !emu_running);
    gtk_widget_set_sensitive(app->btn_stop, emu_running || app->host_attached);
    gtk_widget_set_sensitive(app->btn_restart, TRUE);
    if (app->mi_start) gtk_widget_set_sensitive(app->mi_start, !emu_running);
    if (app->mi_stop) gtk_widget_set_sensitive(app->mi_stop, emu_running || app->host_attached);
    if (app->mi_eject) gtk_widget_set_sensitive(app->mi_eject, app->cfg.sd_enabled);
    update_summary_visibility(app);
    refresh_telemetry(app);
}

static void on_emu_log(const char *line, gpointer user_data) { append_emu_log((App*)user_data, line); }
static void write_hw_state(App *app) {
    char p1[PATH_MAX], p2[PATH_MAX];
    snprintf(p1, sizeof(p1), "%s/%s", app->repo_root, HW_ROT_STATE_PATH);
    snprintf(p2, sizeof(p2), "%s/%s", app->repo_root, HW_BOARD_STATE_PATH);
    FILE *f = fopen(p1, "w"); if (f) { fprintf(f, "%d\n", app->cfg.hw_rotation); fclose(f); }
    f = fopen(p2, "w"); if (f) { fprintf(f, "%s\n", launcher_board_id(app->cfg.board)); fclose(f); }
}
static int build_emu_backend_command(App *app, char *out, size_t outsz) {
    if (!app || !out || !outsz) return -1;
    GString *notes = g_string_new(NULL);
    int rc = build_emu_command(app, out, outsz, notes);
    if (notes->len) {
        char *nl = notes->str;
        while (nl && *nl) {
            char *e = strchr(nl, '\n');
            if (e) *e = '\0';
            if (*nl) appendf_log(app, FALSE, "[launcher] %s", nl);
            if (!e) break;
            nl = e + 1;
        }
    }
    g_string_free(notes, TRUE);
    return rc;
}

static void stop_host(App *app) {
    if (app->render_tick_id) { g_source_remove(app->render_tick_id); app->render_tick_id = 0; }
    if (app->host) {
        if (app->net_started) { sim_host_stop_net(app->host); app->net_started = FALSE; }
        sim_host_video_shutdown(app->host); sim_host_destroy(app->host); app->host = NULL;
    }
    if (app->bridge_proc && sim_proc_is_running(app->bridge_proc)) {
        sim_proc_stop(app->bridge_proc, 0);
    }
    if (app->cosim_unix_actual[0]) { unlink(app->cosim_unix_actual); app->cosim_unix_actual[0] = '\0'; }
    app->host_attached = FALSE; app->host_connected = FALSE;
}
/* SDL only learns that the embedded X window was moved/resized when its event queue is pumped (sim_host never does it in embedded mode);
 * without this the SDL view keeps the old size after a panel is shown/hidden or a divider is dragged. */
static void sdl_sync_events(void) { SDL_PumpEvents(); SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT); }
/* Same for the cached window size (SDL_GetWindowSize feeds the LCD placement in sim_host_render): tell SDL the real size of the canvas window. */
static void sdl_set_canvas_size(int w, int h) {
    for (Uint32 id = 1; id <= 4; id++) { SDL_Window *sw = SDL_GetWindowFromID(id); if (sw) { int cw = 0, ch = 0; SDL_GetWindowSize(sw, &cw, &ch); if (cw != w || ch != h) SDL_SetWindowSize(sw, w, h); break; } }
}
static gboolean render_tick(gpointer data) { App *app = (App*)data; if (app->host && app->host_attached) { sdl_sync_events(); { GtkAllocation al; gtk_widget_get_allocation(app->canvas, &al); sdl_set_canvas_size(al.width, al.height); } sim_host_update(app->host); sim_host_render(app->host); } return G_SOURCE_CONTINUE; }
static gboolean try_attach_host(gpointer data) {
    App *app = (App*)data; if (!app->host || app->host_attached) return G_SOURCE_REMOVE;
    GdkWindow *gw = gtk_widget_get_window(app->canvas);
    if (!gw || !GDK_IS_X11_WINDOW(gw)) { append_host_log(app, "[host] canvas not ready/X11; retry"); return G_SOURCE_CONTINUE; }
    if (!app->host_connected) { if (!sim_host_connect(app->host)) return G_SOURCE_CONTINUE; app->host_connected = TRUE; } /* esp-emu may not listen yet: retry */
    guintptr xid = GDK_WINDOW_XID(gw); GtkAllocation alloc; gtk_widget_get_allocation(app->canvas, &alloc);
    int w = alloc.width > 1 ? alloc.width : 480, h = alloc.height > 1 ? alloc.height : 640;
    if (!sim_host_video_attach_native(app->host, (uintptr_t)xid, w, h)) { append_host_log(app, "[host] failed to attach SDL to canvas"); return G_SOURCE_REMOVE; }
    if (!sim_host_start_net(app->host)) append_host_log(app, "[host] failed to start cosim net thread"); else app->net_started = TRUE;
    app->host_attached = TRUE; if (!app->render_tick_id) app->render_tick_id = g_timeout_add(16, render_tick, app);
    append_host_log(app, "[host] attached to canvas, cosim connecting..."); update_status(app); refresh_telemetry(app); return G_SOURCE_REMOVE;
}
static gboolean do_start(App *app);
static void on_emu_exit(SimProc *proc, gint wait_status, gpointer user_data) {
    (void)proc; (void)wait_status; App *app = (App*)user_data; append_emu_log(app, "[launcher] esp-emu stopped");
    stop_host(app); app->emu_start_us = 0; update_status(app);
    if (app->restart_pending) { app->restart_pending = FALSE; append_emu_log(app, "[launcher] restarting..."); app->emu_start_us = g_get_monotonic_time(); do_start(app); }
}
static void stop_all(App *app) {
    app->restart_pending = FALSE; stop_host(app);
    if (app->emu_proc && sim_proc_is_running(app->emu_proc)) { append_emu_log(app, "[launcher] stopping esp-emu..."); sim_proc_stop(app->emu_proc, 0); }
    update_status(app);
}
static gboolean do_start(App *app) {
    if (!app) return FALSE;
    if (app->emu_proc && sim_proc_is_running(app->emu_proc)) { append_launcher_log(app, "[launcher] already running"); return FALSE; }
    apply_ui_to_cfg(app); update_panels(app);
    if (!prestart_validate(app)) { update_status(app); return FALSE; }
    write_hw_state(app); save_conf_only(app); /* the profile file is written by Save only; unsaved edits stay dirty */
    char emu_cmd[LAUNCHER_MAX_CMD + 256];
    if (build_emu_backend_command(app, emu_cmd, sizeof(emu_cmd)) != 0) { append_launcher_log(app, "[launcher] failed to build emu command"); update_status(app); return FALSE; }
    GError *err = NULL;
    if (app->run_mode == 1)
        append_launcher_log(app, "[launcher] start esp-emu backend (backend-only, --no-host/cosim; cosim-tcp ready)");
    else
        append_launcher_log(app, "[launcher] start esp-emu backend (ui + embedded canvas, cosim-tcp)");
    {
        char *logged = redact_wifi_password(emu_cmd);
        append_launcher_log(app, logged);
        g_free(logged);
    }
    telem_set_log_paths(&app->telem, "launcher-ui", "embedded-sim_host");
    sim_ports_t ports; app_ports(app, &ports);
    char cosim_ep[64]; snprintf(cosim_ep, sizeof(cosim_ep), "127.0.0.1:%d", ports.cosim);
    telem_set_cosim_info(&app->telem, (app->run_mode==0 && app->cfg.cosim_use_unix)?"unix-bridge":"tcp", cosim_ep, NULL, app->cfg.cosim_board_path, ports.cosim > 0);
    if (app->check_trace_enabled && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_trace_enabled)) && app->entry_trace_path)
        telem_set_trace_file(&app->telem, gtk_entry_get_text(GTK_ENTRY(app->entry_trace_path)), true, false, 0);
    if (app->check_gdb_enabled && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_gdb_enabled)))
        telem_set_gdb_config(&app->telem, true, ports.gdb,
                             app->check_gdb_halt && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_gdb_halt)), NULL);
    telem_note_update(&app->telem, (uint64_t)(g_get_monotonic_time() / 1000));
    if (!sim_proc_start_shell(app->emu_proc, emu_cmd, app->repo_root, on_emu_log, app, on_emu_exit, app, &err)) {
        append_launcher_log(app, "[launcher] failed to start esp-emu"); if (err) { append_launcher_log(app, err->message); g_clear_error(&err); }
        update_status(app); return FALSE;
    }
    stop_host(app);
    if (app->run_mode == 1) {
        append_host_log(app, "[host] skipped (backend-only mode). External cosim host/client may connect via TCP or Unix bridge.");
    } else {
        SimHostConfig hcfg; sim_host_config_init(&hcfg);
        static char portbuf[16], rotbuf[PATH_MAX], brdbuf[PATH_MAX], unixbuf[PATH_MAX];
        snprintf(portbuf, sizeof(portbuf), "%d", ports.cosim); hcfg.port = portbuf;
        snprintf(rotbuf, sizeof(rotbuf), "%s/%s", app->repo_root, HW_ROT_STATE_PATH); hcfg.rot_state_file = rotbuf;
        snprintf(brdbuf, sizeof(brdbuf), "%s/%s", app->repo_root, HW_BOARD_STATE_PATH); hcfg.board_state_file = brdbuf;
        hcfg.hw_rot = app->cfg.hw_rotation & 3; hcfg.scale = app->cfg.scale; hcfg.show_touch_debug = true;
        if (app->cfg.cosim_use_unix) {
            const char *up = app->cfg.cosim_unix_path[0] ? app->cfg.cosim_unix_path : "/tmp/esp-sim-cosim.sock";
            snprintf(app->cosim_unix_actual, sizeof(app->cosim_unix_actual), "%s", up);
            unlink(app->cosim_unix_actual);
            char bcmd[1024];
            snprintf(bcmd, sizeof(bcmd), "socat UNIX-LISTEN:%s,fork,reuseaddr,unlink-early TCP:127.0.0.1:%d",
                     app->cosim_unix_actual, ports.cosim);
            GError *berr = NULL;
            if (app->bridge_proc && sim_proc_start_shell(app->bridge_proc, bcmd, app->repo_root, NULL, NULL, NULL, NULL, &berr)) {
                append_host_log(app, "[host] cosim Unix bridge started (socat UNIX->TCP, conditional tool)");
                snprintf(unixbuf, sizeof(unixbuf), "%s", app->cosim_unix_actual);
                hcfg.unix_path = unixbuf;
            } else {
                append_host_log(app, "[host] socat bridge failed/unavailable; falling back to TCP cosim");
                if (berr) { append_host_log(app, berr->message); g_clear_error(&berr); }
                app->cosim_unix_actual[0] = '\0';
            }
        }
        app->host = sim_host_create(&hcfg);
        if (!app->host) { append_host_log(app, "[host] create failed"); update_status(app); return FALSE; }
        sim_host_set_debug(app->host, false); append_host_log(app, "[host] waiting for canvas attach/connect...");
        g_timeout_add(150, try_attach_host, app);
    }
    gchar *line = g_strdup_printf("[launcher] board=%s firmware=%s rot=%d mode=%s profile=%s", launcher_board_id(app->cfg.board), launcher_firmware_name(app->cfg.firmware), app->cfg.hw_rotation, app->run_mode==1?"backend-only":"ui+host", app->profile_path);
    append_launcher_log(app, line); g_free(line); update_status(app); return TRUE;
}

static void refresh_after_profile_switch(App *app, const char *msg) {
    if (!app) return;
    launcher_config_resolve_paths(&app->cfg);
    app->cfg.run_mode = app->cfg.run_mode ? 1 : 0; app->run_mode = app->cfg.run_mode;
    apply_board_catalog_hints(app, true);
    sync_ui_from_state(app);
    update_panels(app);
    render_validation(app, false);
    persist_active_profile_conf(app);
    set_dirty(app, FALSE);
    if (msg && msg[0]) append_launcher_log(app, msg);
    update_status(app);
}

static gboolean load_profile_from_path(App *app, const char *path, gboolean set_active, gboolean persist_active) {
    if (!app || !path || !path[0]) return FALSE;
    sim_profile_diags_t diags; sim_profile_diags_init(&diags);
    char abs[PATH_MAX]; resolve_repo_path(app, path, abs, sizeof(abs));
    sim_profile_t loaded; sim_profile_init(&loaded);
    if (sim_profile_load_json(&loaded, abs, &diags) != 0) {
        log_profile_diags(app, &diags, "load");
        appendf_log(app, FALSE, _("[profile] error loading '%s'"), path);
        sim_profile_free(&loaded);
        sim_profile_diags_free(&diags);
        return FALSE;
    }
    sim_profile_free(&app->profile);
    app->profile = loaded;
    app->profile_loaded = TRUE;
    if (set_active) snprintf(app->profile_path, sizeof(app->profile_path), "%s", path);
    sync_cfg_from_profile(app);
    log_profile_diags(app, &diags, "load");
    refresh_after_profile_switch(app, NULL);
    appendf_log(app, FALSE, _("[profile] loaded: %s%s"), path, set_active ? _(" (active)") : "");
    if (persist_active) persist_active_profile_conf(app);
    sim_profile_diags_free(&diags);
    return TRUE;
}

static void profile_new_defaults(App *app) {
    if (!app) return;
    apply_ui_to_cfg(app);
    sim_profile_free(&app->profile);
    sim_profile_defaults(&app->profile);
    app->profile_loaded = FALSE;
    snprintf(app->profile_path, sizeof(app->profile_path), "%s", "simulator/launcher/profiles/new-profile.json");
    snprintf(app->profile.metadata.id, sizeof(app->profile.metadata.id), "new-profile");
    snprintf(app->profile.metadata.name, sizeof(app->profile.metadata.name), _("New profile"));
    sync_cfg_from_profile(app);
    refresh_after_profile_switch(app, _("[profile] new profile created (defaults). Adjust, then use Save/Save as."));
}

static void profile_identity_from_path(App *app, const char *json_path);
static void profile_duplicate_active(App *app, const char *dest_path) {
    if (!app || !dest_path || !dest_path[0]) return;
    apply_ui_to_cfg(app);
    /* keep current state, save as new file and make it active: the copy gets its own id and name */
    char old_id[SIM_PROFILE_MAX_ID], old_name[SIM_PROFILE_MAX_NAME];
    snprintf(old_id, sizeof(old_id), "%s", app->profile.metadata.id); snprintf(old_name, sizeof(old_name), "%s", app->profile.metadata.name);
    char *json_path = profile_json_name(dest_path); /* same ".json" rule as Save as */
    profile_identity_from_path(app, json_path);
    if (save_profile_to_path(app, json_path, true, false)) {
        snprintf(app->profile_path, sizeof(app->profile_path), "%s", json_path);
        eset(app->entry_profile_path, json_path); /* the entry signal marks dirty: the saved state is clean */
        set_dirty(app, FALSE); update_panels(app); render_validation(app, false); update_status(app);
        persist_active_profile_conf(app);
        appendf_log(app, FALSE, _("[profile] duplicated to: %s (active)"), json_path);
    } else {
        snprintf(app->profile.metadata.id, sizeof(app->profile.metadata.id), "%s", old_id);
        snprintf(app->profile.metadata.name, sizeof(app->profile.metadata.name), "%s", old_name);
        appendf_log(app, FALSE, _("[profile] failed to duplicate to: %s"), json_path);
    }
    g_free(json_path);
}

static void profile_import_conf_path(App *app, const char *conf_path) {
    if (!app || !conf_path || !conf_path[0]) return;
    sim_profile_diags_t diags; sim_profile_diags_init(&diags);
    sim_profile_t imported; sim_profile_init(&imported);
    if (sim_profile_import_launcher_conf(&imported, conf_path, app->repo_root, &diags) != 0) {
        log_profile_diags(app, &diags, "import");
        appendf_log(app, FALSE, _("[profile] error importing launcher.conf: %s"), conf_path);
        sim_profile_free(&imported);
        sim_profile_diags_free(&diags);
        return;
    }
    sim_profile_free(&app->profile);
    app->profile = imported;
    app->profile_loaded = FALSE;
    /* keep current active profile path as save target (migration into profile) */
    ensure_profile_path(app);
    sync_cfg_from_profile(app);
    {
        launcher_config_t src;
        launcher_config_defaults(&src);
        if (launcher_config_load(&src, conf_path) == 0) {
            app->cfg.storage = src.storage;
            app->cfg.audio_net = src.audio_net;
            snprintf(app->cfg.emu_caps_path, sizeof(app->cfg.emu_caps_path), "%s", src.emu_caps_path);
            app->cfg.audio = src.audio;
            app->cfg.net_user = src.net_user;
        }
    }
    log_profile_diags(app, &diags, "import");
    refresh_after_profile_switch(app, NULL);
    appendf_log(app, FALSE, _("[profile] imported/migrated from %s to active profile '%s' (use Save to persist the JSON)"), conf_path, app->profile_path);
    sim_profile_diags_free(&diags);
}

static void profile_export_to_path(App *app, const char *dest_path) {
    if (!app || !dest_path || !dest_path[0]) return;
    char keep[LAUNCHER_MAX_PATH]; snprintf(keep, sizeof(keep), "%s", app->profile_path);
    if (save_profile_to_path(app, dest_path, false, false)) {
        appendf_log(app, FALSE, _("[profile] exported to: %s (active profile kept: %s)"), dest_path, keep);
        char a[PATH_MAX], c[PATH_MAX]; resolve_repo_path(app, dest_path, a, sizeof(a)); resolve_repo_path(app, keep, c, sizeof(c));
        if (strcmp(a, c) == 0) { set_dirty(app, FALSE); update_status(app); } /* exported over the active file: it now matches the UI */
    } else {
        appendf_log(app, FALSE, _("[profile] failed to export to: %s"), dest_path);
    }
    persist_active_profile_conf(app);
}

static void on_start(GtkButton *b, gpointer data){ (void)b; App*app=(App*)data; app->emu_start_us = g_get_monotonic_time(); do_start(app); refresh_telemetry(app); }
static void on_stop(GtkButton *b, gpointer data){ (void)b; stop_all((App*)data); refresh_telemetry((App*)data); }
static void on_restart(GtkButton *b, gpointer data){ (void)b; App *app=(App*)data; if (app->emu_proc && sim_proc_is_running(app->emu_proc)) { app->restart_pending=TRUE; append_emu_log(app,"[launcher] restart requested"); sim_proc_stop(app->emu_proc,0); stop_host(app);} else { app->emu_start_us = g_get_monotonic_time(); do_start(app);} update_status(app); }
static void on_save(GtkButton *b, gpointer data){ (void)b; App *app=(App*)data; apply_ui_to_cfg(app); save_profile_and_conf(app); update_panels(app); render_validation(app,false); set_dirty(app,FALSE); append_launcher_log(app,"[launcher] config/profile saved"); update_status(app); }
static void on_validate(GtkButton *b, gpointer data){ (void)b; App *app=(App*)data; render_validation(app,true); update_panels(app); append_launcher_log(app,_("[launcher] preflight/validation updated")); update_status(app); }
static void on_copy_command(GtkButton *b, gpointer data){ (void)b; App *app=(App*)data; char cmd[LAUNCHER_MAX_CMD+512]; build_effective_command_preview(app,cmd,sizeof(cmd)); GtkClipboard *cb=gtk_clipboard_get(GDK_SELECTION_CLIPBOARD); gtk_clipboard_set_text(cb,cmd,-1); append_launcher_log(app,_("[launcher] effective command copied")); }

static void on_refresh_caps(GtkButton *b, gpointer data){
    (void)b; App *app=(App*)data; if(!app) return;
    const char *path = etext(app->entry_emu_caps);
    if (path && path[0]) emu_caps_probe_set_path(&app->caps, path);
    else emu_caps_probe_set_path(&app->caps, EMU_DEFAULT_PATH);
    if (emu_caps_probe_path(&app->caps) == 0) append_launcher_log(app, "[caps] probe refreshed");
    else append_launcher_log(app, _("[caps] probe failed/incomplete (binary/args may be unavailable; conditional)"));
    render_validation(app, true); update_panels(app); update_status(app);
}

static void apply_log_filters(App *app){
    if (!app || !app->clog) return;
    clog_filter_t f = clog_filter_get(app->clog);
    f.level_mask = CLOG_ALL_LEVELS; f.origin_mask = CLOG_ALL_ORIGINS; f.substring[0] = '\0';
    if (app->combo_log_level) {
        switch (gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo_log_level))) {
            case 1: f.level_mask = CLOG_LEVEL_BIT(CLOG_LEVEL_UART); break;
            case 2: f.level_mask = CLOG_LEVEL_BIT(CLOG_LEVEL_INFO); break;
            case 3: f.level_mask = CLOG_LEVEL_BIT(CLOG_LEVEL_WARN); break;
            case 4: f.level_mask = CLOG_LEVEL_BIT(CLOG_LEVEL_ERROR); break;
            case 5: f.level_mask = CLOG_LEVEL_BIT(CLOG_LEVEL_HOST); break;
            default: f.level_mask = CLOG_ALL_LEVELS; break;
        }
    }
    if (app->combo_log_origin) {
        switch (gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo_log_origin))) {
            case 1: f.origin_mask = CLOG_ORIGIN_BIT(CLOG_ORIGIN_EMU); break;
            case 2: f.origin_mask = CLOG_ORIGIN_BIT(CLOG_ORIGIN_HOST); break;
            case 3: f.origin_mask = CLOG_ORIGIN_BIT(CLOG_ORIGIN_LAUNCHER); break;
            case 4: f.origin_mask = CLOG_ORIGIN_BIT(CLOG_ORIGIN_VALIDATION); break;
            default: f.origin_mask = CLOG_ALL_ORIGINS; break;
        }
    }
    if (app->entry_log_filter) {
        const char *s = etext(app->entry_log_filter);
        if (s) snprintf(f.substring, sizeof(f.substring), "%s", s);
    }
    clog_filter_set_levels(app->clog, f.level_mask);
    clog_filter_set_origins(app->clog, f.origin_mask);
    clog_filter_set_substring(app->clog, f.substring);
    refresh_log_views(app);
}
static void on_log_filter_changed(GtkWidget *w, gpointer data){ (void)w; apply_log_filters((App*)data); }
static void on_log_clear(GtkButton *b, gpointer data){ (void)b; App *app=(App*)data; if(app && app->clog){ clog_clear(app->clog); refresh_log_views(app);} }
static void on_log_export(GtkButton *b, gpointer data){
    (void)b; App *app=(App*)data; if(!app || !app->clog) return;
    char *path = choose_path_dialog(app, _("Export logs (text)"), GTK_FILE_CHOOSER_ACTION_SAVE, "simulator/logs/launcher-console.log");
    if (!path) return;
    char abs[PATH_MAX]; resolve_repo_path(app, path, abs, sizeof(abs)); g_free(path);
    if (clog_export_text(app->clog, abs, true)) append_launcher_log(app, _("[logs] exported (filtered)"));
    else append_launcher_log(app, _("[logs] failed to export"));
}
static void on_log_follow_toggled(GtkToggleButton *b, gpointer data){
    App *app=(App*)data; if(!app || !app->clog) return;
    clog_set_follow(app->clog, gtk_toggle_button_get_active(b));
}

static void on_merge_wizard(GtkButton *b, gpointer data){
    (void)b; App *app=(App*)data; if(!app) return;
    GtkWidget *dlg = gtk_dialog_new_with_buttons("Merge Wizard (esptool merge-bin)",
        GTK_WINDOW(app->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        _("Validate"), 1, _("Dry-run"), 2, _("Merge"), 3, _("Close"), GTK_RESPONSE_CLOSE, NULL);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *grid = gtk_grid_new(); gtk_grid_set_row_spacing(GTK_GRID(grid),4); gtk_grid_set_column_spacing(GTK_GRID(grid),6);
    gtk_container_set_border_width(GTK_CONTAINER(grid),8); gtk_box_pack_start(GTK_BOX(box), grid, FALSE,FALSE,0);
    fw_merge_plan_t plan; fw_merge_plan_init(&plan);
    (void)fw_merge_plan_defaults_for_board(launcher_board_id(app->cfg.board), &plan);
    GtkWidget *e_board = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(e_board), launcher_board_id(app->cfg.board));
    GtkWidget *e_boot = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(e_boot), plan.segments[FW_SEG_BOOTLOADER].path);
    GtkWidget *e_part = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(e_part), plan.segments[FW_SEG_PARTITIONS].path);
    GtkWidget *e_appb = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(e_appb), plan.segments[FW_SEG_APP].path);
    GtkWidget *e_fs   = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(e_fs), plan.segments[FW_SEG_FS].path);
    GtkWidget *e_out  = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(e_out), plan.output[0]?plan.output:"simulator/build/merged-emu.bin");
    GtkWidget *e_mode = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(e_mode), plan.flash_mode);
    GtkWidget *e_freq = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(e_freq), plan.flash_freq);
    GtkWidget *e_size = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(e_size), plan.flash_size);
    GtkWidget *chk_fs = gtk_check_button_new_with_label(_("Include FS (LittleFS)")); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_fs), plan.include_fs);
    GtkWidget *chk_ow = gtk_check_button_new_with_label(_("Overwrite output")); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_ow), TRUE);
    GtkWidget *lbl_note = gtk_label_new(_("SD card is not part of the merged image: esp-emu 1.1.0 mounts it with --sd (Card menu).")); gtk_label_set_xalign(GTK_LABEL(lbl_note),0.0);
    int r=0;
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Board")),0,r,1,1); gtk_grid_attach(GTK_GRID(grid), e_board,1,r,1,1); r++;
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Bootloader")),0,r,1,1); gtk_grid_attach(GTK_GRID(grid), e_boot,1,r,1,1); r++;
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Partitions")),0,r,1,1); gtk_grid_attach(GTK_GRID(grid), e_part,1,r,1,1); r++;
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("App")),0,r,1,1); gtk_grid_attach(GTK_GRID(grid), e_appb,1,r,1,1); r++;
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("FS image")),0,r,1,1); gtk_grid_attach(GTK_GRID(grid), e_fs,1,r,1,1); r++;
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Output")),0,r,1,1); gtk_grid_attach(GTK_GRID(grid), e_out,1,r,1,1); r++;
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Flash mode/freq/size")),0,r,1,1);
    GtkWidget *rowf = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_pack_start(GTK_BOX(rowf), e_mode, TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(rowf), e_freq, TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(rowf), e_size, TRUE,TRUE,0);
    gtk_grid_attach(GTK_GRID(grid), rowf,1,r,1,1); r++;
    gtk_grid_attach(GTK_GRID(grid), chk_fs,0,r,1,1); gtk_grid_attach(GTK_GRID(grid), chk_ow,1,r,1,1); r++;
    gtk_grid_attach(GTK_GRID(grid), lbl_note,0,r,2,1); r++;
    GtkWidget *sc = gtk_scrolled_window_new(NULL,NULL); gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sc),120);
    GtkWidget *tv = gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(tv),FALSE); gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv),TRUE);
    GtkTextBuffer *tb = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)); gtk_container_add(GTK_CONTAINER(sc), tv);
    gtk_box_pack_start(GTK_BOX(box), sc, TRUE,TRUE,0);
    gtk_widget_show_all(dlg);
    int resp;
    while ((resp = gtk_dialog_run(GTK_DIALOG(dlg))) != GTK_RESPONSE_CLOSE && resp != GTK_RESPONSE_DELETE_EVENT) {
        fw_merge_plan_init(&plan);
        (void)fw_merge_plan_defaults_for_board(etext(e_board), &plan);
        fw_merge_plan_set_segment(&plan, FW_SEG_BOOTLOADER, etext(e_boot));
        fw_merge_plan_set_segment(&plan, FW_SEG_PARTITIONS, etext(e_part));
        fw_merge_plan_set_segment(&plan, FW_SEG_APP, etext(e_appb));
        fw_merge_plan_set_fs(&plan, etext(e_fs), gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_fs)));
        fw_merge_plan_set_output(&plan, etext(e_out));
        fw_merge_plan_set_flash(&plan, etext(e_mode), etext(e_freq), etext(e_size));
        plan.overwrite = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_ow));
        fw_validation_t v; fw_validation_init(&v);
        (void)fw_merge_plan_validate(&plan, &v);
        char sum[2048]; sum[0]='\0'; (void)fw_merge_plan_summary(&plan, sum, sizeof(sum));
        GString *gs = g_string_new(sum);
        g_string_append_printf(gs, "\nvalidate: ok=%d errors=%d warnings=%d\n%s\n%s\n", v.ok, v.error_count, v.warning_count, v.errors, v.warnings);
        if (resp == 2 || resp == 3) {
            fw_merge_run_t run; memset(&run,0,sizeof(run));
            int rc = fw_merge_plan_run(&plan, resp == 2, &run);
            g_string_append_printf(gs, "%s: rc=%d exit=%d executed=%d\ncmd: %s\n%s\n",
                run.dry_run?"dry-run":"merge", rc, run.exit_code, run.executed?1:0, run.cmdline, run.output);
            if (!run.dry_run && rc == 0) {
                append_launcher_log(app, _("[merge] merged output created/updated"));
                if (app->entry_merged) { eset(app->entry_merged, plan.output); on_cfg_changed(NULL, app); }
            }
        }
        set_buf_text(tb, gs->str); g_string_free(gs, TRUE);
    }
    gtk_widget_destroy(dlg);
}

static void on_parts_scan(GtkButton *b, gpointer data) {
    (void)b; App *app = (App*)data; if (!app) return;
    const char *dir = etext(app->entry_parts_dir);
    if (!dir || !dir[0]) dir = "simulator/emu/parts";
    char abs[PATH_MAX]; resolve_repo_path(app, dir, abs, sizeof(abs));
    parts_ui_report_init(&app->parts_report);
    memset(&app->parts_ctx.parts, 0, sizeof(app->parts_ctx.parts)); app->parts_ctx.part_count = 0;
    memset(&app->parts_ctx.instances, 0, sizeof(app->parts_ctx.instances)); app->parts_ctx.instance_count = 0;
    app->parts_ctx.event_head = 0; app->parts_ctx.event_count = 0;
    app->parts_ctx.host_cosim_tcp_available = true;
    app->parts_ctx.host_cosim_board_available = app->cfg.cosim_board_path[0] != '\0';
    app->parts_ctx.esp_emu_has_wasm_parts = false; /* conditional(version/tool), runtime integration pending */
    app->parts_ctx.esp_emu_has_control_part_cmds = false;
    snprintf(app->parts_ctx.esp_emu_version, sizeof(app->parts_ctx.esp_emu_version), "unknown");
    int n = parts_ui_discover_dir(&app->parts_ctx, abs, &app->parts_report);
    if (n < 0) parts_ui_report_add(&app->parts_report, PARTS_UI_DIAG_ERROR, "PARTS_SCAN_FAIL", abs, _("Failed to discover parts in '%s'"), abs);
    else parts_ui_report_add(&app->parts_report, PARTS_UI_DIAG_INFO, "PARTS_SCAN_OK", abs, _("Discovered %d part(s) in '%s'"), n, abs);
    (void)parts_ui_validate(&app->parts_ctx, &app->parts_report);
    render_parts_ui(app);
}
static void on_parts_add_instance(GtkButton *b, gpointer data) {
    (void)b; App *app = (App*)data; if (!app) return;
    const char *iid = etext(app->entry_parts_instance_id);
    const char *pname = etext(app->entry_parts_part_name);
    char idbuf[PARTS_UI_MAX_NAME];
    if (!iid || !iid[0]) { snprintf(idbuf, sizeof(idbuf), "inst%d", app->parts_ctx.instance_count + 1); iid = idbuf; }
    if (!pname || !pname[0]) {
        if (app->parts_ctx.part_count > 0) pname = app->parts_ctx.parts[0].name;
        else { parts_ui_report_add(&app->parts_report, PARTS_UI_DIAG_ERROR, "PARTS_NO_PART", "-", _("No part selected/named")); render_parts_ui(app); return; }
    }
    parts_ui_report_init(&app->parts_report);
    if (parts_ui_configure_instance(&app->parts_ctx, iid, pname, launcher_board_id(app->cfg.board), app->profile.metadata.id, NULL, 0, true, &app->parts_report) == 0) {
        (void)parts_ui_set_instance_state(&app->parts_ctx, iid, PARTS_UI_SUPPORT_NEEDS_RUNTIME_HOST, false, _("configured (cosim runtime pending)"), NULL);
        parts_ui_report_add(&app->parts_report, PARTS_UI_DIAG_INFO, "PARTS_INST_OK", iid, _("Instance '%s' configured for part '%s' (needs-runtime-host)"), iid, pname);
    }
    (void)parts_ui_validate(&app->parts_ctx, &app->parts_report);
    render_parts_ui(app);
}
static void on_parts_remove_instance(GtkButton *b, gpointer data) {
    (void)b; App *app = (App*)data; if (!app) return;
    const char *iid = etext(app->entry_parts_instance_id);
    parts_ui_report_init(&app->parts_report);
    if (iid && iid[0] && parts_ui_remove_instance(&app->parts_ctx, iid) == 0)
        parts_ui_report_add(&app->parts_report, PARTS_UI_DIAG_INFO, "PARTS_INST_REMOVED", iid, _("Instance removed: %s"), iid);
    else
        parts_ui_report_add(&app->parts_report, PARTS_UI_DIAG_WARN, "PARTS_INST_NOT_FOUND", iid?iid:"-", _("Instance not found"));
    (void)parts_ui_validate(&app->parts_ctx, &app->parts_report);
    render_parts_ui(app);
}
static void on_parts_validate(GtkButton *b, gpointer data) {
    (void)b; App *app = (App*)data; if (!app) return;
    parts_ui_report_init(&app->parts_report);
    app->parts_ctx.host_cosim_board_available = app->cfg.cosim_board_path[0] != '\0';
    int rc = parts_ui_validate(&app->parts_ctx, &app->parts_report);
    parts_ui_report_add(&app->parts_report, rc == 0 ? PARTS_UI_DIAG_INFO : PARTS_UI_DIAG_WARN,
                        rc == 0 ? "PARTS_VALIDATE_OK" : "PARTS_VALIDATE_ERR", "-",
                        rc == 0 ? _("Validation passed with no errors (conflicts/capabilities checked)") : _("Validation found errors/conflicts"));
    parts_ui_report_add(&app->parts_report, PARTS_UI_DIAG_INFO, "PARTS_RUNTIME", "-",
                        _("Cosim runtime integration: placeholder (needs-runtime-host / conditional esp-emu)."));
    render_parts_ui(app);
}

static void choose_file_into_entry(App *app, GtkWidget *entry, const char *title, GtkFileChooserAction action) {
    if (!app || !entry) return;
    GtkWidget *dlg = gtk_file_chooser_dialog_new(title, GTK_WINDOW(app->window), action, "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_ACCEPT, NULL);
    const char *cur = etext(entry);
    if (cur && cur[0]) { char abs[PATH_MAX]; resolve_repo_path(app, cur, abs, sizeof(abs)); gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dlg), abs); }
    else if (app->repo_root[0]) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), app->repo_root);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) { char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg)); if (f) { eset(entry, f); g_free(f);} on_cfg_changed(NULL, app); }
    gtk_widget_destroy(dlg);
}
static char *choose_path_dialog(App *app, const char *title, GtkFileChooserAction action, const char *suggested) {
    if (!app) return NULL;
    GtkWidget *dlg = gtk_file_chooser_dialog_new(title, GTK_WINDOW(app->window), action,
        "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_ACCEPT, NULL);
    if (suggested && suggested[0]) {
        char abs[PATH_MAX]; resolve_repo_path(app, suggested, abs, sizeof(abs));
        if (action == GTK_FILE_CHOOSER_ACTION_SAVE) gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dlg), abs);
        else gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dlg), abs);
    } else if (app->repo_root[0]) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), app->repo_root);
    }
    char *ret = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) ret = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    gtk_widget_destroy(dlg);
    return ret;
}
static void on_browse_merged(GtkButton*b,gpointer d){(void)b; choose_file_into_entry((App*)d,((App*)d)->entry_merged,_("Select merged firmware"),GTK_FILE_CHOOSER_ACTION_OPEN);} 
static void on_browse_elf(GtkButton*b,gpointer d){(void)b; choose_file_into_entry((App*)d,((App*)d)->entry_elf,_("Select ELF"),GTK_FILE_CHOOSER_ACTION_OPEN);} 
static void on_browse_cosim(GtkButton*b,gpointer d){(void)b; choose_file_into_entry((App*)d,((App*)d)->entry_cosim_board,_("Select cosim board JSON"),GTK_FILE_CHOOSER_ACTION_OPEN);} 
static void on_browse_profile(GtkButton*b,gpointer d){(void)b; App*app=(App*)d; char *f=choose_path_dialog(app,_("Select/set the profile JSON path"),GTK_FILE_CHOOSER_ACTION_OPEN,etext(app->entry_profile_path)); if(f){eset(app->entry_profile_path,f); g_free(f);} on_cfg_changed(NULL,app);} 
static void on_browse_littlefs(GtkButton*b,gpointer d){(void)b; choose_file_into_entry((App*)d,((App*)d)->entry_littlefs,_("Select LittleFS image"),GTK_FILE_CHOOSER_ACTION_OPEN);} 
static void on_browse_audio_out(GtkButton*b,gpointer d){(void)b; choose_file_into_entry((App*)d,((App*)d)->entry_audio_out,_("Select I2S audio out (WAV)"),GTK_FILE_CHOOSER_ACTION_SAVE);} 
static void on_browse_audio_in(GtkButton*b,gpointer d){(void)b; choose_file_into_entry((App*)d,((App*)d)->entry_audio_in,_("Select I2S audio in (WAV)"),GTK_FILE_CHOOSER_ACTION_OPEN);} 
static void on_browse_caps(GtkButton*b,gpointer d){(void)b; choose_file_into_entry((App*)d,((App*)d)->entry_emu_caps,_("Select esp-emu (capabilities probe)"),GTK_FILE_CHOOSER_ACTION_OPEN);} 
static void on_browse_trace(GtkButton*b,gpointer d){(void)b; choose_file_into_entry((App*)d,((App*)d)->entry_trace_path,_("Select trace file"),GTK_FILE_CHOOSER_ACTION_SAVE);} 

static void on_profile_new(GtkButton*b,gpointer d){(void)b; profile_new_defaults((App*)d);} 
static void on_profile_open(GtkButton*b,gpointer d){(void)b; App*app=(App*)d; sync_profile_paths_from_entries(app); char *f=choose_path_dialog(app,_("Open/Load profile JSON"),GTK_FILE_CHOOSER_ACTION_OPEN, etext(app->entry_profile_path)); if(f){ eset(app->entry_profile_path,f); load_profile_from_path(app,f,TRUE,TRUE); g_free(f);} }
static void on_profile_save(GtkButton*b,gpointer d){ on_save(b,d); }
/* Save as to a new file: the copy gets its own id (slug of the file stem) and name (the stem). */
static void profile_identity_from_path(App *app, const char *json_path) {
    gchar *base = g_path_get_basename(json_path);
    size_t bl = strlen(base);
    gchar *stem = (bl > 5 && g_ascii_strcasecmp(base + bl - 5, ".json") == 0) ? g_strndup(base, bl - 5) : g_strdup(base);
    g_free(base);
    char id[SIM_PROFILE_MAX_ID]; size_t n = 0; gboolean dash = FALSE;
    for (const char *p = stem; *p && n + 1 < sizeof(id); p++) {
        char c = (char)g_ascii_tolower(*p);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) { id[n++] = c; dash = FALSE; }
        else if (n > 0 && !dash) { id[n++] = '-'; dash = TRUE; }
    }
    if (n > 0 && id[n - 1] == '-') n--;
    id[n] = '\0';
    if (!id[0]) snprintf(id, sizeof(id), "%s", "profile");
    snprintf(app->profile.metadata.id, sizeof(app->profile.metadata.id), "%s", id);
    snprintf(app->profile.metadata.name, sizeof(app->profile.metadata.name), "%s", stem);
    g_free(stem);
}
static void on_profile_save_as(GtkButton*b,gpointer d){(void)b; App*app=(App*)d; char *f=choose_path_dialog(app,_("Save profile as..."),GTK_FILE_CHOOSER_ACTION_SAVE, etext(app->entry_profile_path)); if(f){ char *jf=profile_json_name(f); g_free(f);
    char a[PATH_MAX], c[PATH_MAX]; resolve_repo_path(app, jf, a, sizeof(a)); resolve_repo_path(app, app->profile_path, c, sizeof(c));
    gboolean other = strcmp(a, c) != 0;
    char old_id[SIM_PROFILE_MAX_ID], old_name[SIM_PROFILE_MAX_NAME];
    snprintf(old_id, sizeof(old_id), "%s", app->profile.metadata.id); snprintf(old_name, sizeof(old_name), "%s", app->profile.metadata.name);
    if (other) profile_identity_from_path(app, jf);
    if (save_profile_to_path(app,jf,TRUE,TRUE)) {
        eset(app->entry_profile_path,jf); /* the entry signal marks dirty: the saved state is clean */
        set_dirty(app,FALSE); update_panels(app); render_validation(app,false); update_status(app);
    } else if (other) {
        snprintf(app->profile.metadata.id, sizeof(app->profile.metadata.id), "%s", old_id);
        snprintf(app->profile.metadata.name, sizeof(app->profile.metadata.name), "%s", old_name);
    }
    g_free(jf);} }
static void on_profile_duplicate(GtkButton*b,gpointer d){(void)b; App*app=(App*)d; char *f=choose_path_dialog(app,_("Duplicate profile to..."),GTK_FILE_CHOOSER_ACTION_SAVE, etext(app->entry_profile_path)); if(f){ profile_duplicate_active(app,f); g_free(f);} }
static void on_profile_import_conf(GtkButton*b,gpointer d){(void)b; App*app=(App*)d; char *f=choose_path_dialog(app,_("Import/migrate launcher.conf"),GTK_FILE_CHOOSER_ACTION_OPEN, LAUNCHER_CONF_PATH); if(f){ profile_import_conf_path(app,f); g_free(f);} }
static void on_profile_export(GtkButton*b,gpointer d){(void)b; App*app=(App*)d; char *f=choose_path_dialog(app,_("Export profile JSON"),GTK_FILE_CHOOSER_ACTION_SAVE, etext(app->entry_profile_path)); if(f){ char *jf=profile_json_name(f); g_free(f); profile_export_to_path(app,jf); g_free(jf);} }

static gboolean on_canvas_button(GtkWidget *w, GdkEventButton *e, gpointer data){ App*app=(App*)data; if(!app->host||!app->host_attached) return FALSE; if(e->button==1){ SimHostMouseAction a=(e->type==GDK_BUTTON_PRESS)?SIM_HOST_MOUSE_DOWN:SIM_HOST_MOUSE_UP; sim_host_handle_mouse(app->host,a,(int)e->x,(int)e->y,true);} gtk_widget_grab_focus(w); return TRUE; }
static gboolean on_canvas_motion(GtkWidget *w, GdkEventMotion *e, gpointer data){ (void)w; App*app=(App*)data; if(!app->host||!app->host_attached) return FALSE; if(e->state&GDK_BUTTON1_MASK) sim_host_handle_mouse(app->host,SIM_HOST_MOUSE_MOVE,(int)e->x,(int)e->y,true); return TRUE; }
static gboolean on_canvas_configure(GtkWidget *w, GdkEventConfigure *e, gpointer data){ (void)w; App*app=(App*)data; if(app->host&&app->host_attached) sim_host_handle_resize(app->host,e->width,e->height); return FALSE; }
/* Canvas repaint: the dark backdrop is drawn here only while no host is attached (the summary card sits above it);
 * once the SDL host owns the window it repaints the whole view itself. */
static gboolean on_canvas_draw(GtkWidget *w, cairo_t *cr, gpointer data){
    App *app = (App*)data; if (app->host && app->host_attached) return TRUE;
    int cw = gtk_widget_get_allocated_width(w), ch = gtk_widget_get_allocated_height(w);
    cairo_set_source_rgb(cr, 0.043, 0.059, 0.078); cairo_paint(cr);
    cairo_set_source_rgba(cr, 0.137, 0.176, 0.227, 0.55); /* faint dot grid, 24 px */
    for (int y = 12; y < ch; y += 24) for (int x = 12; x < cw; x += 24) cairo_rectangle(cr, x, y, 1.5, 1.5);
    cairo_fill(cr);
    return TRUE;
}
static gboolean canvas_render_idle(gpointer data){ App*app=(App*)data; if(app->host&&app->host_attached){ sim_host_update(app->host); sim_host_render(app->host);} return G_SOURCE_REMOVE; }
/* Panel shown/hidden or divider dragged: tell the SDL view the new size and repaint it immediately (again once layout settles). */
static void on_canvas_size_allocate(GtkWidget *w, GdkRectangle *a, gpointer data){
    (void)w; App*app=(App*)data;
    if(app->host&&app->host_attached){ sdl_sync_events(); sdl_set_canvas_size(a->width,a->height); sim_host_handle_resize(app->host,a->width,a->height); sim_host_render(app->host); g_idle_add(canvas_render_idle, app); }
    else gtk_widget_queue_draw(app->canvas);
}
/* Save the active profile on quit: the same steps as Save (save_profile_and_conf), with the result. */
static gboolean save_active_profile_on_quit(App *app) {
    sync_profile_paths_from_entries(app);
    ensure_profile_path(app);
    return save_profile_to_path(app, app->profile_path, true, false);
}
/* Quit path of window close, Ctrl+Q and Profile > Quit. The profile file is written only by Save, Save as, Duplicate and Export.
 * A dirty profile asks first: Save writes it and quits, Discard quits without writing, Cancel (or Escape) keeps the launcher open. */
static gboolean on_delete(GtkWidget *w, GdkEvent *e, gpointer data){
    (void)w;(void)e; App*app=(App*)data;
    if (app->dirty) {
        GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(app->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
            "%s", _("The profile has unsaved changes."));
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", _("Save the active profile before quitting?"));
        gtk_dialog_add_buttons(GTK_DIALOG(d), _("Cancel"), GTK_RESPONSE_CANCEL, _("Discard"), GTK_RESPONSE_NO, _("Save"), GTK_RESPONSE_YES, NULL);
        gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_CANCEL);
        gint rr = gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d);
        if (rr == GTK_RESPONSE_YES) {
            if (!save_active_profile_on_quit(app)) {
                GtkWidget *m = gtk_message_dialog_new(GTK_WINDOW(app->window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", _("Could not save the profile."));
                gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(m), "%s", _("The launcher stays open. See the log."));
                gtk_dialog_run(GTK_DIALOG(m)); gtk_widget_destroy(m);
                return TRUE;
            }
        } else if (rr != GTK_RESPONSE_NO) {
            return TRUE; /* Cancel, Escape or the dialog closed: keep the launcher open */
        }
    }
    app->restart_pending=FALSE; stop_all(app); return FALSE;
}
static void on_destroy(GtkWidget *w, gpointer data){ (void)w; App*app=(App*)data; apply_ui_to_cfg(app); save_ui_layout_to_cfg(app);
    /* launcher.conf only: the profile file is not written here (see on_delete). */
    if (app->profile_path[0]) snprintf(app->cfg.profile_path, sizeof(app->cfg.profile_path), "%s", app->profile_path);
    launcher_config_save(&app->cfg, LAUNCHER_CONF_PATH);
    stop_host(app); if(app->emu_proc){sim_proc_free(app->emu_proc); app->emu_proc=NULL;} if(app->bridge_proc){sim_proc_free(app->bridge_proc); app->bridge_proc=NULL;} if(app->build_proc){sim_proc_free(app->build_proc); app->build_proc=NULL;} if(app->clog){clog_destroy(app->clog); app->clog=NULL;} sim_profile_free(&app->profile); gtk_main_quit(); }


static void on_workflow_open_file(GtkButton *b, gpointer data) {
    (void)b; App *app=(App*)data; if(!app||!app->workflow_edit_buf) return;
    const char *path = etext(app->entry_workflow_file);
    char rp[PATH_MAX]; resolve_repo_path(app, path, rp, sizeof(rp));
    gchar *contents=NULL; gsize len=0;
    if (path[0] && g_file_get_contents(rp, &contents, &len, NULL)) {
        set_buf_text(app->workflow_edit_buf, contents);
        g_free(contents);
        append_launcher_log(app, _("[workflow] file opened"));
    } else {
        append_launcher_log(app, _("[workflow] open failed (give a valid path)"));
    }
}
static void on_workflow_save_file(GtkButton *b, gpointer data) {
    (void)b; App *app=(App*)data; if(!app||!app->workflow_edit_buf) return;
    const char *path = etext(app->entry_workflow_file);
    char rp[PATH_MAX]; resolve_repo_path(app, path, rp, sizeof(rp));
    GtkTextIter s0,e0; gtk_text_buffer_get_bounds(app->workflow_edit_buf,&s0,&e0);
    gchar *txt = gtk_text_buffer_get_text(app->workflow_edit_buf,&s0,&e0,FALSE);
    if (path[0] && txt && g_file_set_contents(rp, txt, -1, NULL)) append_launcher_log(app, _("[workflow] file saved"));
    else append_launcher_log(app, _("[workflow] save failed"));
    g_free(txt);
}
static void on_build_output(const char *line, gpointer user_data){ append_emu_log((App*)user_data, line); }
static void on_build_exit(SimProc *proc, gint wait_status, gpointer user_data){
    (void)proc; (void)wait_status; App *app=(App*)user_data;
    append_launcher_log(app, _("[workflow] build finished"));
    if (app && app->workflow_buf) set_buf_text(app->workflow_buf, _("Build finished (see logs). Tools/exit depend on the environment."));
    update_status(app);
}
static void on_workflow_build(GtkButton *b, gpointer data) {
    (void)b; App *app=(App*)data; if(!app) return;
    if (!app->build_proc) app->build_proc = sim_proc_new();
    const char *cmd = etext(app->entry_build_cmd);
    if (!cmd || !cmd[0]) cmd = "./start.sh emu build";
    GError *err=NULL;
    append_launcher_log(app, _("[workflow] running build..."));
    if (app->workflow_buf) set_buf_text(app->workflow_buf, _("Running: build via start.sh (conditional: toolchain present)"));
    if (!sim_proc_start_shell(app->build_proc, cmd, app->repo_root, on_build_output, app, on_build_exit, app, &err)) {
        append_launcher_log(app, _("[workflow] failed to start build (tool missing?)"));
        if (err) { append_launcher_log(app, err->message); g_clear_error(&err); }
    }
}

/* ---- SD card actions (menu Cartão). esp-emu 1.1.0 mounts the image with --sd at start and cannot insert or
 * remove a card during a run, so every card swap on a running emulator is a controlled restart. ---- */
static void card_changed(App *app, const char *msg) {
    append_launcher_log(app, msg);
    apply_ui_to_cfg(app); /* also copies the card fields into the in-memory profile */
    set_dirty(app, TRUE); /* the card belongs to the profile: Save or the quit dialog writes it */
    save_conf_only(app);
    update_panels(app); render_validation(app, false);
    if (app->emu_proc && sim_proc_is_running(app->emu_proc)) {
        append_launcher_log(app, _("[card] emulator running: controlled restart to apply the card"));
        on_restart(NULL, app);
    }
    update_status(app);
}
static void on_card_replace(GtkMenuItem *mi, gpointer data) {
    (void)mi; App *app = (App*)data; if (!app) return;
    GtkWidget *dlg = gtk_file_chooser_dialog_new(_("Replace SD card image (restarts the emulator)"), GTK_WINDOW(app->window),
        GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileFilter *ff = gtk_file_filter_new(); gtk_file_filter_set_name(ff, _("Images (*.img)")); gtk_file_filter_add_pattern(ff, "*.img");
    GtkFileFilter *fa = gtk_file_filter_new(); gtk_file_filter_set_name(fa, _("All files")); gtk_file_filter_add_pattern(fa, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), ff); gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), fa);
    char abs[PATH_MAX]; resolve_repo_path(app, app->cfg.sd_image_path, abs, sizeof(abs));
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dlg), abs);
    char *f = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    gtk_widget_destroy(dlg);
    if (!f) return;
    /* store repo-relative when the image lives inside the repository (keeps the default image recognisable) */
    size_t rl = strlen(app->repo_root);
    const char *store = (rl > 0 && strncmp(f, app->repo_root, rl) == 0 && f[rl] == '/') ? f + rl + 1 : f;
    snprintf(app->cfg.sd_image_path, sizeof(app->cfg.sd_image_path), "%s", store);
    app->cfg.sd_enabled = true;
    char msg[PATH_MAX + 64]; snprintf(msg, sizeof(msg), _("[card] image replaced: %s"), app->cfg.sd_image_path);
    g_free(f);
    card_changed(app, msg);
}
static void on_card_eject(GtkMenuItem *mi, gpointer data) {
    (void)mi; App *app = (App*)data; if (!app) return;
    app->cfg.sd_enabled = false;
    /* the legacy SD checkbox feeds storage.sd.enabled (a second --sd after the tail "--"): keep it in step */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_sd_enabled), FALSE);
    card_changed(app, _("[card] card ejected (--no-sd on next start)"));
}
static void on_card_exit(SimProc *proc, gint wait_status, gpointer user_data) {
    (void)proc; App *app = (App*)user_data;
    gchar *m = g_strdup_printf(_("[card] finished (%s, state %d)"), (WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 0) ? "ok" : _("ERROR"), WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : wait_status);
    append_launcher_log(app, m); g_free(m);
    update_panels(app); update_status(app);
}
static void on_card_output(const char *line, gpointer user_data) { append_launcher_log((App*)user_data, line); }
/* Runs "./start.sh card ..." with the existing process helper; output goes to the launcher log. */
static void card_run(App *app, const char *args_fmt_cmd) {
    if (!app->build_proc) app->build_proc = sim_proc_new();
    if (sim_proc_is_running(app->build_proc)) { append_launcher_log(app, _("[card] another build/card process is still running")); return; }
    GError *err = NULL;
    append_launcher_log(app, args_fmt_cmd);
    if (!sim_proc_start_shell(app->build_proc, args_fmt_cmd, app->repo_root, on_card_output, app, on_card_exit, app, &err)) {
        append_launcher_log(app, _("[card] failed to start start.sh card"));
        if (err) { append_launcher_log(app, err->message); g_clear_error(&err); }
    }
}
static void on_card_build(GtkMenuItem *mi, gpointer data) {
    (void)mi; App *app = (App*)data; if (!app) return;
    if (app->emu_proc && sim_proc_is_running(app->emu_proc)) {
        GtkWidget *m = gtk_message_dialog_new(GTK_WINDOW(app->window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            _("Stop the emulator before rebuilding the card image (the image is mounted by esp-emu)."));
        gtk_dialog_run(GTK_DIALOG(m)); gtk_widget_destroy(m); return;
    }
    char *dir = choose_path_dialog(app, _("Folder with the card contents"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "simulator/SD_Card");
    if (!dir) return;
    GtkWidget *m = gtk_message_dialog_new(GTK_WINDOW(app->window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
        _("Building the image from a folder REPLACES the image %s.\nWhat the firmware wrote to the card is lost. Continue?"), app->cfg.sd_image_path);
    gint rr = gtk_dialog_run(GTK_DIALOG(m)); gtk_widget_destroy(m);
    if (rr == GTK_RESPONSE_YES) {
        gchar *qd = g_shell_quote(dir), *qi = g_shell_quote(app->cfg.sd_image_path);
        gchar *cmd = g_strdup_printf("./start.sh card image --force %s %s", qd, qi);
        card_run(app, cmd); g_free(cmd); g_free(qd); g_free(qi);
    }
    g_free(dir);
}
static void on_card_extract(GtkMenuItem *mi, gpointer data) {
    (void)mi; App *app = (App*)data; if (!app) return;
    char *dir = choose_path_dialog(app, _("Destination folder for extracting the card"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "simulator/emu/storage/sd-extract");
    if (!dir) return;
    gchar *qd = g_shell_quote(dir), *qi = g_shell_quote(app->cfg.sd_image_path);
    gchar *cmd = g_strdup_printf("./start.sh card extract --force %s %s", qi, qd);
    card_run(app, cmd); g_free(cmd); g_free(qd); g_free(qi); g_free(dir);
}
static void on_card_open_folder(GtkMenuItem *mi, gpointer data) {
    (void)mi; App *app = (App*)data; if (!app) return;
    gchar *p = g_build_filename(app->repo_root, "simulator", "SD_Card", NULL);
    gchar *uri = g_filename_to_uri(p, NULL, NULL);
    GError *err = NULL;
    if (!uri || !g_app_info_launch_default_for_uri(uri, NULL, &err)) {
        append_launcher_log(app, _("[card] could not open simulator/SD_Card (xdg-open)"));
        if (err) { append_launcher_log(app, err->message); g_clear_error(&err); }
    }
    g_free(p); g_free(uri);
}

/* ---- Nav rail + drawer: the rail items pick a config page, a second click on the open page closes the drawer. ---- */
static void nav_sync(App *app) {
    if (!app->w_cfg_panel) return;
    gboolean vis = gtk_widget_get_visible(app->w_cfg_panel);
    int cur = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook_cfg));
    static const char *titles[4] = { N_("PROFILE / FIRMWARE"), N_("RUNTIME / AUDIO / NETWORK"), N_("Merge / Run"), N_("STORAGE / PARTS") };
    for (int i = 0; i < 4; i++) {
        GtkStyleContext *sc = gtk_widget_get_style_context(app->rail_btn[i]);
        if (vis && i == cur) gtk_style_context_add_class(sc, "active"); else gtk_style_context_remove_class(sc, "active");
    }
    if (cur >= 0 && cur < 4) gtk_label_set_text(GTK_LABEL(app->lbl_drawer_title), _(titles[cur]));
}
static void on_rail_clicked(GtkButton *b, gpointer data) {
    App *app = (App*)data; int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "page"));
    gboolean vis = gtk_widget_get_visible(app->w_cfg_panel);
    int cur = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook_cfg));
    if (vis && cur == i) { gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->mi_view_cfg), FALSE); return; }
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook_cfg), i);
    if (!vis) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->mi_view_cfg), TRUE);
    nav_sync(app);
}
/* Drawer (left panel) default width in px: wide enough for the Runtime page at 1280 px. */
#define CFG_PANEL_W_DEFAULT 460
/* ---- Ver menu: panels on demand. Hidden panels use gtk_widget_hide so the canvas gets the space; the divider
 * position is remembered on hide and restored on show. ---- */
static void view_apply_cfg(App *app, gboolean show) {
    if (!app->w_cfg_panel) return;
    if (show) {
        gtk_widget_show(app->w_cfg_panel);
        gtk_paned_set_position(GTK_PANED(app->paned_main), app->cfg_panel_w > 100 ? app->cfg_panel_w : CFG_PANEL_W_DEFAULT);
    } else if (gtk_widget_get_visible(app->w_cfg_panel)) {
        int pos = gtk_paned_get_position(GTK_PANED(app->paned_main));
        if (pos > 100) app->cfg_panel_w = pos;
        gtk_widget_hide(app->w_cfg_panel);
    }
    nav_sync(app);
}
static void view_apply_dock(App *app) {
    if (!app->w_dock) return;
    gboolean bottom_on = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(app->mi_view_bottom));
    gboolean logs_on = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(app->mi_view_logs));
    for (int i = 0; i < 6; i++) if (app->dock_pages_tools[i]) gtk_widget_set_visible(app->dock_pages_tools[i], bottom_on);
    for (int i = 0; i < 3; i++) if (app->dock_pages_logs[i]) gtk_widget_set_visible(app->dock_pages_logs[i], logs_on);
    gtk_widget_set_visible(app->w_log_bar, logs_on);
    GtkNotebook *nb = GTK_NOTEBOOK(app->notebook_bottom);
    GtkWidget *cur = gtk_notebook_get_nth_page(nb, gtk_notebook_get_current_page(nb));
    if (!cur || !gtk_widget_get_visible(cur)) {
        GtkWidget *first = logs_on && !bottom_on ? app->dock_pages_logs[0] : app->dock_pages_tools[0];
        if (!bottom_on && !logs_on) first = NULL;
        if (first) gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, first));
    }
    gboolean show = bottom_on || logs_on;
    if (app->btn_dock && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->btn_dock)) != show) { app->nav_syncing = TRUE; gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->btn_dock), show); app->nav_syncing = FALSE; }
    if (show && !gtk_widget_get_visible(app->w_dock)) {
        gtk_widget_show(app->w_dock);
        refresh_log_views(app);
        int h =gtk_widget_get_allocated_height(app->paned_right);
        int dh = app->dock_h > 80 ? app->dock_h : 280;
        if (h > dh + 80) gtk_paned_set_position(GTK_PANED(app->paned_right), h - dh);
    } else if (!show && gtk_widget_get_visible(app->w_dock)) {
        int h = gtk_widget_get_allocated_height(app->paned_right);
        int dh = h - gtk_paned_get_position(GTK_PANED(app->paned_right));
        if (dh > 80) app->dock_h = dh;
        gtk_widget_hide(app->w_dock);
    }
}
/* Applies the remembered Ver menu state once the window is allocated (divider positions need a real size). */
static gboolean view_init_cb(gpointer data) {
    App *app = (App*)data;
    if (gtk_widget_get_visible(app->w_cfg_panel)) gtk_paned_set_position(GTK_PANED(app->paned_main), app->cfg_panel_w > 100 ? app->cfg_panel_w : CFG_PANEL_W_DEFAULT);
    if (gtk_widget_get_visible(app->w_dock)) {
        int h = gtk_widget_get_allocated_height(app->paned_right), dh = app->dock_h > 80 ? app->dock_h : 280;
        if (h > dh + 80) gtk_paned_set_position(GTK_PANED(app->paned_right), h - dh);
    }
    return G_SOURCE_REMOVE;
}
static void on_view_cfg_toggled(GtkCheckMenuItem *mi, gpointer data) {
    App *app = (App*)data; gboolean on = gtk_check_menu_item_get_active(mi);
    view_apply_cfg(app, on); app->cfg.ui_view_cfg = on; schedule_layout_save(app);
}
static void on_view_dock_toggled(GtkCheckMenuItem *mi, gpointer data) {
    (void)mi; App *app = (App*)data;
    view_apply_dock(app);
    app->cfg.ui_view_bottom = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(app->mi_view_bottom));
    app->cfg.ui_view_logs = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(app->mi_view_logs));
    schedule_layout_save(app);
}
/* Status bar button: opens/closes the whole bottom dock (tools + logs). */
static void on_dock_btn_toggled(GtkToggleButton *b, gpointer data) {
    App *app = (App*)data; if (app->nav_syncing) return;
    gboolean on = gtk_toggle_button_get_active(b);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->mi_view_bottom), on);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->mi_view_logs), on);
}
static void on_view_status_toggled(GtkCheckMenuItem *mi, gpointer data) {
    App *app = (App*)data; gboolean on = gtk_check_menu_item_get_active(mi);
    gtk_widget_set_visible(app->w_statusbar, on); app->cfg.ui_view_status = on; schedule_layout_save(app);
}
/* Ver > Janela de GPIO/Pins: shows the bottom dock and focuses the GPIO/Pins tab (kept simple: no separate window). */
static void on_view_gpio(GtkMenuItem *mi, gpointer data) {
    (void)mi; App *app = (App*)data;
    if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(app->mi_view_bottom)))
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->mi_view_bottom), TRUE);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook_bottom), gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook_bottom), app->gpio_page));
}
/* Help menu: the windows live in help_ui.c */
static void on_help_contents(GtkMenuItem *mi, gpointer data) { (void)mi; help_show_contents(GTK_WINDOW(((App*)data)->window)); }
static void on_help_shortcuts(GtkMenuItem *mi, gpointer data) { (void)mi; help_show_shortcuts(GTK_WINDOW(((App*)data)->window)); }
static void on_help_about(GtkMenuItem *mi, gpointer data) { (void)mi; help_show_about(GTK_WINDOW(((App*)data)->window)); }
/* menu construction helpers */
static void on_ui_lang_selected(GtkCheckMenuItem *mi, gpointer data) {
    App *app = (App*)data;
    if (!gtk_check_menu_item_get_active(mi)) return;
    const char *code = (const char*)g_object_get_data(G_OBJECT(mi), "ui-lang");
    if (!code || strcmp(app->cfg.ui_lang, code) == 0) return;
    snprintf(app->cfg.ui_lang, sizeof(app->cfg.ui_lang), "%s", code);
    (void)launcher_config_save(&app->cfg, LAUNCHER_CONF_PATH);
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(app->window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                          "%s", _("Restart the launcher to apply the language."));
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}
static GtkWidget *menu_top(GtkWidget *bar, const char *label) {
    GtkWidget *top = gtk_menu_item_new_with_mnemonic(label), *sub = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(top), sub); gtk_menu_shell_append(GTK_MENU_SHELL(bar), top);
    return sub;
}
static void on_quit(GtkWidget *w, gpointer data){ (void)w; App*app=(App*)data; gtk_window_close(GTK_WINDOW(app->window)); }
static GtkWidget *menu_add(GtkWidget *menu, const char *label, GCallback cb, gpointer data, const char *tip) {
    GtkWidget *it = gtk_menu_item_new_with_label(label);
    if (cb) g_signal_connect(it, "activate", cb, data);
    if (tip) gtk_widget_set_tooltip_text(it, tip);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    return it;
}
static GtkWidget *menu_check(GtkWidget *menu, const char *label, gboolean active, GCallback cb, gpointer data) {
    GtkWidget *it = gtk_check_menu_item_new_with_label(label);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), active);
    if (cb) g_signal_connect(it, "toggled", cb, data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    return it;
}

static GtkWidget *labeled(const char *label, GtkWidget *w){ GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,2); GtkWidget*l=gtk_label_new(label); gtk_widget_set_halign(l,GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(box),l,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),w,FALSE,FALSE,0); return box; }
static GtkWidget *with_browse(GtkWidget *entry,const char*btn_label,GCallback cb,gpointer data,const char*tip){ GtkWidget*box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4); gtk_box_pack_start(GTK_BOX(box),entry,TRUE,TRUE,0); GtkWidget*btn=gtk_button_new_with_label(btn_label?btn_label:"Browse"); if(cb) g_signal_connect(btn,"clicked",cb,data); if(tip) gtk_widget_set_tooltip_text(btn,tip); gtk_box_pack_start(GTK_BOX(box),btn,FALSE,FALSE,0); return box; }
static void add_tooltip(GtkWidget*w,const char*tip){ if(w&&tip) gtk_widget_set_tooltip_text(w,tip); }
static GtkWidget *make_log_view(GtkTextBuffer **buf_out){ GtkWidget*sc=gtk_scrolled_window_new(NULL,NULL); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC); GtkWidget*tv=gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(tv),FALSE); gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv),TRUE); *buf_out=gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)); gtk_container_add(GTK_CONTAINER(sc),tv); return sc; }

/* Drawer pages must be able to shrink below their natural width (dragging the divider): scroll instead of forcing the minimum. */
static GtkWidget *scroll_wrap(GtkWidget *w) {
    GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sc), w);
    return sc;
}
/* Narrow drawer widgets: entries ask for a short natural width (they fill the column), check-button labels wrap.
 * Without this the Runtime grid asks for ~650 px and the CFG_PANEL_W_DEFAULT px drawer scrolls with hidden overlay bars. */
static void drawer_fit_width(GtkWidget *w) {
    if (GTK_IS_ENTRY(w)) {
        gtk_entry_set_width_chars(GTK_ENTRY(w), 14);
        gtk_widget_set_hexpand(w, TRUE);
    } else if (GTK_IS_CHECK_BUTTON(w)) {
        GtkWidget *lbl = gtk_bin_get_child(GTK_BIN(w));
        if (GTK_IS_LABEL(lbl)) {
            gtk_label_set_line_wrap(GTK_LABEL(lbl), TRUE);
            gtk_label_set_max_width_chars(GTK_LABEL(lbl), 30);
        }
    }
    if (GTK_IS_CONTAINER(w)) {
        GList *kids = gtk_container_get_children(GTK_CONTAINER(w));
        for (GList *k = kids; k; k = k->next) drawer_fit_width(k->data);
        g_list_free(kids);
    }
}
static void combo_ellipsize(GtkWidget *c) {
    GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(c));
    for (GList *l = cells; l; l = l->next) g_object_set(l->data, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    g_list_free(cells);
}

static void sync_ui_from_state(App *app){
    if (!app) return;
    app->ui_syncing = TRUE;
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->combo_board), app->cfg.board);
    if (app->combo_chip) gtk_combo_box_set_active(GTK_COMBO_BOX(app->combo_chip), chip_index_for_id(app->profile.board_hardware.mcu.chip[0]?app->profile.board_hardware.mcu.chip:"esp32s3"));
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->combo_firmware), app->cfg.firmware);
    eset(app->entry_profile_path, app->profile_path[0]?app->profile_path:ACTIVE_PROFILE_PATH);
    eset(app->entry_merged, app->cfg.merged_image_path);
    eset(app->entry_elf, app->cfg.elf_path);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_use_elf), app->cfg.use_elf);
    eset(app->entry_custom, app->cfg.custom_firmware_path);
    eset(app->entry_cosim_board, app->cfg.cosim_board_path);
    eset(app->entry_littlefs, app->cfg.storage.littlefs.image_path[0]?app->cfg.storage.littlefs.image_path:
                               (app->profile.storage.littlefs_image_path[0]?app->profile.storage.littlefs_image_path:app->profile.firmware.artifacts.littlefs));
    eset(app->entry_littlefs_label, app->cfg.storage.littlefs.partition_label);
    eset(app->entry_littlefs_mount, app->cfg.storage.littlefs.mount_path);
    {
        char num[32];
        snprintf(num, sizeof(num), "%u", (unsigned)app->cfg.storage.littlefs.offset);
        eset(app->entry_littlefs_offset, num);
        snprintf(num, sizeof(num), "%u", (unsigned)app->cfg.storage.littlefs.size);
        eset(app->entry_littlefs_size, num);
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_littlefs_enabled), app->cfg.storage.littlefs.enabled);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_sd_enabled), app->cfg.storage.sd.enabled);
    eset(app->entry_sd_image, app->cfg.storage.sd.image_path);
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->combo_sd_mode),
                             app->cfg.storage.sd.mode == SLFS_SD_MODE_SDSPI ? 1 :
                             (app->cfg.storage.sd.mode == SLFS_SD_MODE_NONE ? 2 : 0));
    {
        char num[32];
        snprintf(num, sizeof(num), "%d", app->cfg.storage.sd.slot);
        eset(app->entry_sd_slot, num);
        snprintf(num, sizeof(num), "%d", app->cfg.storage.sd.cs_pin);
        eset(app->entry_sd_cs, num);
    }
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin_lcd_w), app->cfg.lcd_w);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin_lcd_h), app->cfg.lcd_h);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin_scale), app->cfg.scale);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin_rot), app->cfg.hw_rotation);
    if (app->spin_instance) gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin_instance), app->cfg.instance);
    update_derived_port_widgets(app);
    eset(app->entry_console_mode, app->profile.emu_runtime.console_mode[0]?app->profile.emu_runtime.console_mode:"tcp");
    eset(app->entry_extra, app->cfg.extra_args);
    eset(app->entry_emu_path, app->profile.emu_runtime.esp_emu_path);
    if (app->entry_emu_caps) eset(app->entry_emu_caps, app->cfg.emu_caps_path);
    if (app->combo_run_mode) gtk_combo_box_set_active(GTK_COMBO_BOX(app->combo_run_mode), app->cfg.run_mode ? 1 : 0);
    if (app->check_cosim_unix) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_cosim_unix), app->cfg.cosim_use_unix);
    if (app->entry_cosim_unix) eset(app->entry_cosim_unix, app->cfg.cosim_unix_path);
    if (app->check_gdb_enabled) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_gdb_enabled), FALSE);
    if (app->check_gdb_halt) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_gdb_halt), FALSE);
    if (app->check_trace_enabled) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_trace_enabled), FALSE);
    if (app->entry_trace_path) eset(app->entry_trace_path, "simulator/logs/emu.trace");
    app->run_mode = app->cfg.run_mode;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_audio), app->cfg.audio);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_audio_realtime), app->cfg.audio_net.i2s_realtime);
    eset(app->entry_audio_out, app->cfg.audio_net.i2s_wav_out[0]?app->cfg.audio_net.i2s_wav_out:app->profile.audio.out);
    eset(app->entry_audio_in, app->cfg.audio_net.i2s_wav_in[0]?app->cfg.audio_net.i2s_wav_in:app->profile.audio.in);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin_audio_instance), app->cfg.audio_net.i2s_instance);
    eset(app->entry_audio_backend, app->cfg.audio_net.i2s_backend_hint);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_net_user), app->cfg.audio_net.net_enabled);
    eset(app->entry_net_mode, app->cfg.audio_net.net_mode);
    {
        char hf[AUDIO_NET_MAX_HOSTFWD * AUDIO_NET_MAX_TEXT];
        if (launcher_config_get_hostfwd(&app->cfg, hf, sizeof(hf)) != 0) hf[0] = '\0';
        eset(app->entry_hostfwd, hf);
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_wifi), app->cfg.audio_net.wifi_enabled);
    eset(app->entry_wifi_ssid, app->cfg.audio_net.wifi_ssid);
    eset(app->entry_wifi_password, app->cfg.audio_net.wifi_password);
    {
        int ai = 4;
        switch (app->cfg.audio_net.wifi_auth) {
            case AUDIO_NET_WIFI_AUTH_OPEN: ai = 0; break;
            case AUDIO_NET_WIFI_AUTH_WPA_PSK: ai = 1; break;
            case AUDIO_NET_WIFI_AUTH_WPA2_PSK: ai = 2; break;
            case AUDIO_NET_WIFI_AUTH_WPA3_PSK: ai = 3; break;
            default: ai = 4; break;
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->combo_wifi_auth), ai);
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_ble), app->cfg.audio_net.ble_enabled);
    eset(app->entry_ble_hci, app->cfg.audio_net.ble_hci);
    gtk_widget_set_sensitive(app->entry_custom, app->cfg.firmware == LAUNCHER_FW_CUSTOM);
    gtk_widget_set_sensitive(app->entry_elf, app->cfg.use_elf);
    app->ui_syncing=FALSE;
}

static void print_usage(void){
    printf("ESP32 Unified Simulator Launcher (GTK3)\n");
    printf("Usage: ./simulator/launcher/sim_launcher [--help]\n\n");
    printf("Single-app frontend: config/profile + embedded SDL cosim canvas + esp-emu subprocess logs.\n");
    printf("Config: %s | Active profile: %s | State: %s,%s\n", LAUNCHER_CONF_PATH, ACTIVE_PROFILE_PATH, HW_ROT_STATE_PATH, HW_BOARD_STATE_PATH);
}

static gboolean layout_save_idle(gpointer data) {
    App *app = (App*)data;
    if (!app) return G_SOURCE_REMOVE;
    app->layout_save_idle_id = 0;
    save_ui_layout_to_cfg(app);
    (void)launcher_config_save(&app->cfg, LAUNCHER_CONF_PATH);
    return G_SOURCE_REMOVE;
}
static void schedule_layout_save(App *app) {
    if (!app || app->restoring_layout) return;
    if (app->layout_save_idle_id == 0) app->layout_save_idle_id = g_idle_add(layout_save_idle, app);
}
static void save_ui_layout_to_cfg(App *app) {
    if (!app || !app->window) return;
    int w=0,h=0,x=0,y=0;
    gtk_window_get_size(GTK_WINDOW(app->window), &w, &h);
    gtk_window_get_position(GTK_WINDOW(app->window), &x, &y);
    app->cfg.ui_window_w = w; app->cfg.ui_window_h = h;
    app->cfg.ui_window_x = x; app->cfg.ui_window_y = y;
    app->cfg.ui_window_has_pos = (x!=0 || y!=0);
    app->cfg.ui_window_maximized = gtk_window_is_maximized(GTK_WINDOW(app->window));
    if (app->paned_main) app->cfg.ui_paned_main_pos = gtk_paned_get_position(GTK_PANED(app->paned_main));
    if (app->paned_right) app->cfg.ui_paned_bottom_pos = gtk_paned_get_position(GTK_PANED(app->paned_right));
    if (app->notebook_cfg) app->cfg.ui_tab_config = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook_cfg));
    if (app->notebook_bottom) app->cfg.ui_tab_bottom = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook_bottom));
    if (app->notebook_logs) app->cfg.ui_tab_log = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook_logs));
    if (app->entry_log_filter) snprintf(app->cfg.ui_log_search, sizeof(app->cfg.ui_log_search), "%s", etext(app->entry_log_filter));
    if (app->combo_log_level) app->cfg.ui_log_min_level = gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo_log_level));
    if (app->combo_log_origin) app->cfg.ui_log_origin_mask = (unsigned)gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo_log_origin));
    if (app->check_log_follow) app->cfg.ui_log_follow = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_log_follow));
}
static void restore_ui_layout(App *app) {
    if (!app || !app->window) return;
    app->restoring_layout = TRUE;
    if (app->cfg.ui_window_w > 200 && app->cfg.ui_window_h > 200)
        gtk_window_resize(GTK_WINDOW(app->window), app->cfg.ui_window_w, app->cfg.ui_window_h);
    if (app->cfg.ui_window_has_pos) gtk_window_move(GTK_WINDOW(app->window), app->cfg.ui_window_x, app->cfg.ui_window_y);
    if (app->cfg.ui_window_maximized) gtk_window_maximize(GTK_WINDOW(app->window));
    if (app->notebook_cfg && app->cfg.ui_tab_config >= 0) gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook_cfg), app->cfg.ui_tab_config);
    if (app->entry_log_filter && app->cfg.ui_log_search[0]) eset(app->entry_log_filter, app->cfg.ui_log_search);
    if (app->combo_log_level && app->cfg.ui_log_min_level >= 0) gtk_combo_box_set_active(GTK_COMBO_BOX(app->combo_log_level), app->cfg.ui_log_min_level);
    if (app->combo_log_origin && app->cfg.ui_log_origin_mask <= 16) gtk_combo_box_set_active(GTK_COMBO_BOX(app->combo_log_origin), (gint)app->cfg.ui_log_origin_mask);
    if (app->check_log_follow) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_log_follow), app->cfg.ui_log_follow);
    app->restoring_layout = FALSE;
}
static void refresh_parts_runtime_hints(App *app) {
    if (!app) return;
    app->parts_ctx.host_cosim_board_available = app->cfg.cosim_board_path[0] != '\0';
    app->parts_ctx.host_cosim_tcp_available = true;
    parts_ui_set_context_capabilities(&app->parts_ctx,
        app->parts_ctx.esp_emu_version[0] ? app->parts_ctx.esp_emu_version : "unknown",
        "esp-emu",
        app->parts_ctx.esp_emu_has_wasm_parts,
        app->parts_ctx.esp_emu_has_control_part_cmds,
        app->cfg.cosim_board_path[0] != '\0',
        true,
        true,
        app->cfg.cosim_board_path[0] != '\0');
    parts_ui_set_cosim_board_path(&app->parts_ctx, app->cfg.cosim_board_path, app->cfg.cosim_board_path[0] != '\0');
    render_parts_ui(app);
}
static void render_debug_gpio_panels(App *app) {
    if (!app) return;
    if (app->debug_buf) {
        GString *g = g_string_new(NULL);
        bool gdb_on = app->check_gdb_enabled && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_gdb_enabled));
        bool trace_on = app->check_trace_enabled && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_trace_enabled));
        g_string_append_printf(g, _("Debug/Profiler (available data, factual)\n"));
        g_string_append_printf(g, "GDB: %s", gdb_on?_("configured (--gdb, conditional esp-emu+ELF+gdb)"):_("inactive"));
        if (gdb_on) { sim_ports_t ports; app_ports(app, &ports); g_string_append_printf(g, " port=%d%s", ports.gdb, (app->check_gdb_halt&&gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_gdb_halt)))?" halt":""); }
        g_string_append(g, "\n");
        g_string_append_printf(g, "Trace: %s", trace_on?_("configured (--trace, conditional)"):_("inactive"));
        if (trace_on && app->entry_trace_path) {
            const char *tp = gtk_entry_get_text(GTK_ENTRY(app->entry_trace_path));
            g_string_append_printf(g, " path=%s", tp&&tp[0]?tp:"(none)");
            if (tp && tp[0]) {
                char rp[PATH_MAX]; struct stat st; resolve_repo_path(app,tp,rp,sizeof(rp));
                if (stat(rp,&st)==0) g_string_append_printf(g, " size=%lldB",(long long)st.st_size);
                else g_string_append(g, " (not created yet)");
            }
        }
        g_string_append(g, "\n");
        if (app->emu_proc) g_string_append_printf(g, _("Process: running=%s pid=%d uptime=%llums\n"), sim_proc_is_running(app->emu_proc)?"yes":"no", (int)sim_proc_get_pid(app->emu_proc), (unsigned long long)app->telem.uptime_ms);
        else g_string_append(g, _("Process: unavailable\n"));
        g_string_append_printf(g, "Run mode: %s | Cosim: %s\n", app->run_mode==1?"backend-only":"ui+embedded", (app->run_mode==0&&app->cfg.cosim_use_unix)?"unix-bridge(socat,conditional)":"tcp");
        g_string_append(g, _("Limits: GPIO/regs/mem/perf/cycles/mips/PC-level live = needs-runtime-host (hooks not connected to the runtime).\n"));
        g_string_append(g, _("sdmmc S3: --sd supported in esp-emu 1.1.0. ELF/trace/gdb = conditional(version/tool).\n"));
        set_buf_text(app->debug_buf, g->str); g_string_free(g, TRUE);
    }
    if (app->gpio_buf) {
        GString *g = g_string_new(NULL);
        const sim_board_t *b = sim_board_catalog_get(&app->boards, launcher_board_id(app->cfg.board));
        g_string_append(g, _("GPIO/Pins/Claims (available state)\n"));
        g_string_append_printf(g, "Board preset: %s | Chip: %s\n", launcher_board_id(app->cfg.board), app->profile.board_hardware.mcu.chip[0]?app->profile.board_hardware.mcu.chip:"esp32s3");
        if (b) {
            g_string_append_printf(g, "Display: %dx%d controller=%s bus=%s iface=%s\n",
                b->display.width,b->display.height,
                b->display.controller[0]?b->display.controller:"?",
                b->display.bus[0]?b->display.bus:"?",
                b->display.interface[0]?b->display.interface:"?");
            g_string_append_printf(g, "Touch/Input: type=%s controller=%s bus=%s addr=%s\n",
                b->touch_input.type[0]?b->touch_input.type:"?",
                b->touch_input.controller[0]?b->touch_input.controller:"none",
                b->touch_input.bus[0]?b->touch_input.bus:"?",
                b->touch_input.i2c_addr[0]?b->touch_input.i2c_addr:"-");
            g_string_append_printf(g, "Support: serial=%s display_live=%s touch=%s parallel_needs_runtime_host=%s\n",
                b->support.serial_headless[0]?b->support.serial_headless:"?", b->support.display_live[0]?b->support.display_live:"?",
                b->support.touch_input[0]?b->support.touch_input:"?", b->support.parallel_needs_runtime_host?"yes":"no");
        }
        g_string_append(g, _("Dynamic mappings of pins/bus/controllers/protocols/input: conditional/needs-runtime-host (compile-time/partial runtime host).\n"));
        g_string_append(g, _("Live GPIO telemetry/runtime claims not connected (hook missing).\n"));
        set_buf_text(app->gpio_buf, g->str); g_string_free(g, TRUE);
    }
}

int main(int argc, char **argv){
    /* Language first: gettext must be bound before any string or widget is created. */
    char ui_lang[LAUNCHER_MAX_SHORT];
    i18n_read_saved_setting(LAUNCHER_CONF_PATH, ui_lang, sizeof(ui_lang));
    i18n_init(ui_lang);
    for(int i=1;i<argc;i++) if(!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")){ print_usage(); return 0; }
    gdk_set_allowed_backends("x11,*"); gtk_init(&argc,&argv);
    App app = {0}; app.emu_proc = sim_proc_new(); app.bridge_proc = sim_proc_new(); app.build_proc = sim_proc_new();
    app.clog = clog_new(4000);
    emu_caps_probe_init(&app.caps);
    emu_caps_probe_set_path(&app.caps, EMU_DEFAULT_PATH);
    if (getcwd(app.repo_root,sizeof(app.repo_root))==NULL) snprintf(app.repo_root,sizeof(app.repo_root),".");
    sim_profile_init(&app.profile); load_profile_or_migrate(&app); launcher_config_resolve_paths(&app.cfg);
    telem_init(&app.telem); telem_set_capabilities_defaults(&app.telem);
    parts_ui_context_init(&app.parts_ctx); parts_ui_report_init(&app.parts_report);
    app.parts_ctx.host_cosim_tcp_available = true;
    app.parts_ctx.host_cosim_board_available = app.cfg.cosim_board_path[0] != '\0';
    app.parts_ctx.esp_emu_has_wasm_parts = false; /* conditional(version/tool) */
    app.parts_ctx.esp_emu_has_control_part_cmds = false;
    snprintf(app.parts_ctx.esp_emu_version, sizeof(app.parts_ctx.esp_emu_version), "unknown");

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), _("ESP32 Simulator (Unified)"));
    gtk_window_set_default_size(GTK_WINDOW(app.window), 1280, 800);
    g_signal_connect(app.window,"delete-event",G_CALLBACK(on_delete),&app);
    g_signal_connect(app.window,"destroy",G_CALLBACK(on_destroy),&app);
    app.cfg_panel_w = CFG_PANEL_W_DEFAULT; app.dock_h = 280;
    GtkAccelGroup *accel = gtk_accel_group_new(); gtk_window_add_accel_group(GTK_WINDOW(app.window), accel);

    /* Layout: menu bar / toolbar (Start, Stop, Restart, profile, board, status badge, timer) / body / status bar.
     * Body = nav rail | horizontal paned [ drawer (config pages, hidden) | vertical paned [ canvas (+ summary card) | bottom dock (hidden) ] ].
     * The canvas has no size request and expand/fill, so both dividers drag freely; panels are shown on demand (rail, Ver menu). */
    theme_apply();
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL,0); gtk_container_add(GTK_CONTAINER(app.window), root);
    GtkWidget *menubar = gtk_menu_bar_new(); gtk_box_pack_start(GTK_BOX(root), menubar, FALSE, FALSE, 0);
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8); gtk_style_context_add_class(gtk_widget_get_style_context(toolbar), "tb"); gtk_box_pack_start(GTK_BOX(root), toolbar, FALSE, FALSE, 0);
    app.entry_profile_path = gtk_entry_new();
    app.btn_start = gtk_button_new_with_label(_("▶ Start")); app.btn_stop = gtk_button_new_with_label(_("■ Stop")); app.btn_restart = gtk_button_new_with_label(_("↻ Restart"));
    gtk_style_context_add_class(gtk_widget_get_style_context(app.btn_start), "primary");
    gtk_widget_set_tooltip_text(app.btn_start, _("Simulator → Start (Ctrl+R)")); gtk_widget_set_tooltip_text(app.btn_stop, _("Simulator → Stop (Ctrl+.)")); gtk_widget_set_tooltip_text(app.btn_restart, _("Simulator → Restart (Ctrl+Shift+R)"));
    gtk_box_pack_start(GTK_BOX(toolbar), app.btn_start, FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(toolbar), app.btn_stop, FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(toolbar), app.btn_restart, FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(toolbar), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE,FALSE,4);
    app.lbl_profile = gtk_label_new("-"); gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_profile), "tb-profile");
    gtk_label_set_ellipsize(GTK_LABEL(app.lbl_profile), PANGO_ELLIPSIZE_END); gtk_label_set_max_width_chars(GTK_LABEL(app.lbl_profile), 32);
    app.lbl_board = gtk_label_new("-"); gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_board), "tb-board");
    gtk_widget_set_tooltip_text(app.lbl_board, _("Board preset and LCD size in use"));
    gtk_box_pack_start(GTK_BOX(toolbar), app.lbl_profile, FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(toolbar), app.lbl_board, FALSE,FALSE,0);
    app.lbl_timer = gtk_label_new("00:00:00"); gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_timer), "tb-timer");
    gtk_widget_set_tooltip_text(app.lbl_timer, _("Time since the emulator started"));
    app.lbl_badge = gtk_label_new(_("STOPPED")); gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_badge), "badge"); gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_badge), "badge-stopped");
    gtk_widget_set_tooltip_text(app.lbl_badge, _("Emulator state. RUNNING appears 2 s after the host connects"));
    gtk_box_pack_end(GTK_BOX(toolbar), app.lbl_timer, FALSE,FALSE,0);
    gtk_box_pack_end(GTK_BOX(toolbar), app.lbl_badge, FALSE,FALSE,0);
    /* single status line (profile, dirty flag, run state, esp-emu version, SD state): lives in the status bar; the long text is its tooltip */
    app.status_label = gtk_label_new(_("Stopped")); gtk_widget_set_halign(app.status_label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(app.status_label), PANGO_ELLIPSIZE_END);
    app.path_label = gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(app.path_label),0.0); gtk_label_set_selectable(GTK_LABEL(app.path_label),TRUE); gtk_label_set_line_wrap(GTK_LABEL(app.path_label),TRUE);
    app.caps_label = gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(app.caps_label),0.0); gtk_label_set_ellipsize(GTK_LABEL(app.caps_label), PANGO_ELLIPSIZE_END);

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0); gtk_box_pack_start(GTK_BOX(root), body, TRUE, TRUE, 0);
    /* nav rail: one flat button per config page; the active one has the orange left bar */
    GtkWidget *rail = gtk_box_new(GTK_ORIENTATION_VERTICAL,0); gtk_style_context_add_class(gtk_widget_get_style_context(rail), "rail"); gtk_box_pack_start(GTK_BOX(body), rail, FALSE, FALSE, 0);
    {
        static const char *rn[4] = { N_("Profile"), N_("Runtime"), N_("Merge / Run"), N_("Storage") };
        static const char *rs[4] = { N_("Firmware"), N_("Audio · Network"), N_("Merge · Run modes"), N_("Parts · LittleFS") };
        static const char *rt[4] = { N_("Profile and firmware settings"), N_("Runtime, audio, network, LittleFS and SD settings"), N_("Run mode notes. The merge wizard is not built yet"), N_("Parts (WASM) list. LittleFS and SD are on the Runtime page") };
        for (int i = 0; i < 4; i++) {
            gchar *m = g_strdup_printf("%s\n<span size=\"small\" foreground=\"#7d8a99\">%s</span>", _(rn[i]), _(rs[i]));
            GtkWidget *l = gtk_label_new(NULL); gtk_label_set_markup(GTK_LABEL(l), m); g_free(m); gtk_label_set_xalign(GTK_LABEL(l), 0.0);
            app.rail_btn[i] = gtk_button_new(); gtk_container_add(GTK_CONTAINER(app.rail_btn[i]), l);
            gtk_style_context_add_class(gtk_widget_get_style_context(app.rail_btn[i]), "rail-item");
            gtk_button_set_relief(GTK_BUTTON(app.rail_btn[i]), GTK_RELIEF_NONE);
            g_object_set_data(G_OBJECT(app.rail_btn[i]), "page", GINT_TO_POINTER(i));
            g_signal_connect(app.rail_btn[i], "clicked", G_CALLBACK(on_rail_clicked), &app);
            gtk_widget_set_size_request(app.rail_btn[i], 132, -1);
            gtk_widget_set_tooltip_text(app.rail_btn[i], _(rt[i]));
            gtk_box_pack_start(GTK_BOX(rail), app.rail_btn[i], FALSE, FALSE, 0);
        }
    }

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL); gtk_paned_set_wide_handle(GTK_PANED(paned), TRUE); gtk_widget_set_hexpand(paned,TRUE); gtk_widget_set_vexpand(paned,TRUE); gtk_box_pack_start(GTK_BOX(body), paned, TRUE, TRUE, 0);
    app.paned_main = paned;
    /* drawer: header (title + close) + profile path + the config notebook (its tabs are replaced by the rail) */
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL,8); gtk_style_context_add_class(gtk_widget_get_style_context(left), "drawer"); gtk_paned_pack1(GTK_PANED(paned), left, FALSE, TRUE);
    app.w_cfg_panel = left;
    {
        GtkWidget *dh = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4); gtk_style_context_add_class(gtk_widget_get_style_context(dh), "drawer-head");
        app.lbl_drawer_title = gtk_label_new(_("PROFILE / FIRMWARE")); gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_drawer_title), "drawer-title");
        gtk_widget_set_halign(app.lbl_drawer_title, GTK_ALIGN_START);
        GtkWidget *bx = gtk_button_new_with_label("✕"); gtk_style_context_add_class(gtk_widget_get_style_context(bx), "flat-btn"); gtk_widget_set_tooltip_text(bx, _("Close panel"));
        gtk_box_pack_start(GTK_BOX(dh), app.lbl_drawer_title, TRUE, TRUE, 0); gtk_box_pack_end(GTK_BOX(dh), bx, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(left), dh, FALSE, FALSE, 0);
        app.btn_drawer_close = bx;
    }
    /* The drawer content scrolls, so its natural width cannot push the canvas column off a 1280 px window. */
    GtkWidget *left_in = gtk_box_new(GTK_ORIENTATION_VERTICAL,8); gtk_container_set_border_width(GTK_CONTAINER(left_in), 8);
    GtkWidget *left_sc = gtk_scrolled_window_new(NULL, NULL); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(left_sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(left_sc), FALSE); /* bars always visible, not only on hover */
    gtk_container_add(GTK_CONTAINER(left_sc), left_in); gtk_box_pack_start(GTK_BOX(left), left_sc, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(left_in), labeled(_("Active profile (JSON)"), with_browse(app.entry_profile_path,_("Browse"),G_CALLBACK(on_browse_profile),&app,_("Choose a profile file"))), FALSE, FALSE, 0);
    GtkWidget *cfg_nb = gtk_notebook_new(); gtk_notebook_set_show_tabs(GTK_NOTEBOOK(cfg_nb), FALSE); gtk_notebook_set_show_border(GTK_NOTEBOOK(cfg_nb), FALSE); gtk_box_pack_start(GTK_BOX(left_in), cfg_nb, TRUE, TRUE, 0);
    app.notebook_cfg = cfg_nb;

    GtkWidget *tab_main = gtk_grid_new(); gtk_grid_set_row_spacing(GTK_GRID(tab_main),6); gtk_grid_set_column_spacing(GTK_GRID(tab_main),8); gtk_container_set_border_width(GTK_CONTAINER(tab_main),8);
    gtk_notebook_append_page(GTK_NOTEBOOK(cfg_nb), scroll_wrap(tab_main), gtk_label_new(_("Profile/Firmware")));
    app.combo_board = gtk_combo_box_text_new(); for(int i=0;i<LAUNCHER_BOARD_COUNT;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_board), launcher_board_id((launcher_board_t)i));
    app.combo_firmware = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_firmware), "Local es3c28p emu (build/merged-emu.bin)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_firmware), "KryonOS v3 per-board (firmware/kryonos-v3/<board>/merged.bin)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_firmware), "Asset es3c28p (firmware/es3c28p/merged.bin)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_firmware), "Custom merged path");
    app.entry_merged = gtk_entry_new(); app.entry_elf = gtk_entry_new(); app.check_use_elf = gtk_check_button_new_with_label(_("Use ELF (--elf)"));
    app.entry_custom = gtk_entry_new(); app.entry_cosim_board = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_cosim_board), _("cosim board json"));
    app.spin_lcd_w = gtk_spin_button_new_with_range(0,1024,1); app.spin_lcd_h = gtk_spin_button_new_with_range(0,1024,1);
    app.spin_scale = gtk_spin_button_new_with_range(1,4,1); app.spin_rot = gtk_spin_button_new_with_range(0,3,1);
    app.combo_chip = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_chip), "esp32s3");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_chip), "esp32c3");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_chip), "esp32c5");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_chip), "esp32c6");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_chip), "esp32h2");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_chip), "esp32p4");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_chip), "esp32s31");
    gtk_grid_attach(GTK_GRID(tab_main), labeled(_("Board preset"), app.combo_board), 0,0,2,1);
    gtk_grid_attach(GTK_GRID(tab_main), labeled(_("Target chip (esp-emu)"), app.combo_chip), 0,1,2,1);
    gtk_grid_attach(GTK_GRID(tab_main), labeled(_("Firmware selection"), app.combo_firmware), 0,2,2,1);
    gtk_grid_attach(GTK_GRID(tab_main), labeled(_("Merged firmware"), with_browse(app.entry_merged,_("Browse"),G_CALLBACK(on_browse_merged),&app,_("Choose the merged firmware image"))), 0,3,2,1);
    gtk_grid_attach(GTK_GRID(tab_main), labeled(_("ELF"), with_browse(app.entry_elf,_("Browse"),G_CALLBACK(on_browse_elf),&app,_("Choose the ELF file"))), 0,4,2,1);
    gtk_grid_attach(GTK_GRID(tab_main), app.check_use_elf, 0,5,2,1);
    gtk_grid_attach(GTK_GRID(tab_main), labeled(_("Custom firmware path (legacy)"), app.entry_custom), 0,6,2,1);
    gtk_grid_attach(GTK_GRID(tab_main), labeled(_("Cosim board JSON"), with_browse(app.entry_cosim_board,_("Browse"),G_CALLBACK(on_browse_cosim),&app,_("Choose the cosim board JSON"))), 0,7,2,1);
    gtk_grid_attach(GTK_GRID(tab_main), labeled(_("LCD W (0=default)"), app.spin_lcd_w), 0,8,1,1);
    gtk_grid_attach(GTK_GRID(tab_main), labeled(_("LCD H (0=default)"), app.spin_lcd_h), 1,8,1,1);
    gtk_grid_attach(GTK_GRID(tab_main), labeled(_("Scale"), app.spin_scale), 0,9,1,1);
    gtk_grid_attach(GTK_GRID(tab_main), labeled(_("HW rot"), app.spin_rot), 1,9,1,1);
    gtk_widget_set_tooltip_text(app.entry_profile_path, _("Profile file that Save writes. Empty uses profiles/active.json"));
    gtk_widget_set_tooltip_text(app.combo_board, _("Board from boards/*.json. Sets cosim board and display size defaults"));
    gtk_widget_set_tooltip_text(app.combo_chip, _("Chip for esp-emu. The es3c28p board is an ESP32-S3"));
    gtk_widget_set_tooltip_text(app.combo_firmware, _("Firmware image to run: local build, KryonOS v3, asset or custom path"));
    gtk_widget_set_tooltip_text(app.entry_merged, _("Merged flash image (.bin) passed to esp-emu"));
    gtk_widget_set_tooltip_text(app.entry_elf, _("ELF with symbols. Used only when Use ELF is checked"));
    gtk_widget_set_tooltip_text(app.entry_custom, _("Old custom firmware path. Used only with Custom firmware selected"));
    gtk_widget_set_tooltip_text(app.spin_scale, _("Whole-number zoom of the LCD on the canvas (1 to 4)"));
    gtk_widget_set_tooltip_text(app.spin_rot, _("Hardware rotation of the LCD and touch, 0 to 3"));

    GtkWidget *tab_runtime_sc = gtk_scrolled_window_new(NULL,NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tab_runtime_sc),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
    gtk_notebook_append_page(GTK_NOTEBOOK(cfg_nb), tab_runtime_sc, gtk_label_new(_("Runtime/Audio/Network")));
    GtkWidget *tab_runtime = gtk_grid_new(); gtk_grid_set_row_spacing(GTK_GRID(tab_runtime),6); gtk_grid_set_column_spacing(GTK_GRID(tab_runtime),8); gtk_container_set_border_width(GTK_CONTAINER(tab_runtime),8);
    gtk_container_add(GTK_CONTAINER(tab_runtime_sc), tab_runtime);
    app.combo_run_mode = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_run_mode), "UI + embedded host (cosim)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_run_mode), "Backend only (--no-host/cosim)");
    app.check_cosim_unix = gtk_check_button_new_with_label(_("Cosim via Unix socket bridge (socat, conditional)"));
    app.entry_cosim_unix = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_cosim_unix), "/tmp/esp-sim-cosim.sock");
    app.entry_uart = gtk_entry_new(); app.spin_instance = gtk_spin_button_new_with_range(0, SIM_INSTANCE_MAX, 1); app.entry_console_mode = gtk_entry_new(); app.entry_extra = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_extra),_("extra args for esp-emu (after --)"));
    app.entry_emu_path = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_emu_path),_("emu_runtime.esp_emu_path (basic)"));
    app.check_audio = gtk_check_button_new_with_label(_("Audio enabled")); app.check_audio_realtime = gtk_check_button_new_with_label(_("Realtime audio (conditional)"));
    app.entry_audio_out = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_audio_out),"simulator/logs/i2s_out.wav"); app.entry_audio_in = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_audio_in),_("I2S WAV in (conditional)"));
    app.check_net_user = gtk_check_button_new_with_label(_("net enabled (user mode legacy)")); app.check_wifi = gtk_check_button_new_with_label(_("Wi-Fi (conditional)")); app.check_ble = gtk_check_button_new_with_label(_("BLE (conditional)"));
    app.check_littlefs_enabled = gtk_check_button_new_with_label(_("LittleFS enabled (conditional)")); app.entry_littlefs = gtk_entry_new();
    /* T20 widgets (created; advanced storage/audio/net/caps) */
    app.entry_littlefs_label = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_littlefs_label),_("partition label"));
    app.entry_littlefs_mount = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_littlefs_mount),_("mount path"));
    app.entry_littlefs_offset = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_littlefs_offset),_("offset"));
    app.entry_littlefs_size = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_littlefs_size),_("size"));
    app.check_sd_enabled = gtk_check_button_new_with_label(_("SD (legacy: use the Card menu)"));
    app.entry_sd_image = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_sd_image),_("SD image (conditional)"));
    app.entry_sd_slot = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(app.entry_sd_slot),"-1");
    app.entry_sd_cs = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(app.entry_sd_cs),"-1");
    app.combo_sd_mode = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_sd_mode), "sdmmc");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_sd_mode), "sdspi");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_sd_mode), "none");
    app.spin_audio_instance = gtk_spin_button_new_with_range(-1,8,1);
    app.entry_audio_backend = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_audio_backend),_("audio backend hint"));
    app.check_net_enabled = gtk_check_button_new_with_label(_("Network enabled"));
    app.entry_net_mode = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_net_mode),_("user/tap/openeth (conditional)"));
    app.entry_hostfwd = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_hostfwd),_("hostfwd rules ; separated"));
    app.entry_wifi_ssid = gtk_entry_new(); app.entry_wifi_password = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(app.entry_wifi_password), FALSE);
    app.combo_wifi_auth = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_wifi_auth), "open");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_wifi_auth), "wpa2-wpa3");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_wifi_auth), "wpa2-psk");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_wifi_auth), "wpa3-sae");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_wifi_auth), "unknown");
    app.entry_ble_hci = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_ble_hci),_("BLE HCI (conditional)"));
    app.entry_emu_caps = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_emu_caps),_("esp-emu path for caps probe"));
    gtk_entry_set_text(GTK_ENTRY(app.entry_emu_caps), EMU_DEFAULT_PATH);
    GtkWidget *btn_refresh_caps = gtk_button_new_with_label(_("Refresh caps"));
    int rr=0;
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("Run mode"), app.combo_run_mode), 0,rr,2,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("Instance"), app.spin_instance), 0,rr,2,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), app.check_cosim_unix, 0,rr,1,1);
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("Cosim Unix path"), app.entry_cosim_unix), 1,rr,1,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("UART TCP port (from instance)"), app.entry_uart), 0,rr,2,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("Console mode (tcp/stdio/pty/none)"), app.entry_console_mode), 0,rr,2,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("esp-emu path (basic)"), app.entry_emu_path), 0,rr,2,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("esp-emu caps path (probe)"), with_browse(app.entry_emu_caps,_("Browse"),G_CALLBACK(on_browse_caps),&app,_("Choose the esp-emu binary for the probe"))), 0,rr,1,1);
    gtk_grid_attach(GTK_GRID(tab_runtime), btn_refresh_caps, 1,rr,1,1); rr++;
    app.check_gdb_enabled = gtk_check_button_new_with_label(_("GDB stub (--gdb, conditional tool)"));
    app.spin_gdb_port = gtk_spin_button_new_with_range(1024,65535,1);
    app.check_gdb_halt = gtk_check_button_new_with_label(_("Halt on start (--gdb-halt)"));
    gtk_grid_attach(GTK_GRID(tab_runtime), app.check_gdb_enabled, 0,rr,1,1);
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("GDB port"), app.spin_gdb_port), 1,rr,1,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), app.check_gdb_halt, 0,rr,2,1); rr++;
    app.check_trace_enabled = gtk_check_button_new_with_label(_("Trace (--trace, conditional tool)"));
    app.entry_trace_path = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_trace_path), "simulator/logs/emu.trace");
    gtk_grid_attach(GTK_GRID(tab_runtime), app.check_trace_enabled, 0,rr,1,1);
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("Trace path"), with_browse(app.entry_trace_path,_("Browse"),G_CALLBACK(on_browse_trace),&app,_("Choose the trace output file"))), 1,rr,1,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("Extra args"), app.entry_extra), 0,rr,2,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), app.check_audio, 0,rr,1,1);
    gtk_grid_attach(GTK_GRID(tab_runtime), app.check_audio_realtime, 1,rr,1,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("Audio out path"), with_browse(app.entry_audio_out,_("Browse"),G_CALLBACK(on_browse_audio_out),&app,_("Choose the output WAV file"))), 0,rr,2,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("Audio in path"), with_browse(app.entry_audio_in,_("Browse"),G_CALLBACK(on_browse_audio_in),&app,_("Choose the input WAV file"))), 0,rr,2,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("I2S instance"), app.spin_audio_instance), 0,rr,1,1);
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("Audio backend hint"), app.entry_audio_backend), 1,rr,1,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), app.check_net_user, 0,rr,1,1);
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("Net mode"), app.entry_net_mode), 1,rr,1,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("Hostfwd (; sep)"), app.entry_hostfwd), 0,rr,2,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), app.check_wifi, 0,rr,1,1);
    gtk_grid_attach(GTK_GRID(tab_runtime), app.check_ble, 1,rr,1,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("Wi-Fi SSID"), app.entry_wifi_ssid), 0,rr,1,1);
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("Wi-Fi password"), app.entry_wifi_password), 1,rr,1,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("Wi-Fi auth"), app.combo_wifi_auth), 0,rr,1,1);
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("BLE HCI"), app.entry_ble_hci), 1,rr,1,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), app.check_littlefs_enabled, 0,rr,2,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("LittleFS image"), with_browse(app.entry_littlefs,_("Browse"),G_CALLBACK(on_browse_littlefs),&app,_("Choose the LittleFS image file"))), 0,rr,2,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("LittleFS label"), app.entry_littlefs_label), 0,rr,1,1);
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("LittleFS mount"), app.entry_littlefs_mount), 1,rr,1,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("LittleFS offset"), app.entry_littlefs_offset), 0,rr,1,1);
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("LittleFS size"), app.entry_littlefs_size), 1,rr,1,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), app.check_sd_enabled, 0,rr,1,1);
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("SD mode"), app.combo_sd_mode), 1,rr,1,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("SD image"), app.entry_sd_image), 0,rr,2,1); rr++;
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("SD slot"), app.entry_sd_slot), 0,rr,1,1);
    gtk_grid_attach(GTK_GRID(tab_runtime), labeled(_("SD CS pin"), app.entry_sd_cs), 1,rr,1,1); rr++;
    drawer_fit_width(tab_runtime);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(tab_runtime_sc), FALSE);
    g_signal_connect(btn_refresh_caps, "clicked", G_CALLBACK(on_refresh_caps), &app);
    gtk_widget_set_tooltip_text(app.combo_run_mode, _("UI with embedded host (cosim), or backend only (--no-host)"));
    gtk_widget_set_tooltip_text(app.check_cosim_unix, _("Route cosim through a Unix socket with socat. Needs socat (conditional)"));
    gtk_widget_set_tooltip_text(app.entry_cosim_unix, _("Unix socket path for the socat cosim bridge"));
    gtk_editable_set_editable(GTK_EDITABLE(app.entry_uart), FALSE);
    gtk_widget_set_tooltip_text(app.entry_uart, _("UART console port, derived from the instance (read-only)"));
    gtk_widget_set_tooltip_text(app.spin_instance, _("Emulator instance 0-99. Ports derive from it: cosim 5591+10N, UART 5555+10N, control 5556+10N, GDB 3333+10N"));
    gtk_widget_set_tooltip_text(app.entry_console_mode, _("Where the firmware console goes: tcp, stdio, pty or none"));
    gtk_widget_set_tooltip_text(app.entry_emu_path, _("Path to the esp-emu binary"));
    gtk_widget_set_tooltip_text(app.entry_emu_caps, _("esp-emu binary used by the capability probe"));
    gtk_widget_set_tooltip_text(btn_refresh_caps, _("Run esp-emu --help and --version again and update the capability states"));
    gtk_widget_set_tooltip_text(app.check_audio, _("Enable I2S audio through esp-emu. Output is a WAV file"));
    gtk_widget_set_tooltip_text(app.entry_audio_out, _("WAV file that receives the I2S output"));
    gtk_widget_set_tooltip_text(app.entry_audio_in, _("WAV file used as I2S input (optional)"));
    gtk_widget_set_tooltip_text(app.spin_audio_instance, _("I2S instance number for esp-emu. -1 keeps the default"));
    gtk_widget_set_tooltip_text(app.entry_audio_backend, _("Audio backend hint kept in the profile (default wav-file)"));
    gtk_widget_set_tooltip_text(app.check_net_user, _("Legacy switch for user-mode networking (--net user). Prefer Network enabled"));
    gtk_widget_set_tooltip_text(app.check_net_enabled, _("Enable networking with --net. Mode is set in Net mode"));
    gtk_widget_set_tooltip_text(app.entry_net_mode, _("Mode for --net. user is the built-in user-mode networking"));
    gtk_widget_set_tooltip_text(app.entry_hostfwd, _("Host port forwards separated by ;. Format proto:hostaddr:hostport-guestaddr:guestport"));
    gtk_widget_set_tooltip_text(app.entry_wifi_ssid, _("Network name of the emulated Wi-Fi AP. Test profiles use testnet"));
    gtk_widget_set_tooltip_text(app.entry_wifi_password, _("WPA passphrase, 8 to 63 characters. Ignored for open"));
    gtk_widget_set_tooltip_text(app.combo_wifi_auth, _("Security of the emulated AP: open, wpa2-psk, wpa3-sae or wpa2-wpa3"));
    gtk_widget_set_tooltip_text(app.entry_ble_hci, _("HCI transport for BLE, for example tcp:localhost:9544. Needs an HCI server there"));
    gtk_widget_set_tooltip_text(app.check_gdb_enabled, _("Start a GDB stub on the GDB port (--gdb). Conditional on esp-emu support"));
    gtk_widget_set_sensitive(app.spin_gdb_port, FALSE);
    gtk_widget_set_tooltip_text(app.spin_gdb_port, _("TCP port of the GDB stub, derived from the instance (read-only)"));
    gtk_widget_set_tooltip_text(app.check_gdb_halt, _("Wait for GDB before the firmware starts (--gdb-halt)"));
    gtk_widget_set_tooltip_text(app.check_trace_enabled, _("Write an esp-emu trace file (--trace). Conditional on esp-emu support"));
    gtk_widget_set_tooltip_text(app.entry_trace_path, _("Output file for the trace"));
    gtk_widget_set_tooltip_text(app.entry_littlefs_label, _("Partition label of the LittleFS image (default littlefs)"));
    gtk_widget_set_tooltip_text(app.entry_littlefs_mount, _("Mount path of the LittleFS image in the firmware"));
    gtk_widget_set_tooltip_text(app.entry_littlefs_offset, _("Flash offset of the LittleFS partition. Must match the partition table"));
    gtk_widget_set_tooltip_text(app.entry_littlefs_size, _("Size of the LittleFS partition. Must match the partition table"));
    gtk_widget_set_tooltip_text(app.check_sd_enabled, _("Legacy SD switch. Use Card menu to eject or replace the image"));
    gtk_widget_set_tooltip_text(app.entry_sd_image, _("Card image file (.img). Set it from Card menu > Replace image"));
    gtk_widget_set_tooltip_text(app.combo_sd_mode, _("SD bus mode: sdmmc (ESP32-S3), sdspi or none"));
    gtk_widget_set_tooltip_text(app.entry_sd_slot, _("SD slot index (legacy field kept in the profile)"));
    gtk_widget_set_tooltip_text(app.entry_sd_cs, _("Chip-select GPIO for SPI SD (legacy field kept in the profile)"));

    GtkWidget *tab_future = gtk_box_new(GTK_ORIENTATION_VERTICAL,8); gtk_container_set_border_width(GTK_CONTAINER(tab_future),8);
    gtk_notebook_append_page(GTK_NOTEBOOK(cfg_nb), scroll_wrap(tab_future), gtk_label_new(_("Merge / Run")));
    GtkWidget *t13 = gtk_frame_new(_("Merge wizard and profile import/export"));
    GtkWidget *t13b = gtk_box_new(GTK_ORIENTATION_VERTICAL,4); gtk_container_add(GTK_CONTAINER(t13), t13b);
    gtk_box_pack_start(GTK_BOX(t13b), gtk_label_new(_("Structure ready for the merge wizard (boot/part/app/fs, offsets, esptool dry-run) and profile import/export.")), FALSE,FALSE,4);
    GtkWidget *t13btn = gtk_button_new_with_label(_("Open merge wizard")); gtk_box_pack_start(GTK_BOX(t13b), t13btn, FALSE,FALSE,0); g_signal_connect(t13btn, "clicked", G_CALLBACK(on_merge_wizard), &app);
    gtk_widget_set_tooltip_text(t13btn, _("Merge wizard is not built yet. Use ./start.sh emu merge"));
    GtkWidget *t14 = gtk_frame_new(_("Run modes and advanced logs"));
    GtkWidget *t14b = gtk_box_new(GTK_ORIENTATION_VERTICAL,4); gtk_container_add(GTK_CONTAINER(t14), t14b);
    gtk_box_pack_start(GTK_BOX(t14b), gtk_label_new(_("Basic telemetry is in the Processes/Telemetry tabs. Run modes and advanced filters: planned.")), FALSE,FALSE,4);
    gtk_box_pack_start(GTK_BOX(t14b), gtk_label_new(_("Run mode and backend-only/--no-host are available in Runtime/Audio/Network. Log filters/export are available in the log panel.")), FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(tab_future), t13, FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(tab_future), t14, FALSE,FALSE,0);

    GtkWidget *tab_parts = gtk_box_new(GTK_ORIENTATION_VERTICAL,6); gtk_container_set_border_width(GTK_CONTAINER(tab_parts),8);
    gtk_notebook_append_page(GTK_NOTEBOOK(cfg_nb), scroll_wrap(tab_parts), gtk_label_new(_("Storage/Parts")));
    app.entry_parts_dir = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_parts_dir), "simulator/emu/parts");
    gtk_entry_set_text(GTK_ENTRY(app.entry_parts_dir), "simulator/emu/parts");
    GtkWidget *parts_dir_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_pack_start(GTK_BOX(parts_dir_row), gtk_label_new(_("Parts dir (.wasm):")), FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(parts_dir_row), app.entry_parts_dir, TRUE,TRUE,0);
    GtkWidget *btn_parts_scan = gtk_button_new_with_label(_("Scan"));
    GtkWidget *btn_parts_validate = gtk_button_new_with_label(_("Check conflicts"));
    gtk_box_pack_start(GTK_BOX(parts_dir_row), btn_parts_scan, FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(parts_dir_row), btn_parts_validate, FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(tab_parts), parts_dir_row, FALSE,FALSE,0);
    GtkWidget *parts_inst_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    app.entry_parts_instance_id = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_parts_instance_id), _("instance id"));
    app.entry_parts_part_name = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_parts_part_name), _("part name (empty=1st)"));
    gtk_box_pack_start(GTK_BOX(parts_inst_row), labeled(_("Instance ID"), app.entry_parts_instance_id), TRUE,TRUE,0);
    gtk_box_pack_start(GTK_BOX(parts_inst_row), labeled(_("Part name"), app.entry_parts_part_name), TRUE,TRUE,0);
    GtkWidget *btn_parts_add = gtk_button_new_with_label(_("Add/Config"));
    GtkWidget *btn_parts_remove = gtk_button_new_with_label(_("Remove"));
    gtk_box_pack_start(GTK_BOX(parts_inst_row), btn_parts_add, FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(parts_inst_row), btn_parts_remove, FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(tab_parts), parts_inst_row, FALSE,FALSE,0);
    GtkWidget *parts_note = gtk_label_new(_("Parts WASM/sidecar: discovery, claims/wiring, conflicts and capabilities. Status: conditional/needs-runtime-host (no real cosim runtime integration; hooks/placeholders)."));
    gtk_label_set_xalign(GTK_LABEL(parts_note), 0.0); gtk_label_set_line_wrap(GTK_LABEL(parts_note), TRUE);
    gtk_box_pack_start(GTK_BOX(tab_parts), parts_note, FALSE,FALSE,0);
    GtkWidget *parts_split = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    GtkWidget *parts_list_sc = gtk_scrolled_window_new(NULL,NULL);
    GtkWidget *parts_list_tv = gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(parts_list_tv),FALSE); gtk_text_view_set_monospace(GTK_TEXT_VIEW(parts_list_tv),TRUE); gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(parts_list_tv),GTK_WRAP_WORD_CHAR);
    app.parts_list_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(parts_list_tv)); gtk_container_add(GTK_CONTAINER(parts_list_sc), parts_list_tv);
    GtkWidget *parts_rep_sc = gtk_scrolled_window_new(NULL,NULL);
    GtkWidget *parts_rep_tv = gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(parts_rep_tv),FALSE); gtk_text_view_set_monospace(GTK_TEXT_VIEW(parts_rep_tv),TRUE); gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(parts_rep_tv),GTK_WRAP_WORD_CHAR);
    app.parts_report_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(parts_rep_tv)); gtk_container_add(GTK_CONTAINER(parts_rep_sc), parts_rep_tv);
    gtk_paned_pack1(GTK_PANED(parts_split), parts_list_sc, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(parts_split), parts_rep_sc, FALSE, FALSE);
    gtk_widget_set_vexpand(parts_split, TRUE);
    gtk_box_pack_start(GTK_BOX(tab_parts), parts_split, TRUE, TRUE, 0);
    g_signal_connect(btn_parts_scan, "clicked", G_CALLBACK(on_parts_scan), &app);
    g_signal_connect(btn_parts_add, "clicked", G_CALLBACK(on_parts_add_instance), &app);
    g_signal_connect(btn_parts_remove, "clicked", G_CALLBACK(on_parts_remove_instance), &app);
    g_signal_connect(btn_parts_validate, "clicked", G_CALLBACK(on_parts_validate), &app);
    gtk_widget_set_tooltip_text(app.entry_parts_dir, _("Folder with .wasm parts. Scan reads this folder"));
    gtk_widget_set_tooltip_text(btn_parts_scan, _("Look for .wasm parts in the parts folder"));
    gtk_widget_set_tooltip_text(btn_parts_validate, _("Check the parts list for claim and wiring conflicts"));
    gtk_widget_set_tooltip_text(app.entry_parts_instance_id, _("Id of this part instance in the parts list"));
    gtk_widget_set_tooltip_text(app.entry_parts_part_name, _("Name of the part to add"));
    gtk_widget_set_tooltip_text(btn_parts_add, _("Add the part instance to the list, or update its settings"));
    gtk_widget_set_tooltip_text(btn_parts_remove, _("Remove the selected part instance from the list"));

    /* One notebook merges the 6 tool tabs and the 3 log tabs (packed into the bottom dock further below). */
    GtkWidget *left_bottom = gtk_notebook_new(); gtk_notebook_set_scrollable(GTK_NOTEBOOK(left_bottom), TRUE);
    app.notebook_bottom = left_bottom;
    GtkWidget *val_sc = gtk_scrolled_window_new(NULL,NULL); gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(val_sc),40);
    GtkWidget *val_tv = gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(val_tv),FALSE); gtk_text_view_set_monospace(GTK_TEXT_VIEW(val_tv),TRUE); gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(val_tv),GTK_WRAP_WORD_CHAR);
    app.validation_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(val_tv)); gtk_container_add(GTK_CONTAINER(val_sc), val_tv);
    gtk_notebook_append_page(GTK_NOTEBOOK(left_bottom), val_sc, gtk_label_new(_("Validation/Capabilities"))); app.dock_pages_tools[0] = val_sc;
    GtkWidget *cmd_sc = gtk_scrolled_window_new(NULL,NULL); gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(cmd_sc),40);
    GtkWidget *cmd_tv = gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(cmd_tv),FALSE); gtk_text_view_set_monospace(GTK_TEXT_VIEW(cmd_tv),TRUE); gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(cmd_tv),GTK_WRAP_CHAR);
    app.cmd_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(cmd_tv)); gtk_container_add(GTK_CONTAINER(cmd_sc), cmd_tv);
    gtk_notebook_append_page(GTK_NOTEBOOK(left_bottom), cmd_sc, gtk_label_new(_("Effective command"))); app.dock_pages_tools[1] = cmd_sc;
    GtkWidget *telem_sc = gtk_scrolled_window_new(NULL,NULL); gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(telem_sc),40);
    GtkWidget *telem_tv = gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(telem_tv),FALSE); gtk_text_view_set_monospace(GTK_TEXT_VIEW(telem_tv),TRUE); gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(telem_tv),GTK_WRAP_WORD_CHAR);
    app.telemetry_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(telem_tv)); gtk_container_add(GTK_CONTAINER(telem_sc), telem_tv);
    gtk_notebook_append_page(GTK_NOTEBOOK(left_bottom), telem_sc, gtk_label_new(_("Processes/Telemetry"))); app.dock_pages_tools[2] = telem_sc;
    GtkWidget *dbg_sc = gtk_scrolled_window_new(NULL,NULL);
    GtkWidget *dbg_tv = gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(dbg_tv),FALSE); gtk_text_view_set_monospace(GTK_TEXT_VIEW(dbg_tv),TRUE);
    app.debug_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(dbg_tv)); gtk_container_add(GTK_CONTAINER(dbg_sc), dbg_tv);
    gtk_notebook_append_page(GTK_NOTEBOOK(left_bottom), dbg_sc, gtk_label_new(_("Debug/GDB/Trace"))); app.dock_pages_tools[3] = dbg_sc;
    GtkWidget *gpio_sc = gtk_scrolled_window_new(NULL,NULL);
    GtkWidget *gpio_tv = gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(gpio_tv),FALSE); gtk_text_view_set_monospace(GTK_TEXT_VIEW(gpio_tv),TRUE);
    app.gpio_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gpio_tv)); gtk_container_add(GTK_CONTAINER(gpio_sc), gpio_tv);
    gtk_notebook_append_page(GTK_NOTEBOOK(left_bottom), gpio_sc, gtk_label_new(_("GPIO/Pins (state)"))); app.dock_pages_tools[4] = gpio_sc; app.gpio_page = gpio_sc;
    GtkWidget *wf_box = gtk_box_new(GTK_ORIENTATION_VERTICAL,4); gtk_container_set_border_width(GTK_CONTAINER(wf_box),6);
    GtkWidget *wf_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    app.entry_build_cmd = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(app.entry_build_cmd), "./start.sh emu build && ./start.sh emu merge");
    app.entry_workflow_file = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_workflow_file), _("file to open/edit (optional)"));
    GtkWidget *btn_wf_open = gtk_button_new_with_label(_("Open"));
    GtkWidget *btn_wf_save = gtk_button_new_with_label(_("Save"));
    GtkWidget *btn_wf_build = gtk_button_new_with_label(_("Build (start.sh)"));
    gtk_box_pack_start(GTK_BOX(wf_row), gtk_label_new(_("Build cmd:")), FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(wf_row), app.entry_build_cmd, TRUE,TRUE,0);
    gtk_box_pack_start(GTK_BOX(wf_row), btn_wf_build, FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(wf_box), wf_row, FALSE,FALSE,0);
    GtkWidget *wf_row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_pack_start(GTK_BOX(wf_row2), gtk_label_new(_("File:")), FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(wf_row2), app.entry_workflow_file, TRUE,TRUE,0);
    gtk_box_pack_start(GTK_BOX(wf_row2), btn_wf_open, FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(wf_row2), btn_wf_save, FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(wf_box), wf_row2, FALSE,FALSE,0);
    GtkWidget *wf_sc = gtk_scrolled_window_new(NULL,NULL); gtk_widget_set_vexpand(wf_sc,TRUE);
    GtkWidget *wf_tv = gtk_text_view_new(); gtk_text_view_set_monospace(GTK_TEXT_VIEW(wf_tv),TRUE);
    app.workflow_edit_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(wf_tv)); gtk_container_add(GTK_CONTAINER(wf_sc), wf_tv);
    gtk_box_pack_start(GTK_BOX(wf_box), wf_sc, TRUE,TRUE,0);
    GtkWidget *wf_status_sc = gtk_scrolled_window_new(NULL,NULL); gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(wf_status_sc),40);
    GtkWidget *wf_status_tv = gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(wf_status_tv),FALSE); gtk_text_view_set_monospace(GTK_TEXT_VIEW(wf_status_tv),TRUE);
    app.workflow_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(wf_status_tv)); gtk_container_add(GTK_CONTAINER(wf_status_sc), wf_status_tv);
    gtk_box_pack_start(GTK_BOX(wf_box), wf_status_sc, FALSE,TRUE,0);
    gtk_notebook_append_page(GTK_NOTEBOOK(left_bottom), wf_box, gtk_label_new(_("Code/Build/Run"))); app.dock_pages_tools[5] = wf_box;
    g_signal_connect(btn_wf_open, "clicked", G_CALLBACK(on_workflow_open_file), &app);
    g_signal_connect(btn_wf_save, "clicked", G_CALLBACK(on_workflow_save_file), &app);
    g_signal_connect(btn_wf_build, "clicked", G_CALLBACK(on_workflow_build), &app);
    gtk_widget_set_tooltip_text(btn_wf_open, _("Open a file in the workflow editor"));
    gtk_widget_set_tooltip_text(btn_wf_save, _("Save the workflow file. This does not save the profile"));
    gtk_widget_set_tooltip_text(btn_wf_build, _("Run the build command shown in Build cmd"));

    GtkWidget *right = gtk_paned_new(GTK_ORIENTATION_VERTICAL); gtk_paned_set_wide_handle(GTK_PANED(right), TRUE); gtk_paned_pack2(GTK_PANED(paned), right, TRUE, TRUE);
    app.paned_right = right;
    { GtkWidget *cs[] = { app.combo_board, app.combo_chip, app.combo_firmware, app.combo_run_mode, app.combo_sd_mode, app.combo_wifi_auth }; for (size_t i = 0; i < sizeof(cs)/sizeof(cs[0]); i++) combo_ellipsize(cs[i]); }
    app.canvas_label = gtk_label_new(_("Canvas: ui+embedded host (sim_host) | detached")); gtk_label_set_xalign(GTK_LABEL(app.canvas_label),0.0);
    /* canvas: takes all spare space, no fixed size request (so the dividers can be dragged freely) */
    app.canvas = gtk_drawing_area_new(); gtk_widget_set_hexpand(app.canvas,TRUE); gtk_widget_set_vexpand(app.canvas,TRUE);
    gtk_widget_add_events(app.canvas, GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_POINTER_MOTION_MASK|GDK_STRUCTURE_MASK);
    g_signal_connect(app.canvas,"button-press-event",G_CALLBACK(on_canvas_button),&app);
    g_signal_connect(app.canvas,"button-release-event",G_CALLBACK(on_canvas_button),&app);
    g_signal_connect(app.canvas,"motion-notify-event",G_CALLBACK(on_canvas_motion),&app);
    g_signal_connect(app.canvas,"configure-event",G_CALLBACK(on_canvas_configure),&app);
    g_signal_connect(app.canvas,"draw",G_CALLBACK(on_canvas_draw),&app);
    g_signal_connect_after(app.canvas,"size-allocate",G_CALLBACK(on_canvas_size_allocate),&app);
    /* overlay: canvas below, configuration summary card + hint above (hidden while the host is attached) */
    GtkWidget *ov = gtk_overlay_new(); gtk_style_context_add_class(gtk_widget_get_style_context(ov), "canvas-wrap");
    gtk_container_add(GTK_CONTAINER(ov), app.canvas);
    {
        GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL,8); gtk_style_context_add_class(gtk_widget_get_style_context(card), "card");
        app.lbl_sum_title = gtk_label_new(_("Ready to start")); gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_sum_title), "card-title"); gtk_widget_set_halign(app.lbl_sum_title, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(card), app.lbl_sum_title, FALSE, FALSE, 0);
        GtkWidget *sub = gtk_label_new(_("Effective configuration of the active profile")); gtk_style_context_add_class(gtk_widget_get_style_context(sub), "card-sub"); gtk_widget_set_halign(sub, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(card), sub, FALSE, FALSE, 0);
        GtkWidget *g = gtk_grid_new(); gtk_grid_set_row_spacing(GTK_GRID(g), 4); gtk_grid_set_column_spacing(GTK_GRID(g), 12); gtk_widget_set_margin_top(g, 8);
        for (int i = 0; i < SUM_ROWS; i++) {
            GtkWidget *k = gtk_label_new(_(k_sum_keys[i])); gtk_style_context_add_class(gtk_widget_get_style_context(k), "card-key"); gtk_label_set_xalign(GTK_LABEL(k), 0.0);
            app.lbl_sum_val[i] = gtk_label_new(""); gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_sum_val[i]), "card-val");
            gtk_label_set_xalign(GTK_LABEL(app.lbl_sum_val[i]), 0.0); gtk_label_set_ellipsize(GTK_LABEL(app.lbl_sum_val[i]), PANGO_ELLIPSIZE_MIDDLE); gtk_label_set_max_width_chars(GTK_LABEL(app.lbl_sum_val[i]), 56);
            gtk_label_set_selectable(GTK_LABEL(app.lbl_sum_val[i]), FALSE);
            gtk_grid_attach(GTK_GRID(g), k, 0, i, 1, 1); gtk_grid_attach(GTK_GRID(g), app.lbl_sum_val[i], 1, i, 1, 1);
        }
        gtk_box_pack_start(GTK_BOX(card), g, FALSE, FALSE, 0);
        /* card + hint live in a scrolled window so a short canvas scrolls them instead of clipping */
        app.w_summary_hint = gtk_label_new(_("Simulator → Start (Ctrl+R)")); gtk_style_context_add_class(gtk_widget_get_style_context(app.w_summary_hint), "hint");
        gtk_widget_set_halign(app.w_summary_hint, GTK_ALIGN_CENTER); gtk_widget_set_margin_top(app.w_summary_hint, 8);
        GtkWidget *wrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_widget_set_valign(wrap, GTK_ALIGN_CENTER); gtk_widget_set_halign(wrap, GTK_ALIGN_CENTER);
        gtk_widget_set_margin_start(wrap, 16); gtk_widget_set_margin_end(wrap, 16); gtk_widget_set_margin_top(wrap, 16); gtk_widget_set_margin_bottom(wrap, 16);
        gtk_box_pack_start(GTK_BOX(wrap), card, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(wrap), app.w_summary_hint, FALSE, FALSE, 0);
        GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_style_context_add_class(gtk_widget_get_style_context(sw), "card-scroll");
        gtk_container_add(GTK_CONTAINER(sw), wrap);
        app.w_summary = sw;
        gtk_overlay_add_overlay(GTK_OVERLAY(ov), sw);
    }
    gtk_paned_pack1(GTK_PANED(right), ov, TRUE, TRUE);
    /* bottom dock: log filter row + one notebook (6 tool tabs + esp-emu log / host/cosim log / Info) */
    GtkWidget *log_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL,4);
    app.w_dock = log_panel;
    gtk_paned_pack2(GTK_PANED(right), log_panel, TRUE, TRUE);
    GtkWidget *log_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    app.w_log_bar = log_bar;
    app.entry_log_filter = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_log_filter), _("text filter (substring)")); gtk_entry_set_width_chars(GTK_ENTRY(app.entry_log_filter), 6);
    app.combo_log_level = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_log_level), "All levels");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_log_level), "UART");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_log_level), "INFO");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_log_level), "WARN");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_log_level), "ERROR");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_log_level), "HOST");
    gtk_combo_box_set_active(GTK_COMBO_BOX(app.combo_log_level), 0);
    app.combo_log_origin = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_log_origin), "All origins");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_log_origin), "EMU");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_log_origin), "HOST");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_log_origin), "LAUNCHER");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.combo_log_origin), "VALIDATION");
    gtk_combo_box_set_active(GTK_COMBO_BOX(app.combo_log_origin), 0);
    app.check_log_follow = gtk_check_button_new_with_label(_("Follow")); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.check_log_follow), TRUE);
    GtkWidget *btn_log_clear = gtk_button_new_with_label(_("Clear"));
    GtkWidget *btn_log_export = gtk_button_new_with_label(_("Export..."));
    gtk_box_pack_start(GTK_BOX(log_bar), gtk_label_new(_("Logs:")), FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(log_bar), app.entry_log_filter, TRUE,TRUE,0);
    gtk_box_pack_start(GTK_BOX(log_bar), app.combo_log_level, FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(log_bar), app.combo_log_origin, FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(log_bar), app.check_log_follow, FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(log_bar), btn_log_clear, FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(log_bar), btn_log_export, FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(log_panel), log_bar, FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(log_panel), left_bottom, TRUE, TRUE, 0);
    GtkWidget *emu_log = make_log_view(&app.emu_log_buf); GtkWidget *host_log = make_log_view(&app.host_log_buf);
    gtk_notebook_append_page(GTK_NOTEBOOK(left_bottom), emu_log, gtk_label_new(_("esp-emu log"))); app.dock_pages_logs[0] = emu_log;
    gtk_notebook_append_page(GTK_NOTEBOOK(left_bottom), host_log, gtk_label_new(_("host/cosim log"))); app.dock_pages_logs[1] = host_log;
    /* Info tab: the status text that used to sit above the canvas/config (canvas, profile/paths) plus the old note */
    GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL,4); gtk_container_set_border_width(GTK_CONTAINER(info_box),6);
    gtk_box_pack_start(GTK_BOX(info_box), app.canvas_label, FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(info_box), app.path_label, FALSE,FALSE,0);
    GtkWidget *info_note = gtk_label_new(_("Telemetry is integrated in 'Processes/Telemetry'. Filters/export apply to the unified buffer (console_log).")); gtk_label_set_xalign(GTK_LABEL(info_note),0.0);
    gtk_label_set_ellipsize(GTK_LABEL(info_note), PANGO_ELLIPSIZE_END); gtk_label_set_ellipsize(GTK_LABEL(app.canvas_label), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(info_box), info_note, FALSE,FALSE,0);
    gtk_notebook_append_page(GTK_NOTEBOOK(left_bottom), info_box, gtk_label_new(_("Info"))); app.dock_pages_logs[2] = info_box;
    g_signal_connect(app.entry_log_filter, "changed", G_CALLBACK(on_log_filter_changed), &app);
    g_signal_connect(app.combo_log_level, "changed", G_CALLBACK(on_log_filter_changed), &app);
    g_signal_connect(app.combo_log_origin, "changed", G_CALLBACK(on_log_filter_changed), &app);
    g_signal_connect(btn_log_clear, "clicked", G_CALLBACK(on_log_clear), &app);
    g_signal_connect(btn_log_export, "clicked", G_CALLBACK(on_log_export), &app);
    g_signal_connect(app.check_log_follow, "toggled", G_CALLBACK(on_log_follow_toggled), &app);
    gtk_widget_set_tooltip_text(app.entry_log_filter, _("Show only log lines that contain this text"));
    gtk_widget_set_tooltip_text(app.combo_log_level, _("Show only lines of this level. All levels means no filter"));
    gtk_widget_set_tooltip_text(app.combo_log_origin, _("Show only lines from this source (esp-emu, host, launcher, validation)"));
    gtk_widget_set_tooltip_text(app.check_log_follow, _("Keep the log scrolled to the newest line"));
    gtk_widget_set_tooltip_text(btn_log_clear, _("Clear the visible log tab"));
    gtk_widget_set_tooltip_text(btn_log_export, _("Save the visible log lines to a text file"));

    add_tooltip(app.check_use_elf, _("Release without ELF: use --no-elf; reduces symbols/BLE intercepts/hooks/debug (conditional)."));
    add_tooltip(app.check_wifi, _("Wi-Fi: conditional(version/tool/fw). Visible in the UI; the full flow depends on support/args/firmware."));
    add_tooltip(app.check_ble, _("BLE: conditional(version/tool). Usually needs external ELF/HCI; not guaranteed."));
    add_tooltip(app.check_audio_realtime, _("Realtime audio: conditional(version/tool). Prefer I2S WAV from file when applicable."));
    add_tooltip(app.check_littlefs_enabled, _("LittleFS conditional (image/layout). SD: Card menu (esp-emu 1.1.0 --sd)."));
    add_tooltip(app.entry_littlefs, _("Optional LittleFS image. SD: Card menu (esp-emu 1.1.0 --sd)."));
    add_tooltip(app.spin_lcd_w, _("Pins/resolution/panel largely compiled (panel_config.h/lcd_touch_host.c). Partial runtime override."));
    add_tooltip(app.spin_lcd_h, _("Pins/resolution/panel largely compiled (panel_config.h/lcd_touch_host.c). Partial runtime override."));
    add_tooltip(app.entry_cosim_board, _("Cosim: --cosim-board in-process preferred; TCP/unix/bridge conditional on version/host."));
    add_tooltip(app.entry_extra, _("Extra args passed to esp-emu (unknown flags may fail)."));

    /* Menu bar (the former toolbar buttons live here; handlers unchanged) */
    app.cfg.ui_view_status = TRUE; /* the status bar carries the dock toggle: always visible at start-up */
    GtkWidget *m_prof = menu_top(menubar, _("_Profile"));
    menu_add(m_prof, _("New"), G_CALLBACK(on_profile_new), &app, _("Create a profile with default values. Nothing is saved until Save"));
    menu_add(m_prof, _("Open…"), G_CALLBACK(on_profile_open), &app, _("Load a profile JSON file and make it the active profile"));
    menu_add(m_prof, _("Save"), G_CALLBACK(on_profile_save), &app, _("Write the active profile file"));
    menu_add(m_prof, _("Save as…"), G_CALLBACK(on_profile_save_as), &app, _("Write the profile to a new file and make it the active profile"));
    menu_add(m_prof, _("Duplicate"), G_CALLBACK(on_profile_duplicate), &app, _("Copy the active profile to a new file and make the copy active"));
    menu_add(m_prof, _("Import conf"), G_CALLBACK(on_profile_import_conf), &app, _("Import a legacy launcher.conf into the editor"));
    menu_add(m_prof, _("Export"), G_CALLBACK(on_profile_export), &app, _("Write a copy of the profile to a file. The active profile does not change"));
    gtk_menu_shell_append(GTK_MENU_SHELL(m_prof), gtk_separator_menu_item_new());
    GtkWidget *mi_quit = menu_add(m_prof, _("Quit"), G_CALLBACK(on_quit), &app, _("Close the launcher. Stops the emulator. Unsaved profile changes ask first (Ctrl+Q)"));
    gtk_widget_add_accelerator(mi_quit, "activate", accel, GDK_KEY_q, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    GtkWidget *m_sim = menu_top(menubar, _("_Simulator"));
    app.mi_start = menu_add(m_sim, _("Start"), G_CALLBACK(on_start), &app, _("Start the emulator with the active profile (Ctrl+R)"));
    app.mi_stop = menu_add(m_sim, _("Stop"), G_CALLBACK(on_stop), &app, _("Stop the emulator and the host (Ctrl+.)"));
    GtkWidget *mi_restart = menu_add(m_sim, _("Restart"), G_CALLBACK(on_restart), &app, _("Stop and start the emulator again (Ctrl+Shift+R)"));
    gtk_widget_add_accelerator(app.mi_start, "activate", accel, GDK_KEY_r, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(app.mi_stop, "activate", accel, GDK_KEY_period, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(mi_restart, "activate", accel, GDK_KEY_r, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
    gtk_menu_shell_append(GTK_MENU_SHELL(m_sim), gtk_separator_menu_item_new());
    menu_add(m_sim, _("Validate"), G_CALLBACK(on_validate), &app, _("Run validation again and show the results in the Validation tab"));
    menu_add(m_sim, _("Copy command"), G_CALLBACK(on_copy_command), &app, _("Copy the effective esp-emu command line to the clipboard"));
    GtkWidget *m_card = menu_top(menubar, _("_Card"));
    menu_add(m_card, _("Replace image… (restarts the emulator)"), G_CALLBACK(on_card_replace), &app,
             _("esp-emu 1.1.0 cannot swap the card while running: if the emulator is running, it is restarted in a controlled way."));
    app.mi_eject = menu_add(m_card, _("Eject (restarts the emulator)"), G_CALLBACK(on_card_eject), &app,
             _("Starts without the card (--no-sd). esp-emu 1.1.0 does not remove the card while running: controlled restart."));
    gtk_menu_shell_append(GTK_MENU_SHELL(m_card), gtk_separator_menu_item_new());
    menu_add(m_card, _("Build image from folder…"), G_CALLBACK(on_card_build), &app,
             _("./start.sh card image --force <folder> <image>: REPLACES the image (what the firmware wrote to the card is lost)."));
    menu_add(m_card, _("Extract image to folder…"), G_CALLBACK(on_card_extract), &app, "./start.sh card extract --force <image> <folder>");
    menu_add(m_card, _("Open card folder"), G_CALLBACK(on_card_open_folder), &app, "xdg-open simulator/SD_Card");
    GtkWidget *m_view = menu_top(menubar, _("_View"));
    app.mi_view_cfg = menu_check(m_view, _("Configuration panel"), app.cfg.ui_view_cfg, G_CALLBACK(on_view_cfg_toggled), &app);
    app.mi_view_bottom = menu_check(m_view, _("Bottom panel"), app.cfg.ui_view_bottom, G_CALLBACK(on_view_dock_toggled), &app);
    app.mi_view_status = menu_check(m_view, _("Status bar"), app.cfg.ui_view_status, G_CALLBACK(on_view_status_toggled), &app);
    app.mi_view_logs = menu_check(m_view, _("Logs"), app.cfg.ui_view_logs, G_CALLBACK(on_view_dock_toggled), &app);
    gtk_widget_set_tooltip_text(app.mi_view_cfg, _("Show or hide the drawer with the configuration pages"));
    gtk_widget_set_tooltip_text(app.mi_view_bottom, _("Show or hide the bottom dock"));
    gtk_widget_set_tooltip_text(app.mi_view_status, _("Show or hide the status bar"));
    gtk_widget_set_tooltip_text(app.mi_view_logs, _("Show or hide the log tabs of the bottom dock"));
    menu_add(m_view, _("GPIO/Pins window"), G_CALLBACK(on_view_gpio), &app, _("Shows the bottom panel on the GPIO/Pins tab"));
    /* View > Language: radio items; names stay in their own language, "Automatic" is translated. */
    GtkWidget *mi_lang = gtk_menu_item_new_with_label(_("Language"));
    gtk_widget_set_tooltip_text(mi_lang, _("Interface language. The launcher must be restarted to apply it"));
    GtkWidget *m_lang = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_lang), m_lang);
    gtk_menu_shell_append(GTK_MENU_SHELL(m_view), mi_lang);
    static const struct { const char *code; const char *name; } k_ui_langs[] = {
        { "auto", NULL }, { "en", "English" }, { "pt", "Português" },
    };
    GSList *lang_group = NULL;
    for (size_t i = 0; i < sizeof(k_ui_langs) / sizeof(k_ui_langs[0]); i++) {
        const char *code = k_ui_langs[i].code;
        GtkWidget *it = gtk_radio_menu_item_new_with_label(lang_group, k_ui_langs[i].name ? k_ui_langs[i].name : _("Automatic"));
        lang_group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(it));
        g_object_set_data(G_OBJECT(it), "ui-lang", (gpointer)code);
        gboolean on = strcmp(app.cfg.ui_lang, code) == 0 ||
                      (strcmp(code, "auto") == 0 && strcmp(app.cfg.ui_lang, "en") != 0 && strcmp(app.cfg.ui_lang, "pt") != 0);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), on);
        g_signal_connect(it, "toggled", G_CALLBACK(on_ui_lang_selected), &app);
        gtk_menu_shell_append(GTK_MENU_SHELL(m_lang), it);
    }
    GtkWidget *m_help = menu_top(menubar, _("_Help"));
    GtkWidget *mi_help_contents = menu_add(m_help, _("Help contents"), G_CALLBACK(on_help_contents), &app, _("Open the launcher help file"));
    gtk_widget_add_accelerator(mi_help_contents, "activate", accel, GDK_KEY_F1, 0, GTK_ACCEL_VISIBLE);
    menu_add(m_help, _("Keyboard shortcuts"), G_CALLBACK(on_help_shortcuts), &app, _("List the keyboard shortcuts"));
    gtk_menu_shell_append(GTK_MENU_SHELL(m_help), gtk_separator_menu_item_new());
    menu_add(m_help, _("About"), G_CALLBACK(on_help_about), &app, _("About this launcher"));
    /* Status bar: capability badges line (single line, full text in the tooltip) */
    GtkWidget *statusbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12); gtk_style_context_add_class(gtk_widget_get_style_context(statusbar), "statusbar");
    app.btn_dock = gtk_toggle_button_new_with_label(_("▤ Console / Logs")); gtk_style_context_add_class(gtk_widget_get_style_context(app.btn_dock), "flat-btn");
    gtk_widget_set_tooltip_text(app.btn_dock, _("Show/hide the bottom panel (validation, command, telemetry, logs)"));
    g_signal_connect(app.btn_dock, "toggled", G_CALLBACK(on_dock_btn_toggled), &app);
    gtk_box_pack_start(GTK_BOX(statusbar), app.btn_dock, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(statusbar), app.status_label, TRUE, TRUE, 0);
    gtk_label_set_max_width_chars(GTK_LABEL(app.caps_label), 60);
    gtk_box_pack_end(GTK_BOX(statusbar), app.caps_label, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(root), statusbar, FALSE, FALSE, 0);
    app.w_statusbar = statusbar;
    g_signal_connect_swapped(app.btn_drawer_close, "clicked", G_CALLBACK(gtk_menu_item_activate), app.mi_view_cfg);
    g_signal_connect(app.btn_start,"clicked",G_CALLBACK(on_start),&app);
    g_signal_connect(app.btn_stop,"clicked",G_CALLBACK(on_stop),&app);
    g_signal_connect(app.btn_restart,"clicked",G_CALLBACK(on_restart),&app);
    g_signal_connect(app.combo_board,"changed",G_CALLBACK(on_board_changed),&app);
    if (app.combo_chip) g_signal_connect(app.combo_chip,"changed",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.combo_firmware,"changed",G_CALLBACK(on_firmware_changed),&app);
    g_signal_connect(app.entry_profile_path,"changed",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.entry_merged,"changed",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.entry_elf,"changed",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.check_use_elf,"toggled",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.entry_custom,"changed",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.entry_cosim_board,"changed",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.spin_lcd_w,"value-changed",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.spin_lcd_h,"value-changed",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.spin_scale,"value-changed",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.spin_rot,"value-changed",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.spin_instance,"value-changed",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.entry_console_mode,"changed",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.entry_extra,"changed",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.entry_emu_path,"changed",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.check_audio,"toggled",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.check_audio_realtime,"toggled",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.entry_audio_out,"changed",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.entry_audio_in,"changed",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.check_net_user,"toggled",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.check_wifi,"toggled",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.check_ble,"toggled",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.check_littlefs_enabled,"toggled",G_CALLBACK(on_cfg_changed),&app);
    g_signal_connect(app.entry_littlefs,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.combo_run_mode) g_signal_connect(app.combo_run_mode,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.check_cosim_unix) g_signal_connect(app.check_cosim_unix,"toggled",G_CALLBACK(on_cfg_changed),&app);
    if (app.entry_cosim_unix) g_signal_connect(app.entry_cosim_unix,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.entry_emu_caps) g_signal_connect(app.entry_emu_caps,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.entry_littlefs_label) g_signal_connect(app.entry_littlefs_label,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.entry_littlefs_mount) g_signal_connect(app.entry_littlefs_mount,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.entry_littlefs_offset) g_signal_connect(app.entry_littlefs_offset,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.entry_littlefs_size) g_signal_connect(app.entry_littlefs_size,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.check_sd_enabled) g_signal_connect(app.check_sd_enabled,"toggled",G_CALLBACK(on_cfg_changed),&app);
    if (app.entry_sd_image) g_signal_connect(app.entry_sd_image,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.combo_sd_mode) g_signal_connect(app.combo_sd_mode,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.entry_sd_slot) g_signal_connect(app.entry_sd_slot,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.entry_sd_cs) g_signal_connect(app.entry_sd_cs,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.spin_audio_instance) g_signal_connect(app.spin_audio_instance,"value-changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.entry_audio_backend) g_signal_connect(app.entry_audio_backend,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.entry_net_mode) g_signal_connect(app.entry_net_mode,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.entry_hostfwd) g_signal_connect(app.entry_hostfwd,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.entry_wifi_ssid) g_signal_connect(app.entry_wifi_ssid,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.entry_wifi_password) g_signal_connect(app.entry_wifi_password,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.combo_wifi_auth) g_signal_connect(app.combo_wifi_auth,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.entry_ble_hci) g_signal_connect(app.entry_ble_hci,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.check_gdb_enabled) g_signal_connect(app.check_gdb_enabled,"toggled",G_CALLBACK(on_cfg_changed),&app);
    if (app.check_gdb_halt) g_signal_connect(app.check_gdb_halt,"toggled",G_CALLBACK(on_cfg_changed),&app);
    if (app.check_trace_enabled) g_signal_connect(app.check_trace_enabled,"toggled",G_CALLBACK(on_cfg_changed),&app);
    if (app.entry_trace_path) g_signal_connect(app.entry_trace_path,"changed",G_CALLBACK(on_cfg_changed),&app);
    if (app.notebook_cfg) g_signal_connect(app.notebook_cfg, "switch-page", G_CALLBACK(on_notebook_switch), &app);
    if (app.notebook_bottom) g_signal_connect(app.notebook_bottom, "switch-page", G_CALLBACK(on_notebook_switch), &app);
    if (app.notebook_logs && app.notebook_logs != app.notebook_bottom) g_signal_connect(app.notebook_logs, "switch-page", G_CALLBACK(on_notebook_switch), &app);


    sync_ui_from_state(&app); app.cfg.run_mode = app.run_mode; update_panels(&app); render_validation(&app,false);
    render_parts_ui(&app); refresh_parts_runtime_hints(&app); render_debug_gpio_panels(&app);
    refresh_telemetry(&app);
    apply_log_filters(&app);
    append_launcher_log(&app, "Unified frontend: config/profile + embedded canvas (sim_host) + esp-emu subprocess.");
    append_launcher_log(&app, _("Merge wizard, run modes (UI and backend-only), log filters and export, and the capability, probe, storage, audio and network UI are integrated (partial where conditional or blocked)."));
    append_launcher_log(&app, _("Telemetry and parts are integrated in the UI with state and capabilities; deep runtime data and WASM execution depend on the host, the emulator and tools (needs runtime host, conditional)."));
    append_host_log(&app, "Host canvas attaches on Start. Cosim: TCP direto ou Unix socket via ponte socat (conditional tool; esp-emu usa --cosim-tcp internamente).");
    append_host_log(&app, _("Limits: SD via --sd (esp-emu 1.1.0, Card menu), release without ELF conditional, pins/resolution partial runtime/compiled."));
    set_dirty(&app, FALSE);
    update_status(&app);
    app.telemetry_tick_id = g_timeout_add(500, telemetry_tick_cb, &app);

    gtk_widget_show_all(app.window);
    /* panels start hidden (canvas dominant); the Ver menu state remembered in launcher.conf is applied here */
    gtk_widget_hide(app.w_cfg_panel); gtk_widget_hide(app.w_dock);
    if (!app.cfg.ui_view_status) gtk_widget_hide(app.w_statusbar);
    view_apply_dock(&app);
    if (app.cfg.ui_view_cfg) gtk_widget_show(app.w_cfg_panel);
    nav_sync(&app);
    g_timeout_add(200, view_init_cb, &app);
    restore_ui_layout(&app); set_dirty(&app, FALSE); update_status(&app); gtk_main();
    if (app.telemetry_tick_id) { g_source_remove(app.telemetry_tick_id); app.telemetry_tick_id = 0; }
    telem_free(&app.telem);
    return 0;
}
