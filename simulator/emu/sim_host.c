#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "sim_host.h"
#include "panel_config.h"

/*
 * Runtime-configurable sim host.
 * - LCD native W/H, scale, hw rotation, mirror, touch mapping/calibration are runtime (SimHostConfig/setters).
 * - Defaults come from panel_config.h (es3c28p-compatible) unless overridden.
 * - Framebuffer cap remains compile-time (LCD_MAX_W/H) to keep fixed storage/simple protocol assumptions.
 * - SPI/I2C bus pins, GPIO watch/drive pins, touch I2C address and controller behavior remain compile-time
 *   from panel_config.h/cosim protocol logic (partially conditional/compiled). Runtime overrides for these
 *   would require broader claim/protocol refactor.
 */

#define LCD_MAX_W 480
#define LCD_MAX_H 480

/* Board metadata kept for state persistence / info defaults; panel geometry is runtime-configurable. */
typedef struct {
    const char *id;
    const char *name;
    int lcd_w;
    int lcd_h;
} BoardProfile;

static const BoardProfile kBoards[] = {
    {"es3c28p", "ES3C28P (KryonOS local)", 240, 320},
    {"esp32-s3-devkitc-1-n16r8", "ESP32-S3-DevKitC-1 N16R8", 240, 320},
    {"lilygo-t-deck", "LilyGO T-Deck", 320, 240},
    {"lilygo-t-hmi", "LilyGO T-HMI", 240, 320},
    {"m5stack-cardputer", "M5Stack Cardputer", 240, 135},
};
static const int kBoardCount = (int)(sizeof(kBoards)/sizeof(kBoards[0]));

/* Compile-time peripheral/bus mapping (cosim claims). */
#define LCD_CS     PANEL_LCD_CS
#define LCD_DC     PANEL_LCD_DC
#define LCD_BL     PANEL_LCD_BL
#define LCD_MOSI   PANEL_LCD_MOSI
#define LCD_MISO   PANEL_LCD_MISO
#define LCD_SCLK   PANEL_LCD_SCLK
#define TOUCH_ADDR PANEL_TOUCH_ADDR
#define TOUCH_SCL  PANEL_TOUCH_SCL
#define TOUCH_SDA  PANEL_TOUCH_SDA
#define TOUCH_INT  PANEL_TOUCH_INT
#define TOUCH_RST  PANEL_TOUCH_RST

struct SimHost {
    /* config */
    char host[256];
    char port[32];
    char unix_path[512];
    char uart_target[256];
    char rot_state_file[512];
    char board_state_file[512];
    char board_profile[512];
    char board_profile_path[512];
    char board_id[64];
    char board_name[128];
    char board_status[64];
    char board_display_info[256];
    char board_touch_info[256];
    char board_input_info[192];
    char board_caps_summary[1024];
    char cosim_name[128];
    bool have_unix;
    bool have_uart;
    bool have_rot_state;
    bool have_board_state;
    bool have_board_profile;
    int reconnect_max;
    int reconnect_delay_ms;
    bool debug;

    /* runtime display/panel/touch */
    int panel_native_w;
    int panel_native_h;
    int scale;
    int board_idx;
    int view_rot;
    int hw_rot;
    int boot_rot;              /* rotation at start; what touch reg 0xF1 reports (hw_rot follows the firmware MADCTL) */
    bool follow_madctl;        /* es3c28p: follow firmware MADCTL rotation at run time */
    bool rot_changed;          /* hw_rot changed at run time; render thread rebuilds texture (under lock) */
    int bl_pin;                /* backlight GPIO, from the profile display.pins.bl */
    int bl_duty_256;           /* backlight level 0..256 (under lock) */
    int mirror_x;
    int mirror_y;
    int touch_swap_xy;
    int touch_invert_x;
    int touch_invert_y;
    int touch_x_off;
    int touch_y_off;

    /* runtime */
    int sock;
    volatile bool running;
    volatile int exit_code;
    SDL_mutex *lock;
    SDL_mutex *send_lock;
    SDL_Thread *net_thread;

    /* video */
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    bool owns_window;
    bool sdl_video_inited;
    bool show_touch_debug;
    int content_w;
    int content_h;

    /* LCD state */
    uint16_t fb565[LCD_MAX_W * LCD_MAX_H];
    bool fb_dirty;
    uint8_t lcd_cmd;
    uint8_t lcd_args[16];
    size_t lcd_args_len;
    int xs, xe, ys, ye;
    int cx, cy;
    uint8_t madctl;
    bool inverted;
    uint8_t odd_byte;
    bool have_odd;

    /* Touch state */
    uint8_t touch_regs[256];
    uint8_t touch_reg_ptr;
    bool touch_down;
    int touch_x, touch_y;
    int touch_dx, touch_dy;
    uint32_t touch_hold_until_ms;
    bool touch_pending_release;
    uint32_t touch_press_ms;   /* wall time of the press, for the hard release timeout */
    int touch_reads;           /* firmware reads of the touch block since the press (under lock) */
    int touch_last_vx, touch_last_vy;
};

static const char *cfg_str(const char *s, const char *def) { return (s && *s) ? s : def; }
static int clampi(int v, int lo, int hi) { if (v < lo) return lo; if (v > hi) return hi; return v; }

void sim_host_config_init(SimHostConfig *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->host = "127.0.0.1";
    cfg->port = PANEL_COSIM_PORT;
    cfg->cosim_name = PANEL_COSIM_NAME;
    cfg->board_profile = "es3c28p";
    cfg->reconnect_max = 0;
    cfg->reconnect_delay_ms = 1000;
    cfg->scale = PANEL_WINDOW_SCALE;
    cfg->show_touch_debug = true;
    cfg->lcd_w = PANEL_WIDTH;
    cfg->lcd_h = PANEL_HEIGHT;
    cfg->mirror_x = PANEL_MIRROR_X;
    cfg->mirror_y = PANEL_MIRROR_Y;
    cfg->touch_swap_xy = PANEL_TOUCH_SWAP_XY;
    cfg->touch_invert_x = PANEL_TOUCH_INVERT_X;
    cfg->touch_invert_y = PANEL_TOUCH_INVERT_Y;
    cfg->touch_x_off = 0;
    cfg->touch_y_off = 0;
}

static inline int host_panel_w(const SimHost *h) { return (h->hw_rot % 2 == 0) ? h->panel_native_w : h->panel_native_h; }
static inline int host_panel_h(const SimHost *h) { return (h->hw_rot % 2 == 0) ? h->panel_native_h : h->panel_native_w; }
static inline int host_view_w(const SimHost *h) { return (h->view_rot % 2 == 0) ? host_panel_w(h) : host_panel_h(h); }
static inline int host_view_h(const SimHost *h) { return (h->view_rot % 2 == 0) ? host_panel_h(h) : host_panel_w(h); }

int sim_host_hw_rot(const SimHost *host) { return host ? host->hw_rot : 0; }
int sim_host_panel_w(const SimHost *host) { return host ? host_panel_w(host) : 0; }
int sim_host_panel_h(const SimHost *host) { return host ? host_panel_h(host) : 0; }
int sim_host_view_w(const SimHost *host) { return host ? host_view_w(host) : 0; }
int sim_host_view_h(const SimHost *host) { return host ? host_view_h(host) : 0; }
const char *sim_host_board_id(const SimHost *host) { return (host && host->board_id[0]) ? host->board_id : ""; }
const char *sim_host_board_profile_path(const SimHost *host) { return (host && host->board_profile_path[0]) ? host->board_profile_path : ""; }
const char *sim_host_board_name(const SimHost *host) { return (host && host->board_name[0]) ? host->board_name : ""; }
const char *sim_host_board_status(const SimHost *host) { return (host && host->board_status[0]) ? host->board_status : ""; }
const char *sim_host_board_display_info(const SimHost *host) { return (host && host->board_display_info[0]) ? host->board_display_info : ""; }
const char *sim_host_board_touch_info(const SimHost *host) { return (host && host->board_touch_info[0]) ? host->board_touch_info : ""; }
const char *sim_host_board_input_info(const SimHost *host) { return (host && host->board_input_info[0]) ? host->board_input_info : ""; }
const char *sim_host_capabilities(const SimHost *host) { return (host && host->board_caps_summary[0]) ? host->board_caps_summary : ""; }

void sim_host_get_display_params(const SimHost *host, int *native_w, int *native_h, int *scale, int *hw_rot, int *mirror_x, int *mirror_y) {
    if (!host) return;
    if (native_w) *native_w = host->panel_native_w;
    if (native_h) *native_h = host->panel_native_h;
    if (scale)    *scale = host->scale;
    if (hw_rot)   *hw_rot = host->hw_rot;
    if (mirror_x) *mirror_x = host->mirror_x;
    if (mirror_y) *mirror_y = host->mirror_y;
}

void sim_host_get_touch_params(const SimHost *host, int *swap_xy, int *invert_x, int *invert_y, int *x_off, int *y_off) {
    if (!host) return;
    if (swap_xy)  *swap_xy = host->touch_swap_xy;
    if (invert_x) *invert_x = host->touch_invert_x;
    if (invert_y) *invert_y = host->touch_invert_y;
    if (x_off)    *x_off = host->touch_x_off;
    if (y_off)    *y_off = host->touch_y_off;
}

static void host_update_capability_summary(SimHost *h) {
    if (!h) return;
    snprintf(h->board_caps_summary, sizeof(h->board_caps_summary),
        "runtime-supported: display(native %dx%d, scale, hw_rot, mirror_x/y), touch(swap_xy,invert_x/y,x_off/y_off), board profile load, cosim name/transport(TCP|Unix)/reconnect/state files; "
        "compile-time/conditional(build): LCD_MAX=%dx%d, SPI/I2C pins+GPIO claims (CS=%d DC=%d SCLK=%d MOSI=%d MISO=%d BL=%d SCL=%d SDA=%d INT=%d RST=%d), touch controller/addr/protocol modeled as FT6336-style I2C regfile @0x%02X only; "
        "not modeled dynamically: parallel RGB/MIPI bus, GT911/XPT2046/other touch controllers, matrix keyboard/input devices, dynamic pin remap/bus protocol emulation.",
        h->panel_native_w, h->panel_native_h,
        LCD_MAX_W, LCD_MAX_H, LCD_CS, LCD_DC, LCD_SCLK, LCD_MOSI, LCD_MISO, LCD_BL, TOUCH_SCL, TOUCH_SDA, TOUCH_INT, TOUCH_RST, TOUCH_ADDR);
}

void sim_host_get_config(const SimHost *host, SimHostConfig *out) {
    if (!host || !out) return;
    sim_host_config_init(out);
    out->host = host->host;
    out->port = host->port;
    out->unix_path = host->have_unix ? host->unix_path : NULL;
    out->uart_target = host->have_uart ? host->uart_target : NULL;
    out->rot_state_file = host->have_rot_state ? host->rot_state_file : NULL;
    out->board_state_file = host->have_board_state ? host->board_state_file : NULL;
    out->board_profile = host->have_board_profile ? host->board_profile : NULL;
    out->cosim_name = host->cosim_name;
    out->reconnect_max = host->reconnect_max;
    out->reconnect_delay_ms = host->reconnect_delay_ms;
    out->debug = host->debug;
    out->hw_rot = host->hw_rot;
    out->scale = host->scale;
    out->show_touch_debug = host->show_touch_debug;
    out->lcd_w = host->panel_native_w;
    out->lcd_h = host->panel_native_h;
    out->mirror_x = host->mirror_x;
    out->mirror_y = host->mirror_y;
    out->touch_swap_xy = host->touch_swap_xy;
    out->touch_invert_x = host->touch_invert_x;
    out->touch_invert_y = host->touch_invert_y;
    out->touch_x_off = host->touch_x_off;
    out->touch_y_off = host->touch_y_off;
}

void sim_host_set_display(SimHost *host, int lcd_w, int lcd_h, int scale, int hw_rot) {
    if (!host) return;
    if (lcd_w > 0) host->panel_native_w = clampi(lcd_w, 1, LCD_MAX_W);
    if (lcd_h > 0) host->panel_native_h = clampi(lcd_h, 1, LCD_MAX_H);
    if (scale > 0) host->scale = scale;
    host->hw_rot = hw_rot & 3;
    host->boot_rot = host->hw_rot;
    host_update_capability_summary(host);
}

void sim_host_set_panel_transform(SimHost *host, int mirror_x, int mirror_y) {
    if (!host) return;
    host->mirror_x = mirror_x ? 1 : 0;
    host->mirror_y = mirror_y ? 1 : 0;
}

void sim_host_set_touch_mapping(SimHost *host, int swap_xy, int invert_x, int invert_y, int x_off, int y_off) {
    if (!host) return;
    host->touch_swap_xy = swap_xy ? 1 : 0;
    host->touch_invert_x = invert_x ? 1 : 0;
    host->touch_invert_y = invert_y ? 1 : 0;
    host->touch_x_off = x_off;
    host->touch_y_off = y_off;
}

