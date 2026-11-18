#ifndef SIM_HOST_H
#define SIM_HOST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SimHost SimHost;

typedef struct {
    const char *host;              /* cosim TCP host (default 127.0.0.1) */
    const char *port;              /* cosim TCP port (default PANEL_COSIM_PORT) */
    const char *unix_path;         /* Unix socket path (overrides TCP when set) */
    const char *uart_target;       /* UART target host:port (informational / future use) */
    const char *rot_state_file;    /* persist/read hw rotation */
    const char *board_state_file;  /* persist/read board id/profile token */
    const char *board_profile;     /* board profile id or JSON path (simulator/emu/boards/<id>.json) */
    const char *cosim_name;        /* cosim register name (default PANEL_COSIM_NAME) */
    int reconnect_max;             /* reconnect attempts after disconnect (default 0) */
    int reconnect_delay_ms;        /* delay between reconnects (default 1000) */
    bool debug;
    int hw_rot;                    /* initial hw rotation 0-3 (may be overridden by state file) */
    int scale;                     /* window scale for standalone video (default 2) */
    bool show_touch_debug;         /* overlay/debug title */

    /* Runtime display/panel (defaults from panel_config.h / es3c28p-compatible). */
    int lcd_w;                     /* native panel width in pixels (default PANEL_WIDTH) */
    int lcd_h;                     /* native panel height in pixels (default PANEL_HEIGHT) */
    int mirror_x;                  /* presentation mirror X (default PANEL_MIRROR_X) */
    int mirror_y;                  /* presentation mirror Y (default PANEL_MIRROR_Y) */

    /* Runtime touch mapping/calibration (applied in panel raw space after view->panel). */
    int touch_swap_xy;             /* swap X/Y (default PANEL_TOUCH_SWAP_XY) */
    int touch_invert_x;            /* invert X (default PANEL_TOUCH_INVERT_X) */
    int touch_invert_y;            /* invert Y (default PANEL_TOUCH_INVERT_Y) */
    int touch_x_off;               /* additive calibration offset X (default 0) */
    int touch_y_off;               /* additive calibration offset Y (default 0) */
} SimHostConfig;

typedef enum {
    SIM_HOST_MOUSE_DOWN = 1,
    SIM_HOST_MOUSE_UP   = 2,
    SIM_HOST_MOUSE_MOVE = 3,
} SimHostMouseAction;

void sim_host_config_init(SimHostConfig *cfg);

SimHost *sim_host_create(const SimHostConfig *cfg);
SimHost *sim_host_create_default(void); /* convenience: defaults + es3c28p compat */
void sim_host_destroy(SimHost *host);

/* Load persisted board/rotation state files if configured (also applied in create). */
void sim_host_load_state(SimHost *host);
void sim_host_save_state(SimHost *host);

int sim_host_hw_rot(const SimHost *host);
int sim_host_panel_w(const SimHost *host);
int sim_host_panel_h(const SimHost *host);
int sim_host_view_w(const SimHost *host);
int sim_host_view_h(const SimHost *host);
const char *sim_host_board_id(const SimHost *host);
const char *sim_host_board_profile_path(const SimHost *host);
const char *sim_host_board_name(const SimHost *host);
const char *sim_host_board_status(const SimHost *host);

/* Runtime config getters/setters (safe before/after video init; geometry affects render/texture). */
void sim_host_get_config(const SimHost *host, SimHostConfig *out);
void sim_host_set_display(SimHost *host, int lcd_w, int lcd_h, int scale, int hw_rot);
void sim_host_set_panel_transform(SimHost *host, int mirror_x, int mirror_y);
void sim_host_set_touch_mapping(SimHost *host, int swap_xy, int invert_x, int invert_y, int x_off, int y_off);
/* Individual getters for embedders (current effective runtime values). */
void sim_host_get_display_params(const SimHost *host, int *native_w, int *native_h, int *scale, int *hw_rot, int *mirror_x, int *mirror_y);
void sim_host_get_touch_params(const SimHost *host, int *swap_xy, int *invert_x, int *invert_y, int *x_off, int *y_off);
const char *sim_host_board_display_info(const SimHost *host); /* profile metadata summary (bus/controller/hints), not runtime claims */
const char *sim_host_board_touch_info(const SimHost *host);    /* profile metadata summary */
const char *sim_host_board_input_info(const SimHost *host);    /* profile metadata summary (input/keyboard hints) */
const char *sim_host_capabilities(const SimHost *host);       /* runtime-supported vs compile-time/conditional limits */
/* Load/apply board profile (id or JSON path). Applies runtime-supported fields only; logs explicit limits. */
bool sim_host_load_board_profile(SimHost *host, const char *id_or_path);
void sim_host_print_board_info(const SimHost *host); /* detailed diagnostics to stderr */

/* Connection / network thread (cosim receive + reconnect loop). */
bool sim_host_connect(SimHost *host);
void sim_host_disconnect(SimHost *host);
bool sim_host_start_net(SimHost *host);
void sim_host_stop_net(SimHost *host);
bool sim_host_net_running(const SimHost *host);

/* Video: standalone SDL window or attach to external/native window (e.g. X11 id from GTK). */
bool sim_host_video_init_standalone(SimHost *host, const char *title);
bool sim_host_video_attach_native(SimHost *host, uintptr_t native_window_id, int width, int height);
void sim_host_video_shutdown(SimHost *host);
bool sim_host_video_ready(const SimHost *host);

/* Step/update/render for external main loops. */
void sim_host_update(SimHost *host);                 /* deferred touch release, state upkeep */
bool sim_host_render(SimHost *host);                 /* upload framebuffer texture and present */
bool sim_host_poll_sdl_events(SimHost *host);        /* poll SDL events (standalone/SDL-owned loop) */
int  sim_host_step(SimHost *host, uint32_t delay_ms);/* poll+update+render+delay; returns 0 to continue, nonzero to stop */

/* Input/window injection for embedders (coordinates in host window/content space). */
void sim_host_handle_resize(SimHost *host, int width, int height);
void sim_host_handle_mouse(SimHost *host, SimHostMouseAction action, int x, int y, bool left_button);
void sim_host_handle_key(SimHost *host, int32_t sdl_keysym, bool down);
void sim_host_request_quit(SimHost *host);
void sim_host_request_rotation_step(SimHost *host);

bool sim_host_running(const SimHost *host);
void sim_host_set_running(SimHost *host, bool running);
int sim_host_exit_code(const SimHost *host);

/* Debug/log helpers */
void sim_host_set_debug(SimHost *host, bool debug);
void sim_host_set_touch_debug(SimHost *host, bool show);

int sim_host_main(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* SIM_HOST_H */
