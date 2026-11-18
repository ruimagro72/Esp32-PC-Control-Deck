#ifndef SIM_LAUNCHER_TELEMETRY_H
#define SIM_LAUNCHER_TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TELEM_MAX_NAME    64
#define TELEM_MAX_PATH    512
#define TELEM_MAX_MSG     160
#define TELEM_MAX_TEXT    2048
#define TELEM_MAX_COUNTERS 16

/* Capability handling notes (esp-emu 1.1.0 baseline):
 * - trace: available when configured (--trace + --elf); stream/runtime access may still
 *   need host integration, but endpoint/file observability can be reported now.
 * - gdb: available when configured (--gdb/--gdb-halt); runtime attach/data still needs host.
 * - control/cosim: CLI/config and transport status observable; deep GPIO/regs/mem/perf/
 *   cycles/mips/trace-stream/gdb-runtime-data remain capability-gated. */

typedef enum {
    TELEM_RUN_STOPPED = 0,
    TELEM_RUN_RUNNING,
    TELEM_RUN_PAUSED
} telem_run_state_t;

typedef enum {
    TELEM_SUPPORT_SUPPORTED_NOW = 0,
    TELEM_SUPPORT_NEEDS_RUNTIME_HOST,
    TELEM_SUPPORT_BLOCKED_BY_EMU,
    TELEM_SUPPORT_CONDITIONAL
} telem_support_state_t;

typedef struct {
    telem_support_state_t state;
    bool available;
    char message[TELEM_MAX_MSG];      /* indisponibilidade/condição */
    char condition[TELEM_MAX_MSG];    /* versão/tool exigida quando conditional */
} telem_capability_t;

typedef struct {
    char key[TELEM_MAX_NAME];
    uint64_t value;
} telem_counter_t;

typedef struct {
    bool has_host_fps;
    double host_fps;
    bool has_frame_count;
    uint64_t frame_count;
    size_t counter_count;
    telem_counter_t counters[TELEM_MAX_COUNTERS];
} telem_metrics_t;

typedef struct {
    char board_id[TELEM_MAX_NAME];
    char board_name[TELEM_MAX_NAME];
    char firmware_name[TELEM_MAX_NAME];
    char firmware_path[TELEM_MAX_PATH];
    char profile_name[TELEM_MAX_NAME];
    char profile_path[TELEM_MAX_PATH];
} telem_target_info_t;

/* Hook payloads: opaque/light snapshots for future runtime integration. */
typedef struct {
    uint32_t gpio_mask;
    uint32_t gpio_values;
    bool valid;
} telem_gpio_snapshot_t;

typedef struct {
    uint32_t address;
    uint64_t value;
    uint32_t width_bits;
    bool valid;
} telem_register_snapshot_t;

typedef struct {
    uint64_t base_addr;
    size_t length;
    const uint8_t *data; /* optional borrowed pointer, may be NULL */
    bool valid;
} telem_memory_snapshot_t;

typedef struct {
    uint64_t cycles;
    double mips;
    double perf_score;
    bool valid;
} telem_perf_snapshot_t;

typedef struct {
    bool active;
    bool attached;
    char endpoint[TELEM_MAX_MSG]; /* e.g. host:port or pipe */
    bool valid;
} telem_gdb_snapshot_t;

typedef struct {
    int exit_code;
    bool exit_known;
    uint64_t started_ms;
    uint64_t ended_ms;
} telem_process_info_t;

typedef struct {
    char emu_log_path[TELEM_MAX_PATH];
    char host_log_path[TELEM_MAX_PATH];
} telem_log_info_t;

typedef struct {
    char path[TELEM_MAX_PATH];
    uint64_t size_bytes;
    bool size_known;
    bool active; /* file configured/emission expected */
} telem_trace_info_t;

typedef struct {
    bool enabled;
    int port;
    bool halt_on_start;
    char endpoint[TELEM_MAX_MSG];
} telem_gdb_info_t;

typedef struct {
    char transport[TELEM_MAX_MSG];  /* e.g. tcp, control-tcp, cosim-tcp */
    char endpoint[TELEM_MAX_MSG];
    char status[TELEM_MAX_MSG];     /* disconnected/configured/active/unknown */
    bool board_associated;
    char board_path[TELEM_MAX_PATH];
} telem_cosim_info_t;

typedef struct {
    const char *text; /* borrowed trace chunk/text, may be NULL */
    size_t length;
    bool valid;
} telem_trace_snapshot_t;

typedef struct {
    telem_capability_t gpio;
    telem_capability_t registers;
    telem_capability_t memory;
    telem_capability_t perf;
    telem_capability_t cycles;
    telem_capability_t mips;
    telem_capability_t trace;
    telem_capability_t gdb;
} telem_capabilities_t;

/* Optional poll callbacks (all nullable). Return true when snapshot produced/updated. */
typedef bool (*telem_poll_gpio_fn)(telem_gpio_snapshot_t *out, void *user_data);
typedef bool (*telem_poll_registers_fn)(telem_register_snapshot_t *out, void *user_data);
typedef bool (*telem_poll_memory_fn)(telem_memory_snapshot_t *out, void *user_data);
typedef bool (*telem_poll_perf_fn)(telem_perf_snapshot_t *out, void *user_data);
typedef bool (*telem_poll_trace_fn)(telem_trace_snapshot_t *out, void *user_data);
typedef bool (*telem_poll_gdb_fn)(telem_gdb_snapshot_t *out, void *user_data);