static void host_touch_update_regs(SimHost *h) {
    memset(h->touch_regs, 0, sizeof(h->touch_regs));
    h->touch_regs[0xA3] = 0x00;
    h->touch_regs[0xA6] = 0x11;
    h->touch_regs[0xA8] = 0x11;
    h->touch_regs[0xAF] = 0x00;
    h->touch_regs[0xF1] = (uint8_t)(h->boot_rot & 3);
    if (h->touch_down) {
        h->touch_regs[0x02] = 1;
        h->touch_regs[0x03] = (0x02 << 6) | ((h->touch_x >> 8) & 0x0F);
        h->touch_regs[0x04] = h->touch_x & 0xFF;
        h->touch_regs[0x05] = (0x00 << 4) | ((h->touch_y >> 8) & 0x0F);
        h->touch_regs[0x06] = h->touch_y & 0xFF;
    } else {
        h->touch_regs[0x02] = 0;
        h->touch_regs[0x03] = 0x40;
        h->touch_regs[0x05] = 0x40;
    }
}

static bool host_send_gpio_drive(SimHost *h, int pin, bool level);
static uint32_t host_now_ms(void) { return SDL_GetTicks(); }

static void host_touch_apply(SimHost *h, bool down, int vx, int vy) {
    int pw = host_panel_w(h), ph = host_panel_h(h);
    vx = clampi(vx, 0, pw - 1);
    vy = clampi(vy, 0, ph - 1);
    /* The chip reports glass coordinates (portrait, rotation 0); the firmware rotates them.
     * So turn the rotated screen point back into a glass point: touchPoll() run backwards. */
    int nw = h->panel_native_w, nh = h->panel_native_h;
    int rx, ry;
    switch (h->hw_rot & 3) {
        case 1:  rx = nw - 1 - vy; ry = vx; break;
        case 2:  rx = nw - 1 - vx; ry = nh - 1 - vy; break;
        case 3:  rx = vy; ry = nh - 1 - vx; break;
        default: rx = vx; ry = vy; break;
    }
    if (h->touch_swap_xy) { int t = rx; rx = ry; ry = t; }
    int px = h->touch_invert_x ? (nw - 1 - rx) : rx;
    int py = h->touch_invert_y ? (nh - 1 - ry) : ry;
    px = clampi(px + h->touch_x_off, 0, nw - 1);
    py = clampi(py + h->touch_y_off, 0, nh - 1);
    SDL_LockMutex(h->lock);
    h->touch_down = down; h->touch_x = px; h->touch_y = py;
    h->touch_dx = vx; h->touch_dy = vy;
    host_touch_update_regs(h);
    SDL_UnlockMutex(h->lock);
    bool int_level = down ? false : true;
    if (h->debug) {
        fprintf(stderr, "[touch] %s view=(%d,%d) panel_raw=(%d,%d) INT=%d\n",
                down ? "DOWN" : "UP", vx, vy, px, py, int_level ? 1 : 0);
        fflush(stderr);
    }
    host_send_gpio_drive(h, TOUCH_INT, int_level);
}

/* Where the view is drawn in a ww x wh window: a whole multiple of the view, centred, so every LCD
 * pixel is the same size (a stretch to any size makes some pixels 1 px wide and others 2). Smaller
 * than 1x: fit by aspect ratio instead. */
static void host_view_rect(const SimHost *h, int ww, int wh, int *dx, int *dy, int *dw, int *dh) {
    int vw = host_view_w(h), vh = host_view_h(h);
    if (ww <= 0) ww = vw;
    if (wh <= 0) wh = vh;
    int s = (ww / vw < wh / vh) ? ww / vw : wh / vh;
    if (s >= 1) { *dw = vw * s; *dh = vh * s; }
    else if (ww * vh < wh * vw) { *dw = ww; *dh = ww * vh / vw; }
    else { *dh = wh; *dw = wh * vw / vh; }
    *dx = (ww - *dw) / 2; *dy = (wh - *dh) / 2;
}

static void host_window_to_fb(SimHost *h, int wx, int wy, int ww, int content_h, int *fx, int *fy) {
    int vw = host_view_w(h), vh = host_view_h(h);
    int dx, dy, dw, dh;
    host_view_rect(h, ww, content_h, &dx, &dy, &dw, &dh);
    if (dw <= 0) dw = 1;
    if (dh <= 0) dh = 1;
    int vx = (wx - dx) * vw / dw;
    int vy = (wy - dy) * vh / dh;
    vx = clampi(vx, 0, vw - 1);
    vy = clampi(vy, 0, vh - 1);
    int pw = host_panel_w(h), ph = host_panel_h(h);
    switch (h->view_rot & 3) {
        case 0: *fx = vx; *fy = vy; break;
        case 1: *fx = vy; *fy = (ph - 1 - vx); break;
        case 2: *fx = (pw - 1 - vx); *fy = (ph - 1 - vy); break;
        default: *fx = (pw - 1 - vy); *fy = vx; break;
    }
}

/* --- minimal board profile loading (id or JSON path). No external JSON deps. --- */
static void reset_lcd_state(SimHost *h);
static bool file_read_all(const char *path, char **out, size_t *out_len) {
    if (!path || !out) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return false; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }
    char *buf = (char*)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[rd] = '\0';
    *out = buf;
    if (out_len) *out_len = rd;
    return true;
}

static const char *json_skip_ws(const char *p) {
    while (p && *p && (unsigned char)*p <= 32) p++;
    return p;
}
static const char *json_find_key(const char *json, const char *key) {
    if (!json || !key) return NULL;
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = json;
    while ((p = strstr(p, pat)) != NULL) {
        const char *q = json_skip_ws(p + strlen(pat));
        if (q && *q == ':') return q + 1;
        p += strlen(pat);
    }
    return NULL;
}
/* Find key inside an object range [obj,obj_end) (best-effort). */
static const char *json_find_key_range(const char *obj, const char *obj_end, const char *key) {
    if (!obj || !key) return NULL;
    char pat[128]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = obj;
    while ((p = strstr(p, pat)) != NULL) {
        if (obj_end && p >= obj_end) return NULL;
        const char *q = json_skip_ws(p + strlen(pat));
        if (q && *q == ':') return q + 1;
        p += strlen(pat);
    }
    return NULL;
}
static const char *json_find_object(const char *json, const char *key, const char **obj_end) {
    const char *v = json_skip_ws(json_find_key(json, key));
    if (!v || *v != '{') return NULL;
    int depth = 0; const char *p = v;
    while (*p) {
        if (*p == '{') depth++;
        else if (*p == '}') { depth--; if (depth == 0) { if (obj_end) *obj_end = p + 1; return v; } }
        else if (*p == '"') { p++; while (*p) { if (*p == '\\' && p[1]) { p += 2; continue; } if (*p == '"') break; p++; } }
        p++;
    }
    return NULL;
}
static bool json_get_string_range(const char *json, const char *end, const char *key, char *out, size_t out_sz) {
    const char *v = json_skip_ws(json_find_key_range(json, end, key));
    if (!v || *v != '"') return false;
    v++; size_t n = 0;
    while (*v && *v != '"' && n + 1 < out_sz) {
        if (*v == '\\' && v[1]) { v++; if (*v == 'n') out[n++]='\n'; else out[n++]=*v; v++; continue; }
        out[n++] = *v++;
    }
    out[n] = '\0';
    return *v == '"';
}
static bool json_get_int_range(const char *json, const char *end, const char *key, int *out) {
    const char *v = json_skip_ws(json_find_key_range(json, end, key));
    if (!v) return false;
    if (*v == '"') { v++; *out = atoi(v); return true; }
    if ((*v >= '0' && *v <= '9') || *v == '-' || *v == '+') { *out = atoi(v); return true; }
    if (!strncmp(v, "null", 4)) return false;
    return false;
}
static bool json_get_bool_range(const char *json, const char *end, const char *key, bool *out) {
    const char *v = json_skip_ws(json_find_key_range(json, end, key));
    if (!v) return false;
    if (!strncmp(v, "true", 4)) { *out = true; return true; }
    if (!strncmp(v, "false", 5)) { *out = false; return true; }
    if (!strncmp(v, "null", 4)) return false;
    return false;
}

static void host_apply_board_defaults(SimHost *h) {
    snprintf(h->board_id, sizeof(h->board_id), "%s", "es3c28p");
    snprintf(h->board_name, sizeof(h->board_name), "%s", "ES3C28P (KryonOS local)");
    snprintf(h->board_status, sizeof(h->board_status), "%s", "supported-default");
    snprintf(h->board_display_info, sizeof(h->board_display_info), "bus=spi controller=compile-time-default (profile metadata not loaded)");
    snprintf(h->board_touch_info, sizeof(h->board_touch_info), "type=capacitive controller=FT6336-style bus=i2c addr=0x%02X (compile-time claims)", TOUCH_ADDR);
    snprintf(h->board_input_info, sizeof(h->board_input_info), "input=touchscreen matrix_keyboard=unknown");
    host_update_capability_summary(h);
}

static bool host_resolve_board_profile_path(const char *id_or_path, char *out, size_t out_sz) {
    if (!id_or_path || !*id_or_path || !out || out_sz == 0) return false;
    /* direct path */
    FILE *f = fopen(id_or_path, "rb");
    if (f) { fclose(f); snprintf(out, out_sz, "%s", id_or_path); return true; }
    const char *dirs[] = { "simulator/emu/boards", "boards", "simulator/launcher/boards", NULL };
    char cand[1024];
    for (int i=0; dirs[i]; i++) {
        snprintf(cand, sizeof(cand), "%s/%s.json", dirs[i], id_or_path);
        f = fopen(cand, "rb"); if (f) { fclose(f); snprintf(out, out_sz, "%.*s", (int)(out_sz - 1), cand); return true; }
        snprintf(cand, sizeof(cand), "%s/%s", dirs[i], id_or_path);
        f = fopen(cand, "rb"); if (f) { fclose(f); snprintf(out, out_sz, "%.*s", (int)(out_sz - 1), cand); return true; }
    }
    /* fallback: try relative to board file location not known; also basename id */
    snprintf(cand, sizeof(cand), "simulator/emu/boards/%s.json", id_or_path);
    f = fopen(cand, "rb"); if (f) { fclose(f); snprintf(out, out_sz, "%.*s", (int)(out_sz - 1), cand); return true; }
    return false;
}

bool sim_host_load_board_profile(SimHost *host, const char *id_or_path) {
    if (!host) return false;
    if (!id_or_path || !*id_or_path) id_or_path = "es3c28p";
    snprintf(host->board_profile, sizeof(host->board_profile), "%s", id_or_path);
    host->have_board_profile = true;
    host_apply_board_defaults(host);

    char resolved[512];
    if (!host_resolve_board_profile_path(id_or_path, resolved, sizeof(resolved))) {
        /* try legacy kBoards id match for geometry defaults */
        for (int i = 0; i < kBoardCount; i++) {
            if (strcmp(kBoards[i].id, id_or_path) == 0) {
                host->board_idx = i;
                host->panel_native_w = clampi(kBoards[i].lcd_w, 1, LCD_MAX_W);
                host->panel_native_h = clampi(kBoards[i].lcd_h, 1, LCD_MAX_H);
                snprintf(host->board_id, sizeof(host->board_id), "%s", kBoards[i].id);
                snprintf(host->board_name, sizeof(host->board_name), "%s", kBoards[i].name);
                snprintf(host->board_status, sizeof(host->board_status), "%s", "legacy-builtin");
                snprintf(host->board_profile_path, sizeof(host->board_profile_path), "%s", "(builtin)");
                snprintf(host->board_display_info, sizeof(host->board_display_info), "metadata=builtin-only (no JSON)");
                snprintf(host->board_touch_info, sizeof(host->board_touch_info), "metadata=builtin-only; runtime touch model remains compile-time FT6336@0x%02X", TOUCH_ADDR);
                host_update_capability_summary(host);
                fprintf(stderr, "[board] profile '%s': builtin metadata only (no JSON file found). Action: add simulator/emu/boards/%s.json for richer hints.\n", id_or_path, id_or_path);
                sim_host_print_board_info(host);
                return true;
            }
        }
        snprintf(host->board_profile_path, sizeof(host->board_profile_path), "%s", "(not-found)");
        fprintf(stderr, "[board] profile '%s': NOT FOUND. Action: pass valid board id/path under simulator/emu/boards/; keeping current/default runtime config (no dynamic bus/pin/controller emulation applied).\n", id_or_path);
        host_update_capability_summary(host);
        sim_host_print_board_info(host);
        return false;
    }

    snprintf(host->board_profile_path, sizeof(host->board_profile_path), "%s", resolved);
    char *json = NULL; size_t jlen = 0;
    if (!file_read_all(resolved, &json, &jlen)) {
        fprintf(stderr, "[board] failed to read profile: %s. Action: check file permissions/path; keeping defaults.\n", resolved);
        host_update_capability_summary(host);
        return false;
    }

    /* top-level fields */
    json_get_string_range(json, NULL, "id", host->board_id, sizeof(host->board_id));
    json_get_string_range(json, NULL, "name", host->board_name, sizeof(host->board_name));
    json_get_string_range(json, NULL, "status", host->board_status, sizeof(host->board_status));
    for (int i = 0; i < kBoardCount; i++) {
        if (strcmp(kBoards[i].id, host->board_id) == 0) host->board_idx = i;
    }

    /* display: runtime-supported w/h/mirror/rotation; surface bus/controller/protocol hints */
    const char *disp_end = NULL;
    const char *disp = json_find_object(json, "display", &disp_end);
    char disp_bus[64]={0}, disp_ctrl[96]={0}, disp_panel[64]={0};
    if (disp) {
        int w=0,h=0; bool b;
        if (json_get_int_range(disp, disp_end, "width", &w) && w > 0) host->panel_native_w = clampi(w, 1, LCD_MAX_W);
        if (json_get_int_range(disp, disp_end, "height", &h) && h > 0) host->panel_native_h = clampi(h, 1, LCD_MAX_H);
        if (json_get_bool_range(disp, disp_end, "mirror_x", &b)) host->mirror_x = b ? 1 : 0;
        if (json_get_bool_range(disp, disp_end, "mirror_y", &b)) host->mirror_y = b ? 1 : 0;
        int rd = 0; if (json_get_int_range(disp, disp_end, "rotation_default", &rd)) host->hw_rot = rd & 3;
        {
            const char *pins_end = NULL;
            const char *pins = json_find_object(disp, "pins", &pins_end);
            int blp = 0;
            if (pins && pins_end <= disp_end && json_get_int_range(pins, pins_end, "bl", &blp) && blp >= 0 && blp < 64) host->bl_pin = blp;
        }
        json_get_string_range(disp, disp_end, "bus", disp_bus, sizeof(disp_bus));
        json_get_string_range(disp, disp_end, "controller", disp_ctrl, sizeof(disp_ctrl));
        json_get_string_range(disp, disp_end, "panel", disp_panel, sizeof(disp_panel));
        snprintf(host->board_display_info, sizeof(host->board_display_info), "bus=%s ctrl=%s panel=%s (profile metadata)",
                 disp_bus[0]?disp_bus:"?", disp_ctrl[0]?disp_ctrl:"?", disp_panel[0]?disp_panel:"?");
        if (disp_bus[0] && strcasecmp(disp_bus, "spi") != 0) {
            fprintf(stderr, "[board] display bus '%s': NOT runtime-modeled (host uses compile-time SPI LCD path). Action: SPI only in this build; parallel/RGB/MIPI require host/protocol support.\n", disp_bus);
        }
        if (disp_ctrl[0] && (strcasestr(disp_ctrl, "parallel") || strcasestr(disp_ctrl, "rgb") || strcasestr(disp_ctrl, "mipi"))) {
            fprintf(stderr, "[board] display controller '%s': NOT runtime-modeled. Action: metadata/validation only; implement controller/bus emulation in sim_host/cosim to claim support.\n", disp_ctrl);
        }
    }

    /* touch mapping/calibration: runtime-supported; surface controller/bus/addr/type hints */
    const char *touch_end = NULL;
    const char *touch = json_find_object(json, "touch", &touch_end);
    char ttype[64]={0}, tctrl[64]={0}, tbus[32]={0}, taddr[32]={0};
    if (touch) {
        json_get_string_range(touch, touch_end, "type", ttype, sizeof(ttype));
        json_get_string_range(touch, touch_end, "controller", tctrl, sizeof(tctrl));
        json_get_string_range(touch, touch_end, "bus", tbus, sizeof(tbus));
        json_get_string_range(touch, touch_end, "addr", taddr, sizeof(taddr));
        snprintf(host->board_touch_info, sizeof(host->board_touch_info), "type=%s ctrl=%s bus=%s addr=%s (profile metadata; runtime model compile-time)",
                 ttype[0]?ttype:"?", tctrl[0]?tctrl:"?", tbus[0]?tbus:"?", taddr[0]?taddr:"?");
        bool modeled = (!tctrl[0] || !strcasecmp(tctrl, "FT6336G") || !strcasecmp(tctrl, "ft6336") || !strcasecmp(tctrl, "ft6336u"))
                       && (!tbus[0] || !strcasecmp(tbus, "i2c")) && (!taddr[0] || !strcasecmp(taddr, "0x38"));
        if (!modeled && (tctrl[0] || tbus[0] || taddr[0])) {
            fprintf(stderr, "[board] touch requested '%s/%s addr=%s': NOT runtime-configurable/modeled by this host. Current build claims FT6336-style I2C regfile @0x%02X on SCL=%d SDA=%d INT=%d RST=%d. Action: rebuild/update panel_config.h + cosim claims and implement controller protocol (GT911/XPT2046/etc.) in sim_host for full support.\n",
                    tbus[0]?tbus:"i2c", tctrl[0]?tctrl:"?", taddr[0]?taddr:"?", TOUCH_ADDR, TOUCH_SCL, TOUCH_SDA, TOUCH_INT, TOUCH_RST);
        }
        const char *map_end=NULL, *cal_end=NULL;
        const char *map = json_find_object(touch, "mapping", &map_end);
        const char *cal = json_find_object(touch, "calibration", &cal_end);
        if (map) {
            bool b;
            if (json_get_bool_range(map, map_end, "swap_xy", &b)) host->touch_swap_xy = b ? 1 : 0;
            if (json_get_bool_range(map, map_end, "invert_x", &b)) host->touch_invert_x = b ? 1 : 0;
            if (json_get_bool_range(map, map_end, "invert_y", &b)) host->touch_invert_y = b ? 1 : 0;
        }
        if (cal) {
            int v=0;
            if (json_get_int_range(cal, cal_end, "x_off", &v)) host->touch_x_off = v;
            if (json_get_int_range(cal, cal_end, "y_off", &v)) host->touch_y_off = v;
        }
    }

    /* input hints (e.g. matrix keyboard) - surfaced only */
    const char *input_end=NULL; const char *input = json_find_object(json, "input", &input_end);
    if (input) {
        char itype[64]={0}, kbctrl[96]={0}; bool matrix=false;
        json_get_string_range(input, input_end, "type", itype, sizeof(itype));
        json_get_string_range(input, input_end, "keyboard_controller", kbctrl, sizeof(kbctrl));
        json_get_bool_range(input, input_end, "matrix_keyboard", &matrix);
        snprintf(host->board_input_info, sizeof(host->board_input_info), "type=%s matrix_kb=%s ctrl=%s",
                 itype[0]?itype:"?", matrix?"yes":"no", kbctrl[0]?kbctrl:"-");
        if (matrix || (itype[0] && strcasestr(itype, "keyboard"))) {
            fprintf(stderr, "[board] input '%s' (matrix/keyboard): NOT modeled by sim_host. Action: no dynamic keyboard/matrix input device in this build; profile metadata only.\n", itype[0]?itype:"keyboard");
        }
    }

    /* cosim device/name hint */
    const char *cos_end=NULL; const char *cos = json_find_object(json, "cosim", &cos_end);
    if (cos) {
        char dev[128]={0};
        if (json_get_string_range(cos, cos_end, "device", dev, sizeof(dev)) && dev[0]) {
            /* only adopt if still default */
            if (!strcmp(host->cosim_name, PANEL_COSIM_NAME)) snprintf(host->cosim_name, sizeof(host->cosim_name), "%s", dev);
        }
    }

    host_update_capability_summary(host);
    fprintf(stderr, "[board] loaded '%s' (%s) status=%s from %s\n", host->board_id, host->board_name, host->board_status[0]?host->board_status:"-", host->board_profile_path);
    fprintf(stderr, "[board] metadata: display[%s] touch[%s] input[%s]\n", host->board_display_info, host->board_touch_info, host->board_input_info[0]?host->board_input_info:"-");
    fprintf(stderr, "[board] runtime applied: lcd=%dx%d mirror=%d,%d touch(swap=%d inv=%d,%d off=%d,%d) hw_rot=%d scale=%d cosim=%s\n",
            host->panel_native_w, host->panel_native_h, host->mirror_x, host->mirror_y,
            host->touch_swap_xy, host->touch_invert_x, host->touch_invert_y, host->touch_x_off, host->touch_y_off,
            host->hw_rot, host->scale, host->cosim_name);
    fprintf(stderr, "[board] caps: %s\n", host->board_caps_summary);

    free(json);
    host_touch_update_regs(host);
    reset_lcd_state(host);
    return true;
}

void sim_host_print_board_info(const SimHost *host) {
    if (!host) return;
    fprintf(stderr, "[board] id=%s name='%s' status=%s profile='%s' path=%s\n",
            host->board_id, host->board_name, host->board_status, host->board_profile, host->board_profile_path);
    fprintf(stderr, "[board] metadata: display[%s] touch[%s] input[%s]\n",
            host->board_display_info[0]?host->board_display_info:"-", host->board_touch_info[0]?host->board_touch_info:"-", host->board_input_info[0]?host->board_input_info:"-");
    fprintf(stderr, "[board] runtime effective: panel_native=%dx%d view=%dx%d hw_rot=%d scale=%d mirror=%d,%d touch(swap=%d inv=%d,%d off=%d,%d) cosim=%s transport=%s\n",
            host->panel_native_w, host->panel_native_h, host_view_w(host), host_view_h(host), host->hw_rot, host->scale,
            host->mirror_x, host->mirror_y, host->touch_swap_xy, host->touch_invert_x, host->touch_invert_y, host->touch_x_off, host->touch_y_off,
            host->cosim_name, host->have_unix ? "unix" : "tcp");
    fprintf(stderr, "[board] compile-time/conditional claims: LCD_CS=%d DC=%d SCLK=%d MOSI=%d MISO=%d BL=%d TOUCH_ADDR=0x%02X SCL=%d SDA=%d INT=%d RST=%d (fixed at build; profile pin/bus/controller values not dynamically claimed)\n",
            LCD_CS, LCD_DC, LCD_SCLK, LCD_MOSI, LCD_MISO, LCD_BL, TOUCH_ADDR, TOUCH_SCL, TOUCH_SDA, TOUCH_INT, TOUCH_RST);
    fprintf(stderr, "[board] caps: %s\n", host->board_caps_summary[0]?host->board_caps_summary:"(not initialized)");
}

void sim_host_save_state(SimHost *host) {
    if (!host) return;
    if (host->have_rot_state) {
        FILE *f = fopen(host->rot_state_file, "w");
        if (f) { fprintf(f, "%d\n", host->hw_rot); fclose(f); }
    }
    if (host->have_board_state) {
        FILE *f = fopen(host->board_state_file, "w");
        if (f) {
            const char *tok = (host->board_profile[0]) ? host->board_profile : host->board_id;
            fprintf(f, "%s\n", tok && *tok ? tok : kBoards[host->board_idx].id);
            fclose(f);
        }
    }
}

void sim_host_load_state(SimHost *host) {
    if (!host) return;
    if (host->have_board_state) {
        FILE *f = fopen(host->board_state_file, "r");
        if (f) {
            char id[512] = {0};
            if (fgets(id, sizeof(id), f)) {
                size_t n = strlen(id);
                while (n > 0 && (id[n-1]=='\n' || id[n-1]=='\r' || id[n-1]==' ' || id[n-1]=='\t')) id[--n] = 0;
                if (id[0]) {
                    /* keep token for later apply (board profile may override geometry) */
                    snprintf(host->board_profile, sizeof(host->board_profile), "%s", id);
                    host->have_board_profile = true;
                    for (int i = 0; i < kBoardCount; i++) {
                        if (strcmp(kBoards[i].id, id) == 0) { host->board_idx = i; break; }
                    }
                }
            }
            fclose(f);
        }
    }
    if (host->have_rot_state) {
        FILE *f = fopen(host->rot_state_file, "r");
        if (f) {
            int r = host->hw_rot;
            if (fscanf(f, "%d", &r) == 1) host->hw_rot = r & 3;
            fclose(f);
        }
    }
}

void sim_host_request_rotation_step(SimHost *host) {
    if (!host) return;
    host->hw_rot = (host->hw_rot + 1) & 3;
    sim_host_save_state(host);
    fprintf(stderr, "[sim] hw rotation -> %d (rebooting firmware)\n", host->hw_rot);
    host->exit_code = 42;
    host->running = false;
}