typedef struct {
    telem_poll_gpio_fn poll_gpio;
    telem_poll_registers_fn poll_registers;
    telem_poll_memory_fn poll_memory;
    telem_poll_perf_fn poll_perf;
    telem_poll_trace_fn poll_trace;
    telem_poll_gdb_fn poll_gdb;
    void *user_data;
} telem_sources_t;

typedef struct {
    telem_run_state_t run_state;
    uint64_t uptime_ms;
    telem_target_info_t target;
    telem_metrics_t metrics;
    telem_capabilities_t caps;

    telem_gpio_snapshot_t gpio;
    telem_register_snapshot_t registers;
    telem_memory_snapshot_t memory;
    telem_perf_snapshot_t perf;
    telem_trace_snapshot_t trace;
    telem_gdb_snapshot_t gdb;

    telem_sources_t sources;

    /* Available observability (module-side hooks/metadata). */
    telem_process_info_t process;
    telem_log_info_t logs;
    telem_trace_info_t trace_file;
    telem_gdb_info_t gdb_cfg;
    telem_cosim_info_t cosim;
    char control_endpoint[TELEM_MAX_MSG];
    uint64_t last_update_ms;
} telem_state_t;

/* Lifecycle */
void telem_init(telem_state_t *st);
void telem_reset(telem_state_t *st);
void telem_free(telem_state_t *st);

/* State/info setters */
void telem_set_run_state(telem_state_t *st, telem_run_state_t state);
void telem_set_uptime_ms(telem_state_t *st, uint64_t uptime_ms);
void telem_set_target_info(telem_state_t *st, const telem_target_info_t *info);
void telem_set_target_board(telem_state_t *st, const char *id, const char *name);
void telem_set_target_firmware(telem_state_t *st, const char *name, const char *path);
void telem_set_target_profile(telem_state_t *st, const char *name, const char *path);

/* Metrics setters */
void telem_set_host_fps(telem_state_t *st, double fps);
void telem_clear_host_fps(telem_state_t *st);
void telem_set_frame_count(telem_state_t *st, uint64_t count);
int telem_set_counter(telem_state_t *st, const char *key, uint64_t value);
void telem_clear_counters(telem_state_t *st);

/* Capability/snapshot setters */
void telem_set_capability(telem_capability_t *cap, telem_support_state_t state,
                          bool available, const char *message, const char *condition);
void telem_set_capabilities_defaults(telem_state_t *st);

void telem_set_gpio_snapshot(telem_state_t *st, const telem_gpio_snapshot_t *snap);
void telem_set_register_snapshot(telem_state_t *st, const telem_register_snapshot_t *snap);
void telem_set_memory_snapshot(telem_state_t *st, const telem_memory_snapshot_t *snap);
void telem_set_perf_snapshot(telem_state_t *st, const telem_perf_snapshot_t *snap);
void telem_set_trace_snapshot(telem_state_t *st, const telem_trace_snapshot_t *snap);
void telem_set_gdb_snapshot(telem_state_t *st, const telem_gdb_snapshot_t *snap);

/* Update from sources: invokes configured poll callbacks for hooks/capabilities. */
void telem_set_sources(telem_state_t *st, const telem_sources_t *sources);
int telem_update_from_sources(telem_state_t *st);

/* Available-source hooks from process/command/config/log-file metadata. */
void telem_set_process_info(telem_state_t *st, const telem_process_info_t *info);
void telem_set_process_running(telem_state_t *st, bool running, bool reset_exit);
void telem_set_process_exit(telem_state_t *st, int exit_code);
void telem_set_log_paths(telem_state_t *st, const char *emu_log_path, const char *host_log_path);
void telem_set_trace_file(telem_state_t *st, const char *path, bool active, bool size_known, uint64_t size_bytes);
void telem_set_gdb_config(telem_state_t *st, bool enabled, bool halt_on_start, int port, const char *endpoint);
void telem_set_cosim_info(telem_state_t *st, const char *transport, const char *endpoint,
                          const char *status, const char *board_path, bool board_associated);
void telem_set_control_endpoint(telem_state_t *st, const char *endpoint);
void telem_note_update(telem_state_t *st, uint64_t now_ms);
void telem_apply_emu_config(telem_state_t *st,
                            bool has_elf,
                            bool has_trace, const char *trace_path,
                            bool has_gdb, int gdb_port, const char *gdb_host, bool gdb_halt,
                            bool has_control_tcp, const char *control_endpoint,
                            bool has_cosim_board, const char *cosim_board_path,
                            bool has_cosim_tcp, const char *cosim_tcp_endpoint,
                            const char *emu_log_path);
void telem_update_capabilities_from_observability(telem_state_t *st);

/* Formatting/snapshots for UI/log */
int telem_format_panel(const telem_state_t *st, char *buf, size_t buflen);
int telem_format_log_summary(const telem_state_t *st, char *buf, size_t buflen);
const char *telem_run_state_str(telem_run_state_t state);
const char *telem_support_state_str(telem_support_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* SIM_LAUNCHER_TELEMETRY_H */