/* The emulator runs slower than real time, so the firmware polls the chip rarely in wall-clock
 * terms. A fixed wall-clock hold dropped short clicks before it read them even once. Keep the
 * touch down until the firmware has read the register block enough times (its Pressed() takes
 * two samples, plus one for the 80 ms touchPoll() hold), after the minimum hold; a firmware that
 * never reads still gets released after the hard timeout. */
#define TOUCH_MIN_HOLD_MS  60
#define TOUCH_MIN_READS    3
#define TOUCH_MAX_HOLD_MS  1500

static bool host_touch_release_due(SimHost *h) {
    uint32_t t = host_now_ms();
    SDL_LockMutex(h->lock);
    bool due = t - h->touch_press_ms >= TOUCH_MAX_HOLD_MS ||
               (t >= h->touch_hold_until_ms && h->touch_reads >= TOUCH_MIN_READS);
    SDL_UnlockMutex(h->lock);
    return due;
}

static void host_touch_set(SimHost *h, bool down, int x, int y) {
    if (down) {
        /* A drag keeps the same press; a new press (also while a release is pending) restarts it. */
        if (!h->touch_down || h->touch_pending_release) {
            uint32_t t = host_now_ms();
            SDL_LockMutex(h->lock);
            h->touch_reads = 0;
            h->touch_press_ms = t;
            h->touch_hold_until_ms = t + TOUCH_MIN_HOLD_MS;
            SDL_UnlockMutex(h->lock);
        }
        h->touch_pending_release = false;
        h->touch_last_vx = x; h->touch_last_vy = y;
        host_touch_apply(h, true, x, y);
    } else {
        if (!host_touch_release_due(h)) {
            h->touch_pending_release = true;
            h->touch_last_vx = x; h->touch_last_vy = y;
        } else {
            h->touch_pending_release = false;
            host_touch_apply(h, false, x, y);
        }
    }
}

/* --- minimal protobuf helpers --- */
typedef struct { const uint8_t *p; size_t n; } slice_t;
typedef struct { uint32_t num; uint32_t wire; uint64_t u; slice_t b; } field_t;

static bool read_varint(const uint8_t *buf, size_t len, size_t *i, uint64_t *out) {
    uint64_t v = 0; int shift = 0;
    while (*i < len && shift < 64) {
        uint8_t byte = buf[(*i)++];
        v |= (uint64_t)(byte & 0x7F) << shift;
        if (!(byte & 0x80)) { *out = v; return true; }
        shift += 7;
    }
    return false;
}

static void buf_append(uint8_t **buf, size_t *len, size_t *cap, const void *data, size_t n) {
    if (*len + n > *cap) {
        size_t nc = *cap ? *cap * 2 : 64;
        while (nc < *len + n) nc *= 2;
        *buf = (uint8_t*)realloc(*buf, nc);
        *cap = nc;
    }
    memcpy(*buf + *len, data, n);
    *len += n;
}

static void enc_varint(uint8_t **buf, size_t *len, size_t *cap, uint64_t v) {
    uint8_t b[10]; size_t n = 0;
    while (v > 0x7F) { b[n++] = (uint8_t)((v & 0x7F) | 0x80); v >>= 7; }
    b[n++] = (uint8_t)v;
    buf_append(buf, len, cap, b, n);
}
static void enc_key(uint8_t **buf, size_t *len, size_t *cap, uint32_t num, uint32_t wire) {
    enc_varint(buf, len, cap, ((uint64_t)num << 3) | wire);
}
static void enc_varint_field(uint8_t **buf, size_t *len, size_t *cap, uint32_t num, uint64_t v) {
    enc_key(buf, len, cap, num, 0); enc_varint(buf, len, cap, v);
}
static void enc_bytes_field(uint8_t **buf, size_t *len, size_t *cap, uint32_t num, const void *data, size_t n) {
    enc_key(buf, len, cap, num, 2); enc_varint(buf, len, cap, n); buf_append(buf, len, cap, data, n);
}
static void enc_msg_field(uint8_t **buf, size_t *len, size_t *cap, uint32_t num, const uint8_t *msg, size_t n) {
    enc_bytes_field(buf, len, cap, num, msg, n);
}

static bool find_field(const uint8_t *buf, size_t len, uint32_t want_num, field_t *out) {
    size_t i = 0;
    bool found = false;
    while (i < len) {
        uint64_t tag;
        if (!read_varint(buf, len, &i, &tag)) break;
        uint32_t num = (uint32_t)(tag >> 3), wire = (uint32_t)(tag & 7);
        field_t f = {0}; f.num = num; f.wire = wire;
        if (wire == 0) {
            if (!read_varint(buf, len, &i, &f.u)) break;
        } else if (wire == 1) {
            if (i > len || 8 > len - i) break;
            f.b.p = buf + i; f.b.n = 8; i += 8;
        } else if (wire == 2) {
            uint64_t n; if (!read_varint(buf, len, &i, &n)) break;
            if (n > (uint64_t)(len - i)) break;
            f.b.p = buf + i; f.b.n = (size_t)n; i += (size_t)n;
        } else if (wire == 5) {
            if (i > len || 4 > len - i) break;
            f.b.p = buf + i; f.b.n = 4; i += 4;
        } else break;
        if (num == want_num) { *out = f; found = true; }
    }
    return found;
}

/* --- socket/frame helpers --- */
static bool is_timeout_errno(void) {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT;
}
static bool send_all(int fd, const void *buf, size_t n) {
    const uint8_t *p = (const uint8_t*)buf;
    uint32_t start = host_now_ms();
    const uint32_t SEND_TIMEOUT_MS = 2000;
    while (n) {
        ssize_t w = send(fd, p, n, 0);
        if (w < 0) {
            if (errno == EINTR) {
                if ((uint32_t)(host_now_ms() - start) < SEND_TIMEOUT_MS) continue;
                return false;
            }
            if (is_timeout_errno()) {
                if ((uint32_t)(host_now_ms() - start) < SEND_TIMEOUT_MS) continue;
            }
            return false;
        }
        if (w == 0) return false;
        p += w; n -= (size_t)w;
        start = host_now_ms();
    }
    return true;
}
static bool recv_all(int fd, void *buf, size_t n) {
    uint8_t *p = (uint8_t*)buf;
    while (n) {
        ssize_t r = recv(fd, p, n, MSG_WAITALL);
        if (r < 0) {
            if (errno == EINTR) continue;
            if (is_timeout_errno()) { errno = ETIMEDOUT; return false; }
            return false;
        }
        if (r == 0) { errno = ENOTCONN; return false; }
        p += r; n -= (size_t)r;
    }
    return true;
}
static bool host_send_msg(SimHost *h, const uint8_t *msg, size_t msg_len) {
    uint32_t n = (uint32_t)msg_len;
    uint8_t hdr[4]; memcpy(hdr, &n, 4);
    bool ok = false;
    if (h->send_lock) SDL_LockMutex(h->send_lock);
    if (h->sock >= 0) {
        ok = send_all(h->sock, hdr, 4) && (!msg_len || send_all(h->sock, msg, msg_len));
    }
    if (h->send_lock) SDL_UnlockMutex(h->send_lock);
    return ok;
}
static bool host_recv_frame(SimHost *h, char **topic_out, uint8_t **payload_out, size_t *payload_len) {
    uint32_t n;
    if (!recv_all(h->sock, &n, 4)) return false;
    if (n < 2 || n > 64*1024*1024) return false;
    uint8_t *frame = (uint8_t*)malloc(n);
    if (!frame) return false;
    if (!recv_all(h->sock, frame, n)) { free(frame); return false; }
    uint16_t tl; memcpy(&tl, frame, 2);
    if (2u + tl > n) { free(frame); return false; }
    char *topic = (char*)malloc(tl + 1);
    memcpy(topic, frame + 2, tl); topic[tl] = '\0';
    size_t pl = n - 2 - tl;
    uint8_t *payload = (uint8_t*)malloc(pl ? pl : 1);
    if (pl) memcpy(payload, frame + 2 + tl, pl);
    free(frame);
    *topic_out = topic; *payload_out = payload; *payload_len = pl;
    return true;
}

/* --- LCD logic --- */
static void host_lcd_map_xy(SimHost *h, int inx, int iny, int *outx, int *outy) {
    int x = inx, y = iny;
    int w = host_panel_w(h), hh = host_panel_h(h);
    bool mx = h->madctl & 0x40, my = h->madctl & 0x80, mv = h->madctl & 0x20;
    if (mv) { int t=x; x=y; y=t; int tw=w; w=hh; hh=tw; }
    if (mx) x = (w - 1) - x;
    if (my) y = (hh - 1) - y;
    if (mv) { int t=x; x=y; y=t; }
    if (x >= 0 && x < host_panel_w(h) && y >= 0 && y < host_panel_h(h)) {
        bool mirror_x = h->mirror_x != 0;
        if (h->hw_rot % 2 == 1) mirror_x = !mirror_x;
        if (mirror_x) x = (host_panel_w(h) - 1) - x;
        if (h->mirror_y) y = (host_panel_h(h) - 1) - y;
        *outx = x; *outy = y;
    } else { *outx = -1; *outy = -1; }
}
static void host_lcd_flush_args(SimHost *h) {
    if (h->lcd_cmd == 0x2A && h->lcd_args_len >= 4) {
        int nxs = (h->lcd_args[0]<<8)|h->lcd_args[1];
        int nxe = (h->lcd_args[2]<<8)|h->lcd_args[3];
        nxs = clampi(nxs, 0, host_panel_w(h) - 1);
        nxe = clampi(nxe, 0, host_panel_w(h) - 1);
        h->xs = nxs; h->xe = nxe; h->cx = h->xs;
        memmove(h->lcd_args, h->lcd_args+4, h->lcd_args_len-4); h->lcd_args_len -= 4;
    } else if (h->lcd_cmd == 0x2B && h->lcd_args_len >= 4) {
        int nys = (h->lcd_args[0]<<8)|h->lcd_args[1];
        int nye = (h->lcd_args[2]<<8)|h->lcd_args[3];
        nys = clampi(nys, 0, host_panel_h(h) - 1);
        nye = clampi(nye, 0, host_panel_h(h) - 1);
        h->ys = nys; h->ye = nye; h->cy = h->ys;
        memmove(h->lcd_args, h->lcd_args+4, h->lcd_args_len-4); h->lcd_args_len -= 4;
    } else if (h->lcd_cmd == 0x36 && h->lcd_args_len >= 1) {
        h->madctl = h->lcd_args[0];
        if (h->follow_madctl) {
            int nr = -1;
            switch (h->madctl & 0xE0) { case 0x40: nr = 0; break; case 0x20: nr = 1; break; case 0x80: nr = 2; break; case 0xE0: nr = 3; break; default: break; }
            if (nr >= 0 && nr != h->hw_rot) {
                SDL_LockMutex(h->lock);
                h->hw_rot = nr;
                memset(h->fb565, 0, sizeof(h->fb565));
                h->xs = 0; h->xe = host_panel_w(h) - 1; h->ys = 0; h->ye = host_panel_h(h) - 1; h->cx = 0; h->cy = 0;
                h->have_odd = false;
                h->rot_changed = true; h->fb_dirty = true;
                SDL_UnlockMutex(h->lock);
                sim_host_save_state(h);
                fprintf(stderr, "[sim] hw rotation -> %d (firmware)\n", nr);
                fflush(stderr);
            }
        }
        memmove(h->lcd_args, h->lcd_args+1, h->lcd_args_len-1); h->lcd_args_len -= 1;
    } else if (h->lcd_cmd == 0x3A && h->lcd_args_len >= 1) {
        memmove(h->lcd_args, h->lcd_args+1, h->lcd_args_len-1); h->lcd_args_len -= 1;
    }
}
static void host_lcd_pixels(SimHost *h, const uint8_t *data, size_t n) {
    uint8_t *buf = NULL; size_t bn = 0;
    if (h->have_odd) {
        buf = (uint8_t*)malloc(n + 1);
        buf[0] = h->odd_byte; memcpy(buf+1, data, n); bn = n + 1;
        h->have_odd = false;
    } else {
        buf = (uint8_t*)data; bn = n;
    }
    size_t pairs = bn & ~(size_t)1;
    SDL_LockMutex(h->lock);
    int pw = host_panel_w(h), ph = host_panel_h(h);
    for (size_t i = 0; i < pairs; i += 2) {
        uint16_t p = ((uint16_t)buf[i] << 8) | buf[i+1];
        int px, py; host_lcd_map_xy(h, h->cx, h->cy, &px, &py);
        if (px >= 0 && py >= 0 && px < pw && py < ph) { h->fb565[py*pw + px] = p; h->fb_dirty = true; }
        h->cx++;
        if (h->cx > h->xe) { h->cx = h->xs; h->cy++; if (h->cy > h->ye) h->cy = h->ys; }
    }
    SDL_UnlockMutex(h->lock);
    if (bn & 1) { h->odd_byte = buf[bn-1]; h->have_odd = true; }
    if (buf != data) free(buf);
}
static void host_lcd_command(SimHost *h, uint8_t c) {
    host_lcd_flush_args(h);
    h->lcd_cmd = c; h->lcd_args_len = 0; h->have_odd = false;
    if (c == 0x01) {
        SDL_LockMutex(h->lock);
        memset(h->fb565, 0, sizeof(h->fb565));
        h->xs=0; h->xe=host_panel_w(h)-1; h->ys=0; h->ye=host_panel_h(h)-1; h->cx=0; h->cy=0; h->madctl=0; h->inverted=false; h->fb_dirty=true;
        SDL_UnlockMutex(h->lock);
    } else if (c == 0x21) { SDL_LockMutex(h->lock); h->inverted=true; h->fb_dirty=true; SDL_UnlockMutex(h->lock); }
    else if (c == 0x20) { SDL_LockMutex(h->lock); h->inverted=false; h->fb_dirty=true; SDL_UnlockMutex(h->lock); }
    else if (c == 0x2C || c == 0x3C) { h->cx = h->xs; h->cy = h->ys; }
}
static void host_handle_spi_transfer(SimHost *h, const uint8_t *tbuf, size_t tlen) {
    field_t f;
    if (find_field(tbuf, tlen, 6, &f) && f.b.n) {
        field_t ph;
        if (find_field(f.b.p, f.b.n, 3, &ph) && ph.b.n) {
            host_lcd_command(h, ph.b.p[ph.b.n - 1]);
        }
    }
    slice_t tx = {0};
    uint64_t pin_levels = 0; uint64_t rx_len = 0;
    if (find_field(tbuf, tlen, 3, &f)) tx = f.b;
    if (find_field(tbuf, tlen, 4, &f)) rx_len = f.u;
    if (find_field(tbuf, tlen, 5, &f)) pin_levels = f.u;
    if (tx.n) {
        bool dc = (pin_levels >> LCD_DC) & 1;
        if (!dc) {
            for (size_t i=0;i<tx.n;i++) host_lcd_command(h, tx.p[i]);
        } else {
            if (h->lcd_cmd == 0x2A || h->lcd_cmd == 0x2B || h->lcd_cmd == 0x36 || h->lcd_cmd == 0x3A) {
                size_t copy = tx.n; if (h->lcd_args_len + copy > sizeof(h->lcd_args)) copy = sizeof(h->lcd_args)-h->lcd_args_len;
                memcpy(h->lcd_args + h->lcd_args_len, tx.p, copy); h->lcd_args_len += copy;
                host_lcd_flush_args(h);
            } else if (h->lcd_cmd == 0x2C || h->lcd_cmd == 0x3C) {
                host_lcd_pixels(h, tx.p, tx.n);
            } else {
                size_t copy = tx.n; if (h->lcd_args_len + copy > sizeof(h->lcd_args)) copy = sizeof(h->lcd_args)-h->lcd_args_len;
                memcpy(h->lcd_args + h->lcd_args_len, tx.p, copy); h->lcd_args_len += copy;
                host_lcd_flush_args(h);
            }
        }
    }
    (void)rx_len;
}

/* --- I2C touch handling --- */
static size_t host_handle_i2c_transaction(SimHost *h, const uint8_t *tbuf, size_t tlen, uint8_t *out, size_t out_cap) {
    field_t f;
    uint64_t addr = 0, read_len = 0;
    slice_t wr = {0};
    if (find_field(tbuf, tlen, 2, &f) && f.wire == 0) addr = f.u;
    if (find_field(tbuf, tlen, 3, &f) && f.wire == 2) wr = f.b;
    if (find_field(tbuf, tlen, 4, &f) && f.wire == 0) read_len = f.u;
    if (addr != TOUCH_ADDR) return (size_t)-1;
    SDL_LockMutex(h->lock);
    uint8_t start_reg = h->touch_reg_ptr;
    if (wr.n) {
        h->touch_reg_ptr = wr.p[0];
        start_reg = h->touch_reg_ptr;
        if (wr.n > 1) {
            for (size_t i=1;i<wr.n;i++) h->touch_regs[(h->touch_reg_ptr + (uint8_t)(i-1)) & 0xFF] = wr.p[i];
        }
    }
    size_t n = 0;
    if (read_len) {
        n = (size_t)read_len;
        if (n > out_cap) n = out_cap;
        for (size_t i=0;i<n;i++) out[i] = h->touch_regs[(h->touch_reg_ptr + (uint8_t)i) & 0xFF];
        h->touch_reg_ptr = (h->touch_reg_ptr + (uint8_t)n) & 0xFF;
        /* Any read that covers reg 0x02 (touch count; the firmware reads 0x02..0x05, then 0x06) is one sample. */
        if (h->touch_down && start_reg <= 0x02 && (size_t)start_reg + n > 0x02) h->touch_reads++;
    }
    if (h->debug) {
        if (wr.n) {
            fprintf(stderr, "[i2c] touch write reg=0x%02X len=%zu", wr.p[0], wr.n);
            if (wr.n > 1) { fprintf(stderr, " data="); for (size_t i=1;i<wr.n && i<9;i++) fprintf(stderr, "%02X", wr.p[i]); }
            fprintf(stderr, "\n");
        }
        if (read_len) {
            fprintf(stderr, "[i2c] touch read  reg=0x%02X len=%zu -> ", start_reg, n);
            for (size_t i=0;i<n && i<16;i++) fprintf(stderr, "%02X", out[i]);
            fprintf(stderr, " (down=%d raw=%d,%d)\n", h->touch_down ? 1 : 0, h->touch_x, h->touch_y);
        }
        fflush(stderr);
    }
    SDL_UnlockMutex(h->lock);
    return n;
}

/* --- registration --- */
static bool host_do_register(SimHost *h) {
    uint8_t *msg=NULL,*reg=NULL; size_t ml=0,mc=0,rl=0,rc=0;
    uint8_t *spi=NULL; size_t sl=0,sc=0;
    enc_varint_field(&spi,&sl,&sc,1,LCD_CS);
    if (LCD_SCLK >= 0) enc_varint_field(&spi,&sl,&sc,2,(uint64_t)LCD_SCLK);
    if (LCD_MOSI >= 0) enc_varint_field(&spi,&sl,&sc,3,(uint64_t)LCD_MOSI);
    if (LCD_MISO >= 0) enc_varint_field(&spi,&sl,&sc,4,(uint64_t)LCD_MISO);
    uint8_t *spi_claim_msg=NULL; size_t sclm=0,sccm=0;
    enc_msg_field(&spi_claim_msg,&sclm,&sccm,3,spi,sl);

    uint8_t *i2c=NULL; size_t il=0,ic=0;
    enc_varint_field(&i2c,&il,&ic,1,TOUCH_ADDR);
    enc_varint_field(&i2c,&il,&ic,2,TOUCH_SCL);
    enc_varint_field(&i2c,&il,&ic,3,TOUCH_SDA);
    uint8_t *i2c_claim_msg=NULL; size_t i2l=0,i2c2=0;
    enc_msg_field(&i2c_claim_msg,&i2l,&i2c2,2,i2c,il);

    uint64_t watch = 0;
    int wpins[] = {LCD_DC, TOUCH_RST};
    for (int i=0;i<2;i++) if (wpins[i]>=0) watch |= (1ULL<<wpins[i]);
    uint64_t drive = 0;
    if (TOUCH_INT >= 0) drive |= (1ULL<<TOUCH_INT);
    uint8_t *gpio=NULL; size_t gl=0,gc=0;
    enc_varint_field(&gpio,&gl,&gc,1,drive);
    enc_varint_field(&gpio,&gl,&gc,2,watch);
    /* backlight pin as PWM pin: esp-emu sends PwmState (envelope field 12) for it */
    if (h->bl_pin >= 0 && h->bl_pin < 64) enc_varint_field(&gpio,&gl,&gc,3,1ULL<<h->bl_pin);
    uint8_t *gpio_claim_msg=NULL; size_t gcl=0,gcc=0;
    enc_msg_field(&gpio_claim_msg,&gcl,&gcc,1,gpio,gl);

    enc_varint_field(&reg,&rl,&rc,1,1);
    const char *name = h->cosim_name[0] ? h->cosim_name : PANEL_COSIM_NAME;
    enc_bytes_field(&reg,&rl,&rc,2,name,strlen(name));
    enc_msg_field(&reg,&rl,&rc,3,spi_claim_msg,sclm);
    enc_msg_field(&reg,&rl,&rc,3,i2c_claim_msg,i2l);
    enc_msg_field(&reg,&rl,&rc,3,gpio_claim_msg,gcl);

    enc_msg_field(&msg,&ml,&mc,4,reg,rl);
    bool ok = host_send_msg(h, msg, ml);
    free(msg); free(reg); free(spi); free(spi_claim_msg); free(i2c); free(i2c_claim_msg); free(gpio); free(gpio_claim_msg);
    return ok;
}

static bool host_send_bus_reply(SimHost *h, uint64_t cid, uint32_t op, const uint8_t *data, size_t n, bool ack) {
    uint8_t *r=NULL,*msg=NULL; size_t rl=0,rc=0,ml=0,mc=0;
    enc_varint_field(&r,&rl,&rc,1,cid);
    enc_varint_field(&r,&rl,&rc,2, ack?1:0);
    if (ack && n) enc_bytes_field(&r,&rl,&rc,3,data,n);
    enc_varint_field(&r,&rl,&rc,4,op);
    enc_msg_field(&msg,&ml,&mc,8,r,rl);
    bool ok = host_send_msg(h, msg, ml);
    free(r); free(msg); return ok;
}

static bool host_send_gpio_drive(SimHost *h, int pin, bool level) {
    if (!h || h->sock < 0) return false;
    uint8_t *d=NULL,*msg=NULL; size_t dl=0,dc=0,ml=0,mc=0;
    enc_varint_field(&d,&dl,&dc,1,(uint64_t)pin);
    enc_varint_field(&d,&dl,&dc,2, level?1:0);
    enc_msg_field(&msg,&ml,&mc,6,d,dl);
    bool ok = host_send_msg(h, msg, ml);
    free(d); free(msg); return ok;
}

static int connect_once(SimHost *h);
static int host_net_thread(void *arg) {
    SimHost *h = (SimHost*)arg;
    int reconnects_left = h->reconnect_max;
    for (;;) {
        if (!host_do_register(h)) {
            fprintf(stderr, "[cosim] register send failed: %s\n", strerror(errno));
            goto disconnected;
        }
        host_send_gpio_drive(h, TOUCH_INT, true);
        char *topic=NULL; uint8_t *payload=NULL; size_t plen=0;
        while (h->running) {
            if (!host_recv_frame(h, &topic, &payload, &plen)) {
                if (!h->running) break;
                if (errno == ETIMEDOUT) continue;
                if (errno == EINTR) continue;
                if (errno == ENOTCONN) {
                    fprintf(stderr, "[cosim] disconnected: peer closed connection\n");
                } else {
                    fprintf(stderr, "[cosim] recv failed/disconnected: %s\n", strerror(errno));
                }
                break;
            }
            reconnects_left = h->reconnect_max;
            bool interesting = (strcmp(topic,"ROUTER")==0 || topic[0]=='@' || strncmp(topic,"HW/",3)==0);
            if (strcmp(topic,"ROUTER")==0 || strncmp(topic,"HW/",3)==0 || strcmp(topic,"SIM/RESET")==0 || strcmp(topic,"SIM/CLOCK")==0 || topic[0]=='@') {
                if (strcmp(topic,"SIM/RESET")==0) {
                    SDL_LockMutex(h->lock); memset(h->fb565,0,sizeof(h->fb565)); h->fb_dirty=true; h->bl_duty_256=256; SDL_UnlockMutex(h->lock);
                }
                {
                    size_t i = 0;
                    while (i < plen) {
                        uint64_t tag; if (!read_varint(payload, plen, &i, &tag)) break;
                        uint32_t num = (uint32_t)(tag >> 3), wire = (uint32_t)(tag & 7);
                        if (wire == 0) { uint64_t v; if (!read_varint(payload, plen, &i, &v)) break; continue; }
                        if (wire == 1) { if (i + 8 > plen) break; i += 8; continue; }
                        if (wire == 5) { if (i + 4 > plen) break; i += 4; continue; }
                        if (wire != 2) break;
                        uint64_t n; if (!read_varint(payload, plen, &i, &n)) break; if (i + n > plen) break;
                        const uint8_t *sub = payload + i; size_t sl = (size_t)n; i += sl;

                        if (num == 5) { fprintf(stderr, "[cosim] register ack received\n"); continue; }
                        if (num == 12 || num == 3) {
                            /* 12 = PwmState {pin, period_ns, high_ns}; 3 = GpioLevel {pin, level} */
                            uint64_t bpin = 0, per = 0, hi = 0, lvl = 0; size_t j = 0;
                            while (j < sl) {
                                uint64_t t2; if (!read_varint(sub, sl, &j, &t2)) break;
                                uint32_t n2 = (uint32_t)(t2 >> 3), w2 = (uint32_t)(t2 & 7);
                                if (w2 != 0) break;
                                uint64_t v2; if (!read_varint(sub, sl, &j, &v2)) break;
                                if (n2 == 1) bpin = v2;
                                else if (n2 == 2) { if (num == 12) per = v2; else lvl = v2; }
                                else if (n2 == 3 && num == 12) hi = v2;
                            }
                            if ((int)bpin == h->bl_pin) {
                                int d = (num == 12) ? (per ? (int)(hi * 256 / per) : (hi ? 256 : 0)) : (lvl ? 256 : 0);
                                if (d > 256) d = 256;
                                SDL_LockMutex(h->lock);
                                int old = h->bl_duty_256; h->bl_duty_256 = d;
                                SDL_UnlockMutex(h->lock);
                                if (h->debug && old != d) { fprintf(stderr, "[sim] backlight %d/256\n", d); fflush(stderr); }
                            }
                            continue;
                        }
                        field_t sf2, sf3;
                        bool has_i2c = find_field(sub, sl, 2, &sf2) && sf2.wire == 2;
                        bool has_spi = find_field(sub, sl, 3, &sf3) && sf3.wire == 2;
                        if (has_i2c || has_spi) {
                            field_t cf; uint64_t cid = 0; bool have_cid = false;
                            if (find_field(sub, sl, 1, &cf) && cf.wire == 0) { cid = cf.u; have_cid = true; }
                            else if (find_field(sub, sl, 2, &cf) && cf.wire == 0 && !has_i2c) { cid = cf.u; have_cid = true; }
                            if (!have_cid) { if (h->debug) fprintf(stderr, "[cosim] drop bus req: missing correlation id\n"); continue; }
                            if (has_spi) {
                                field_t rxf; uint64_t rx_len = 0;
                                if (find_field(sf3.b.p, sf3.b.n, 4, &rxf) && rxf.wire == 0) rx_len = rxf.u;
                                else if (find_field(sf3.b.p, sf3.b.n, 4, &rxf)) { host_send_bus_reply(h, cid, 2, NULL, 0, false); continue; }
                                if (h->debug) {
                                    field_t txf; size_t tx_n = 0;
                                    if (find_field(sf3.b.p, sf3.b.n, 3, &txf) && txf.wire == 2) tx_n = txf.b.n;
                                    fprintf(stderr, "[spi] bus req cid=%llu rx_len=%llu tx=%zu\n",
                                            (unsigned long long)cid, (unsigned long long)rx_len, tx_n);
                                }
                                if (rx_len > 4096) { host_send_bus_reply(h, cid, 2, NULL, 0, false); continue; }
                                host_handle_spi_transfer(h, sf3.b.p, sf3.b.n);
                                if (rx_len > 0) {
                                    uint8_t *zeros = (uint8_t*)calloc((size_t)rx_len, 1);
                                    if (!zeros) { host_send_bus_reply(h, cid, 2, NULL, 0, false); continue; }
                                    host_send_bus_reply(h, cid, 2, zeros, (size_t)rx_len, true);
                                    free(zeros);
                                }
                            } else if (has_i2c) {
                                uint8_t out[256]; size_t nn = host_handle_i2c_transaction(h, sf2.b.p, sf2.b.n, out, sizeof(out));
                                if (nn == (size_t)-1) host_send_bus_reply(h, cid, 1, NULL, 0, false);
                                else host_send_bus_reply(h, cid, 1, out, nn, true);
                            }
                            continue;
                        }
                        (void)interesting;
                    }
                }
            }
            free(topic); free(payload); topic=NULL; payload=NULL;
        }
        free(topic); free(payload);
    disconnected:
        if (!h->running) break;
        if (h->sock >= 0) { shutdown(h->sock, SHUT_RDWR); close(h->sock); h->sock = -1; }
        if (reconnects_left <= 0) {
            fprintf(stderr, "[cosim] shutting down (no reconnect)\n");
            break;
        }
        reconnects_left--;
        fprintf(stderr, "[cosim] reconnecting in %d ms (%d attempt%s left)...\n",
                h->reconnect_delay_ms, reconnects_left, reconnects_left == 1 ? "" : "s");
        SDL_Delay((Uint32)h->reconnect_delay_ms);
        if (!h->running) break;
        h->sock = connect_once(h);
        if (h->sock < 0) { fprintf(stderr, "[cosim] reconnect failed: could not connect\n"); continue; }
        {
            struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 200000;
            setsockopt(h->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
        if (h->have_unix) fprintf(stderr, "Reconnected to cosim unix:%s\n", h->unix_path);
        else fprintf(stderr, "Reconnected to cosim %s:%s\n", h->host, h->port);
    }
    h->running = false;
    if (h->sock >= 0) { shutdown(h->sock, SHUT_RDWR); close(h->sock); h->sock = -1; }
    return 0;
}

static int connect_tcp(const char *host, const char *port) {
    struct addrinfo hints,*res=NULL,*p;
    memset(&hints,0,sizeof(hints)); hints.ai_socktype = SOCK_STREAM;
    for (int i=0;i<50;i++) {
        if (getaddrinfo(host, port, &hints, &res)==0) {
            for (p=res;p;p=p->ai_next) {
                int fd = socket(p->ai_family,p->ai_socktype,p->ai_protocol);
                if (fd<0) continue;
                if (connect(fd,p->ai_addr,p->ai_addrlen)==0) { freeaddrinfo(res); return fd; }
                close(fd);
            }
            freeaddrinfo(res); res=NULL;
        }
        struct timespec ts={0,100*1000*1000}; nanosleep(&ts,NULL);
    }
    return -1;
}

static int connect_unix_path(const char *path) {
#ifdef __linux__
    for (int i=0;i<50;i++) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd >= 0) {
            struct sockaddr_un addr; memset(&addr,0,sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, path, sizeof(addr.sun_path)-1);
            if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) return fd;
            close(fd);
        }
        struct timespec ts={0,100*1000*1000}; nanosleep(&ts,NULL);
    }
#else
    (void)path;
#endif
    return -1;
}

static int connect_once(SimHost *h) {
    if (h->have_unix) return connect_unix_path(h->unix_path);
    return connect_tcp(h->host, h->port);
}

bool sim_host_connect(SimHost *host) {
    if (!host) return false;
    host->sock = connect_once(host);
    if (host->sock < 0) return false;
    struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 200000;
    setsockopt(host->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (host->have_unix) fprintf(stderr, "Connected to cosim unix:%s\n", host->unix_path);
    else fprintf(stderr, "Connected to cosim %s:%s\n", host->host, host->port);
    return true;
}

void sim_host_disconnect(SimHost *host) {
    if (!host) return;
    if (host->sock >= 0) { shutdown(host->sock, SHUT_RDWR); close(host->sock); host->sock = -1; }
}

bool sim_host_start_net(SimHost *host) {
    if (!host || host->net_thread) return false;
    host->running = true;
    host->net_thread = SDL_CreateThread(host_net_thread, "sim_host_net", host);
    return host->net_thread != NULL;
}

void sim_host_stop_net(SimHost *host) {
    if (!host) return;
    host->running = false;
    if (host->sock >= 0) { shutdown(host->sock, SHUT_RDWR); }
    if (host->net_thread) {
        SDL_WaitThread(host->net_thread, NULL);
        host->net_thread = NULL;
    }
    if (host->sock >= 0) { close(host->sock); host->sock = -1; }
}

bool sim_host_net_running(const SimHost *host) {
    return host && host->running;
}

static void reset_lcd_state(SimHost *h) {
    memset(h->fb565, 0, sizeof(h->fb565));
    h->fb_dirty = true;
    h->lcd_cmd = 0; h->lcd_args_len = 0;
    h->xs = 0; h->xe = host_panel_w(h) - 1; h->ys = 0; h->ye = host_panel_h(h) - 1;
    h->cx = 0; h->cy = 0; h->madctl = 0; h->inverted = false;
    h->odd_byte = 0; h->have_odd = false;
}

SimHost *sim_host_create(const SimHostConfig *cfg) {
    SimHost *h = (SimHost*)calloc(1, sizeof(SimHost));
    if (!h) return NULL;
    SimHostConfig def; if (!cfg) { sim_host_config_init(&def); cfg = &def; }
    snprintf(h->host, sizeof(h->host), "%s", cfg_str(cfg->host, "127.0.0.1"));
    snprintf(h->port, sizeof(h->port), "%s", cfg_str(cfg->port, PANEL_COSIM_PORT));
    snprintf(h->cosim_name, sizeof(h->cosim_name), "%s", cfg_str(cfg->cosim_name, PANEL_COSIM_NAME));
    if (cfg->unix_path && *cfg->unix_path) { snprintf(h->unix_path, sizeof(h->unix_path), "%s", cfg->unix_path); h->have_unix = true; }
    if (cfg->uart_target && *cfg->uart_target) { snprintf(h->uart_target, sizeof(h->uart_target), "%s", cfg->uart_target); h->have_uart = true; }
    if (cfg->rot_state_file && *cfg->rot_state_file) { snprintf(h->rot_state_file, sizeof(h->rot_state_file), "%s", cfg->rot_state_file); h->have_rot_state = true; }
    if (cfg->board_state_file && *cfg->board_state_file) { snprintf(h->board_state_file, sizeof(h->board_state_file), "%s", cfg->board_state_file); h->have_board_state = true; }
    if (cfg->board_profile && *cfg->board_profile) { snprintf(h->board_profile, sizeof(h->board_profile), "%s", cfg->board_profile); h->have_board_profile = true; }
    h->reconnect_max = cfg->reconnect_max < 0 ? 0 : cfg->reconnect_max;
    h->reconnect_delay_ms = cfg->reconnect_delay_ms > 0 ? cfg->reconnect_delay_ms : 1000;
    h->debug = cfg->debug;
    h->hw_rot = cfg->hw_rot & 3;
    h->scale = cfg->scale > 0 ? cfg->scale : PANEL_WINDOW_SCALE;
    h->show_touch_debug = cfg->show_touch_debug;
    h->panel_native_w = clampi(cfg->lcd_w > 0 ? cfg->lcd_w : PANEL_WIDTH, 1, LCD_MAX_W);
    h->panel_native_h = clampi(cfg->lcd_h > 0 ? cfg->lcd_h : PANEL_HEIGHT, 1, LCD_MAX_H);
    h->mirror_x = cfg->mirror_x ? 1 : 0;
    h->mirror_y = cfg->mirror_y ? 1 : 0;
    h->touch_swap_xy = cfg->touch_swap_xy ? 1 : 0;
    h->touch_invert_x = cfg->touch_invert_x ? 1 : 0;
    h->touch_invert_y = cfg->touch_invert_y ? 1 : 0;
    h->touch_x_off = cfg->touch_x_off;
    h->touch_y_off = cfg->touch_y_off;
    h->sock = -1;
    h->running = true;
    h->board_idx = 0; h->view_rot = 0;
    h->bl_pin = LCD_BL; h->bl_duty_256 = 256;
    host_apply_board_defaults(h);
    sim_host_load_state(h); /* may set board_profile token from state file */
    if (h->have_board_profile && h->board_profile[0]) {
        sim_host_load_board_profile(h, h->board_profile);
    } else {
        sim_host_load_board_profile(h, "es3c28p");
    }
    /* Geometry explicit overrides from cfg (used by env/CLI handling in main via cfg values). */
    if (cfg->lcd_w > 0) h->panel_native_w = clampi(cfg->lcd_w, 1, LCD_MAX_W);
    if (cfg->lcd_h > 0) h->panel_native_h = clampi(cfg->lcd_h, 1, LCD_MAX_H);
    if (cfg->scale > 0) h->scale = cfg->scale;
    /* hw_rot/mirror/touch kept from profile+state; main reapplies explicit CLI/env fields. */
    if (cfg->hw_rot) h->hw_rot = cfg->hw_rot & 3;
    h->boot_rot = h->hw_rot;
    h->follow_madctl = (strcmp(h->board_id, "es3c28p") == 0);
    host_update_capability_summary(h);
    host_touch_update_regs(h);
    reset_lcd_state(h);
    h->lock = SDL_CreateMutex();
    h->send_lock = SDL_CreateMutex();
    if (!h->lock || !h->send_lock) { sim_host_destroy(h); return NULL; }
    return h;
}

SimHost *sim_host_create_default(void) {
    SimHostConfig cfg; sim_host_config_init(&cfg);
    return sim_host_create(&cfg);
}

void sim_host_destroy(SimHost *host) {
    if (!host) return;
    sim_host_stop_net(host);
    sim_host_video_shutdown(host);
    sim_host_disconnect(host);
    if (host->lock) SDL_DestroyMutex(host->lock);
    if (host->send_lock) SDL_DestroyMutex(host->send_lock);
    free(host);
}

bool sim_host_video_init_standalone(SimHost *host, const char *title) {
    if (!host) return false;
    if (!host->sdl_video_inited) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return false;
        }
        host->sdl_video_inited = true;
    }
    int w = host_view_w(host) * host->scale;
    int h = host_view_h(host) * host->scale;
    host->content_w = w; host->content_h = h;
    host->win = SDL_CreateWindow(title ? title : "ESP32 Simulator (cosim)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 w, h, SDL_WINDOW_RESIZABLE);
    if (!host->win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false; }
    host->ren = SDL_CreateRenderer(host->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!host->ren) host->ren = SDL_CreateRenderer(host->win, -1, SDL_RENDERER_SOFTWARE);
    if (!host->ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return false; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   /* nearest: no blur between the LCD pixels */
    host->tex = SDL_CreateTexture(host->ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, host_panel_w(host), host_panel_h(host));
    if (!host->tex) { fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError()); return false; }
    host->owns_window = true;
    return true;
}

bool sim_host_video_attach_native(SimHost *host, uintptr_t native_window_id, int width, int height) {
    if (!host || !native_window_id) return false;
    if (!host->sdl_video_inited) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return false;
        }
        host->sdl_video_inited = true;
    }
    host->win = SDL_CreateWindowFrom((void*)native_window_id);
    if (!host->win) { fprintf(stderr, "SDL_CreateWindowFrom: %s\n", SDL_GetError()); return false; }
    host->ren = SDL_CreateRenderer(host->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!host->ren) host->ren = SDL_CreateRenderer(host->win, -1, SDL_RENDERER_SOFTWARE);
    if (!host->ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return false; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   /* nearest: no blur between the LCD pixels */
    host->tex = SDL_CreateTexture(host->ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, host_panel_w(host), host_panel_h(host));
    if (!host->tex) { fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError()); return false; }
    host->owns_window = false;
    host->content_w = width > 0 ? width : host_view_w(host) * host->scale;
    host->content_h = height > 0 ? height : host_view_h(host) * host->scale;
    return true;
}

void sim_host_video_shutdown(SimHost *host) {
    if (!host) return;
    if (host->tex) { SDL_DestroyTexture(host->tex); host->tex = NULL; }
    if (host->ren) { SDL_DestroyRenderer(host->ren); host->ren = NULL; }
    if (host->win) { SDL_DestroyWindow(host->win); host->win = NULL; }
    host->owns_window = false;
    if (host->sdl_video_inited) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        host->sdl_video_inited = false;
    }
}

bool sim_host_video_ready(const SimHost *host) {
    return host && host->win && host->ren && host->tex;
}

void sim_host_handle_resize(SimHost *host, int width, int height) {
    if (!host) return;
    if (width > 0) host->content_w = width;
    if (height > 0) host->content_h = height;
}

void sim_host_handle_mouse(SimHost *host, SimHostMouseAction action, int x, int y, bool left_button) {
    if (!host) return;
    int ww = host->content_w > 0 ? host->content_w : (host->win ? (SDL_GetWindowSize(host->win, &ww, &host->content_h), ww) : host_view_w(host));
    int ch = host->content_h > 0 ? host->content_h : host_view_h(host);
    int fx, fy; host_window_to_fb(host, x, y, ww, ch, &fx, &fy);
    if (action == SIM_HOST_MOUSE_DOWN && left_button) host_touch_set(host, true, fx, fy);
    else if (action == SIM_HOST_MOUSE_UP && left_button) host_touch_set(host, false, fx, fy);
    else if (action == SIM_HOST_MOUSE_MOVE && left_button) host_touch_set(host, true, fx, fy);
}

void sim_host_handle_key(SimHost *host, int32_t sdl_keysym, bool down) {
    if (!host || !down) return;
    if (sdl_keysym == SDLK_F1) host->show_touch_debug = !host->show_touch_debug;
    else if (sdl_keysym == SDLK_r) sim_host_request_rotation_step(host);
}

void sim_host_request_quit(SimHost *host) { if (host) host->running = false; }
void sim_host_set_running(SimHost *host, bool running) { if (host) host->running = running; }
bool sim_host_running(const SimHost *host) { return host && host->running; }
int sim_host_exit_code(const SimHost *host) { return host ? host->exit_code : 0; }
void sim_host_set_debug(SimHost *host, bool debug) { if (host) host->debug = debug; }
void sim_host_set_touch_debug(SimHost *host, bool show) { if (host) host->show_touch_debug = show; }

void sim_host_update(SimHost *host) {
    if (!host) return;
    if (host->touch_pending_release && host_touch_release_due(host)) {
        host->touch_pending_release = false;
        host_touch_apply(host, false, host->touch_last_vx, host->touch_last_vy);
    }
}

bool sim_host_render(SimHost *host) {
    if (!host || !sim_host_video_ready(host)) return false;
    SDL_LockMutex(host->lock);
    bool dirty = host->fb_dirty; host->fb_dirty = false;
    bool tdown = host->touch_down;
    int tdx = host->touch_dx, tdy = host->touch_dy;
    int pw = host_panel_w(host), ph = host_panel_h(host);
    int bl = host->bl_duty_256;
    bool rot_changed = host->rot_changed; host->rot_changed = false;
    if (rot_changed) {
        /* firmware changed rotation: new texture size, empty frame (the firmware repaints) */
        SDL_DestroyTexture(host->tex);
        host->tex = SDL_CreateTexture(host->ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, pw, ph);
        dirty = true;
    }
    if (host->tex && dirty) SDL_UpdateTexture(host->tex, NULL, host->fb565, pw * sizeof(uint16_t));
    SDL_UnlockMutex(host->lock);
    if (!host->tex) { fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError()); return false; }
    if (rot_changed && host->owns_window && host->win) {
        int nw = host_view_w(host) * host->scale, nh = host_view_h(host) * host->scale;
        SDL_SetWindowSize(host->win, nw, nh);
        host->content_w = nw; host->content_h = nh;
    }

    int ww = host->content_w, wh = host->content_h;
    if (host->win) SDL_GetWindowSize(host->win, &ww, &wh);
    if (ww <= 0) ww = host_view_w(host);
    if (wh <= 0) wh = host_view_h(host);
    host->content_w = ww; host->content_h = wh;

    SDL_SetRenderDrawColor(host->ren, 20, 20, 24, 255);
    SDL_RenderClear(host->ren);
    int rdx, rdy, rdw, rdh;
    host_view_rect(host, ww, wh, &rdx, &rdy, &rdw, &rdh);
    SDL_Rect dst = {rdx, rdy, rdw, rdh};
    {   /* backlight: linear dimming of the panel only (window background and overlay stay full) */
        int v = bl < 0 ? 0 : (bl > 255 ? 255 : bl);
        SDL_SetTextureColorMod(host->tex, (Uint8)v, (Uint8)v, (Uint8)v);
    }
    SDL_RenderCopyEx(host->ren, host->tex, NULL, &dst, host->view_rot * 90.0, NULL, SDL_FLIP_NONE);

    if (host->show_touch_debug) {
        int vw = host_view_w(host), vh = host_view_h(host);
        int vdx = tdx, vdy = tdy;
        switch (host->view_rot & 3) {
            case 0: vdx = tdx; vdy = tdy; break;
            case 1: vdx = (ph - 1 - tdy); vdy = tdx; break;
            case 2: vdx = (pw - 1 - tdx); vdy = (ph - 1 - tdy); break;
            default: vdx = tdy; vdy = (pw - 1 - tdx); break;
        }
        int sx = rdx + vdx * rdw / (vw ? vw : 1) + rdw / (vw ? vw : 1) / 2;
        int sy = rdy + vdy * rdh / (vh ? vh : 1) + rdh / (vh ? vh : 1) / 2;
        SDL_SetRenderDrawBlendMode(host->ren, SDL_BLENDMODE_BLEND);
        if (tdown) {
            SDL_SetRenderDrawColor(host->ren, 255, 64, 64, 220);
            SDL_Rect r = {sx-10, sy-10, 20, 20};
            SDL_RenderDrawRect(host->ren, &r);
            SDL_RenderDrawLine(host->ren, sx-14, sy, sx+14, sy);
            SDL_RenderDrawLine(host->ren, sx, sy-14, sx, sy+14);
        } else {
            SDL_SetRenderDrawColor(host->ren, 64, 200, 255, 140);
            SDL_RenderDrawLine(host->ren, sx-6, sy, sx+6, sy);
            SDL_RenderDrawLine(host->ren, sx, sy-6, sx, sy+6);
        }
        SDL_SetRenderDrawBlendMode(host->ren, SDL_BLENDMODE_NONE);
        char tstr[160];
        snprintf(tstr, sizeof(tstr), "ESP32 Sim hw_rot=%d panel=%dx%d touch=%s [R/F1]",
                 host->hw_rot, pw, ph, tdown ? "DOWN" : "UP");
        if (host->owns_window && host->win) SDL_SetWindowTitle(host->win, tstr);
    }
    SDL_RenderPresent(host->ren);
    return true;
}

bool sim_host_poll_sdl_events(SimHost *host) {
    if (!host) return false;
    SDL_Event e;
    int ww = host->content_w, wh = host->content_h;
    if (host->win) SDL_GetWindowSize(host->win, &ww, &wh);
    if (ww <= 0) ww = host_view_w(host);
    if (wh <= 0) wh = host_view_h(host);
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) { host->running = false; }
        else if (e.type == SDL_WINDOWEVENT) {
            if (e.window.event == SDL_WINDOWEVENT_CLOSE) host->running = false;
            else if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED) {
                host->content_w = e.window.data1; host->content_h = e.window.data2;
            }
        } else if (e.type == SDL_KEYDOWN) {
            sim_host_handle_key(host, e.key.keysym.sym, true);
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            sim_host_handle_mouse(host, SIM_HOST_MOUSE_DOWN, e.button.x, e.button.y, true);
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            sim_host_handle_mouse(host, SIM_HOST_MOUSE_UP, e.button.x, e.button.y, true);
        } else if (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_LMASK)) {
            sim_host_handle_mouse(host, SIM_HOST_MOUSE_MOVE, e.motion.x, e.motion.y, true);
        }
    }
    return host->running;
}

int sim_host_step(SimHost *host, uint32_t delay_ms) {
    if (!host) return 1;
    if (host->owns_window) sim_host_poll_sdl_events(host);
    sim_host_update(host);
    sim_host_render(host);
    if (delay_ms) SDL_Delay(delay_ms);
    return host->running ? 0 : (host->exit_code ? host->exit_code : 1);
}

/* Headless evidence: the panel as the firmware drew it, as a binary PPM (--dump-fb). */
static bool host_dump_ppm(SimHost *h, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    SDL_LockMutex(h->lock);
    int pw = host_panel_w(h), ph = host_panel_h(h);
    fprintf(f, "P6\n%d %d\n255\n", pw, ph);
    for (int i = 0; i < pw * ph; i++) {
        uint16_t v = h->fb565[i];
        uint8_t px[3] = { (uint8_t)(((v >> 11) & 31) * 255 / 31), (uint8_t)(((v >> 5) & 63) * 255 / 63), (uint8_t)((v & 31) * 255 / 31) };
        fwrite(px, 1, 3, f);
    }
    SDL_UnlockMutex(h->lock);
    fclose(f);
    return true;
}

static const char *env_or_null(const char *name) {
    const char *v = getenv(name);
    return (v && *v) ? v : NULL;
}
static bool parse_bool_str(const char *s, bool defv) {
    if (!s || !*s) return defv;
    if (!strcmp(s,"1") || !strcasecmp(s,"true") || !strcasecmp(s,"yes") || !strcasecmp(s,"on")) return true;
    if (!strcmp(s,"0") || !strcasecmp(s,"false") || !strcasecmp(s,"no") || !strcasecmp(s,"off")) return false;
    return defv;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --host HOST            Cosim TCP host (default: 127.0.0.1)\n"
        "  --port PORT            Cosim TCP port (default: %s)\n"
        "  --unix PATH            Connect via Unix socket instead of TCP\n"
        "  --uart HOST:PORT       UART TCP target for firmware commands\n"
        "  --cosim-name NAME      Cosim register name (default: %s)\n"
        "  --hw-rot N             Hardware rotation 0-3\n"
        "  --rot-state FILE       Persist/read hw rotation state\n"
        "  --board-state FILE     Persist/read selected board profile id/path token\n"
        "  --board ID|PATH        Load board profile (simulator/emu/boards/*.json); default: es3c28p\n"
        "  --reconnect N          Reconnect attempts after disconnect (default: 0)\n"
        "  --lcd-w W              Native LCD width (runtime, <=%d)\n"
        "  --lcd-h H              Native LCD height (runtime, <=%d)\n"
        "  --scale N              Standalone window scale\n"
        "  --mirror-x 0|1         Panel X mirror (default: %d)\n"
        "  --mirror-y 0|1         Panel Y mirror (default: %d)\n"
        "  --touch-swap-xy 0|1    Touch swap XY (default: %d)\n"
        "  --touch-invert-x 0|1   Touch invert X (default: %d)\n"
        "  --touch-invert-y 0|1   Touch invert Y (default: %d)\n"
        "  --touch-x-off N        Touch calibration offset X (default: 0)\n"
        "  --touch-y-off N        Touch calibration offset Y (default: 0)\n"
        "  --touch-debug 0|1      Show touch debug overlay (default: 1)\n"
        "  --dump-fb FILE         Write the panel as a PPM after --dump-after seconds, then exit (headless: SDL_VIDEODRIVER=dummy)\n"
        "  --dump-after SECS      Delay for --dump-fb (default 20)\n"
        "  --debug                Verbose touch/I2C/SPI logs (-v, --debug-touch)\n"
        "  --help                 Show this help\n"
        "Env (overrides defaults; CLI overrides env): SIM_HOST, SIM_PORT, SIM_UNIX, SIM_UART,\n"
        "  SIM_COSIM_NAME, SIM_HW_ROT, SIM_ROT_STATE, SIM_BOARD_STATE, SIM_BOARD, SIM_RECONNECT, SIM_LCD_W,\n"
        "  SIM_LCD_H, SIM_SCALE, SIM_MIRROR_X, SIM_MIRROR_Y, SIM_TOUCH_SWAP_XY, SIM_TOUCH_INVERT_X,\n"
        "  SIM_TOUCH_INVERT_Y, SIM_TOUCH_X_OFF, SIM_TOUCH_Y_OFF, SIM_TOUCH_DEBUG, SIM_DEBUG\n"
        "Notes: board profiles may set runtime display/touch mapping; SPI/I2C pins, touch addr/controller\n"
        "  and bus/protocol claims remain compile-time/conditional in this host build (see --debug / logs).\n",
        prog, PANEL_COSIM_PORT, PANEL_COSIM_NAME, LCD_MAX_W, LCD_MAX_H,
        PANEL_MIRROR_X, PANEL_MIRROR_Y, PANEL_TOUCH_SWAP_XY, PANEL_TOUCH_INVERT_X, PANEL_TOUCH_INVERT_Y);
}

int sim_host_main(int argc, char **argv) {
    SimHostConfig cfg; sim_host_config_init(&cfg);
    const char *ev;
    if ((ev = env_or_null("SIM_HOST"))) cfg.host = ev;
    if ((ev = env_or_null("SIM_PORT"))) cfg.port = ev;
    if ((ev = env_or_null("SIM_UNIX"))) cfg.unix_path = ev;
    if ((ev = env_or_null("SIM_UART"))) cfg.uart_target = ev;
    if ((ev = env_or_null("SIM_COSIM_NAME"))) cfg.cosim_name = ev;
    const char *dump_path = NULL;
    int dump_after_s = 20;
    bool set_hw_rot=false,set_lcd_w=false,set_lcd_h=false,set_scale=false;
    bool set_mirror_x=false,set_mirror_y=false,set_touch_swap=false,set_touch_inv_x=false,set_touch_inv_y=false;
    bool set_txoff=false,set_tyoff=false;
    if ((ev = env_or_null("SIM_HW_ROT"))) { cfg.hw_rot = atoi(ev) & 3; set_hw_rot = true; }
    if ((ev = env_or_null("SIM_ROT_STATE"))) cfg.rot_state_file = ev;
    if ((ev = env_or_null("SIM_BOARD_STATE"))) cfg.board_state_file = ev;
    if ((ev = env_or_null("SIM_BOARD"))) cfg.board_profile = ev;
    if ((ev = env_or_null("SIM_RECONNECT"))) cfg.reconnect_max = atoi(ev);
    if ((ev = env_or_null("SIM_LCD_W"))) { cfg.lcd_w = atoi(ev); set_lcd_w = true; }
    if ((ev = env_or_null("SIM_LCD_H"))) { cfg.lcd_h = atoi(ev); set_lcd_h = true; }
    if ((ev = env_or_null("SIM_SCALE"))) { cfg.scale = atoi(ev); set_scale = true; }
    if ((ev = env_or_null("SIM_MIRROR_X"))) { cfg.mirror_x = parse_bool_str(ev, cfg.mirror_x); set_mirror_x = true; }
    if ((ev = env_or_null("SIM_MIRROR_Y"))) { cfg.mirror_y = parse_bool_str(ev, cfg.mirror_y); set_mirror_y = true; }
    if ((ev = env_or_null("SIM_TOUCH_SWAP_XY"))) { cfg.touch_swap_xy = parse_bool_str(ev, cfg.touch_swap_xy); set_touch_swap = true; }
    if ((ev = env_or_null("SIM_TOUCH_INVERT_X"))) { cfg.touch_invert_x = parse_bool_str(ev, cfg.touch_invert_x); set_touch_inv_x = true; }
    if ((ev = env_or_null("SIM_TOUCH_INVERT_Y"))) { cfg.touch_invert_y = parse_bool_str(ev, cfg.touch_invert_y); set_touch_inv_y = true; }
    if ((ev = env_or_null("SIM_TOUCH_X_OFF"))) { cfg.touch_x_off = atoi(ev); set_txoff = true; }
    if ((ev = env_or_null("SIM_TOUCH_Y_OFF"))) { cfg.touch_y_off = atoi(ev); set_tyoff = true; }
    if ((ev = env_or_null("SIM_TOUCH_DEBUG"))) cfg.show_touch_debug = parse_bool_str(ev, cfg.show_touch_debug);
    if ((ev = env_or_null("SIM_DEBUG"))) cfg.debug = parse_bool_str(ev, cfg.debug);

    int hw_rot_cli = 0; bool hw_rot_set = false;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--port") && i+1<argc) cfg.port = argv[++i];
        else if (!strcmp(argv[i],"--host") && i+1<argc) cfg.host = argv[++i];
        else if ((!strcmp(argv[i],"--unix") || !strcmp(argv[i],"--socket")) && i+1<argc) cfg.unix_path = argv[++i];
        else if ((!strcmp(argv[i],"--uart") || !strcmp(argv[i],"--uart-tcp")) && i+1<argc) cfg.uart_target = argv[++i];
        else if (!strcmp(argv[i],"--cosim-name") && i+1<argc) cfg.cosim_name = argv[++i];
        else if (!strcmp(argv[i],"--hw-rot") && i+1<argc) { hw_rot_cli = atoi(argv[++i]) & 3; hw_rot_set = true; set_hw_rot = true; }
        else if (!strcmp(argv[i],"--rot-state") && i+1<argc) cfg.rot_state_file = argv[++i];
        else if (!strcmp(argv[i],"--board-state") && i+1<argc) cfg.board_state_file = argv[++i];
        else if (!strcmp(argv[i],"--board") && i+1<argc) cfg.board_profile = argv[++i];
        else if (!strcmp(argv[i],"--reconnect") && i+1<argc) { cfg.reconnect_max = atoi(argv[++i]); if (cfg.reconnect_max < 0) cfg.reconnect_max = 0; }
        else if (!strcmp(argv[i],"--lcd-w") && i+1<argc) { cfg.lcd_w = atoi(argv[++i]); set_lcd_w = true; }
        else if (!strcmp(argv[i],"--lcd-h") && i+1<argc) { cfg.lcd_h = atoi(argv[++i]); set_lcd_h = true; }
        else if (!strcmp(argv[i],"--scale") && i+1<argc) { cfg.scale = atoi(argv[++i]); set_scale = true; }
        else if (!strcmp(argv[i],"--mirror-x") && i+1<argc) { cfg.mirror_x = parse_bool_str(argv[++i], cfg.mirror_x); set_mirror_x = true; }
        else if (!strcmp(argv[i],"--mirror-y") && i+1<argc) { cfg.mirror_y = parse_bool_str(argv[++i], cfg.mirror_y); set_mirror_y = true; }
        else if (!strcmp(argv[i],"--touch-swap-xy") && i+1<argc) { cfg.touch_swap_xy = parse_bool_str(argv[++i], cfg.touch_swap_xy); set_touch_swap = true; }
        else if (!strcmp(argv[i],"--touch-invert-x") && i+1<argc) { cfg.touch_invert_x = parse_bool_str(argv[++i], cfg.touch_invert_x); set_touch_inv_x = true; }
        else if (!strcmp(argv[i],"--touch-invert-y") && i+1<argc) { cfg.touch_invert_y = parse_bool_str(argv[++i], cfg.touch_invert_y); set_touch_inv_y = true; }
        else if (!strcmp(argv[i],"--touch-x-off") && i+1<argc) { cfg.touch_x_off = atoi(argv[++i]); set_txoff = true; }
        else if (!strcmp(argv[i],"--touch-y-off") && i+1<argc) { cfg.touch_y_off = atoi(argv[++i]); set_tyoff = true; }
        else if (!strcmp(argv[i],"--touch-debug") && i+1<argc) cfg.show_touch_debug = parse_bool_str(argv[++i], cfg.show_touch_debug);
        else if (!strcmp(argv[i],"--dump-fb") && i+1<argc) dump_path = argv[++i];
        else if (!strcmp(argv[i],"--dump-after") && i+1<argc) dump_after_s = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--debug") || !strcmp(argv[i],"--debug-touch") || !strcmp(argv[i],"-v")) cfg.debug = true;
        else if (!strcmp(argv[i],"--help") || !strcmp(argv[i],"-h")) { print_usage(argv[0]); return 0; }
        else { fprintf(stderr, "Unknown option: %s\n", argv[i]); print_usage(argv[0]); return 1; }
    }

    SimHost *host = sim_host_create(&cfg);
    if (!host) { fprintf(stderr, "sim_host_create failed\n"); return 1; }
    /* explicit CLI/env runtime overrides take precedence over profile/state defaults */
    if (set_hw_rot) { host->hw_rot = (hw_rot_set ? hw_rot_cli : cfg.hw_rot) & 3; host->boot_rot = host->hw_rot; }
    if (set_lcd_w || set_lcd_h || set_scale) {
        int w = set_lcd_w ? cfg.lcd_w : host->panel_native_w;
        int h = set_lcd_h ? cfg.lcd_h : host->panel_native_h;
        int s = set_scale ? cfg.scale : host->scale;
        sim_host_set_display(host, w, h, s, host->hw_rot);
    }
    if (set_mirror_x || set_mirror_y) sim_host_set_panel_transform(host, set_mirror_x ? cfg.mirror_x : host->mirror_x, set_mirror_y ? cfg.mirror_y : host->mirror_y);
    if (set_touch_swap || set_touch_inv_x || set_touch_inv_y || set_txoff || set_tyoff)
        sim_host_set_touch_mapping(host,
            set_touch_swap ? cfg.touch_swap_xy : host->touch_swap_xy,
            set_touch_inv_x ? cfg.touch_invert_x : host->touch_invert_x,
            set_touch_inv_y ? cfg.touch_invert_y : host->touch_invert_y,
            set_txoff ? cfg.touch_x_off : host->touch_x_off,
            set_tyoff ? cfg.touch_y_off : host->touch_y_off);
    host_update_capability_summary(host);
    sim_host_save_state(host);
    if (host->debug) sim_host_print_board_info(host);
    else fprintf(stderr, "[board] caps: %s\n", sim_host_capabilities(host));
    reset_lcd_state(host);

    if (!sim_host_video_init_standalone(host, "ESP32 Simulator (cosim)")) {
        sim_host_destroy(host); return 1;
    }
    if (!sim_host_connect(host)) {
        if (host->have_unix) fprintf(stderr, "Failed to connect to unix socket %s\n", host->unix_path);
        else fprintf(stderr, "Failed to connect to %s:%s\n", host->host, host->port);
        sim_host_destroy(host); return 1;
    }
    if (!sim_host_start_net(host)) {
        fprintf(stderr, "Failed to start network thread\n");
        sim_host_destroy(host); return 1;
    }

    uint32_t t0 = SDL_GetTicks();
    while (sim_host_running(host)) {
        sim_host_step(host, 16);
        if (dump_path && SDL_GetTicks() - t0 >= (uint32_t)dump_after_s * 1000u) {
            fprintf(stderr, host_dump_ppm(host, dump_path) ? "[dump] wrote %s\n" : "[dump] cannot write %s\n", dump_path);
            break;
        }
    }

    int code = sim_host_exit_code(host);
    sim_host_destroy(host);
    return code;
}
