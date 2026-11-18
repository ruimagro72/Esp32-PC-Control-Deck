#include "telemetry.h"
#include "i18n.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void telem_copy_str(char *dst, size_t dstsz, const char *src) {
    if (!dst || dstsz == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dstsz, "%s", src);
}

void telem_set_capability(telem_capability_t *cap, telem_support_state_t state,
                          bool available, const char *message, const char *condition) {
    if (!cap) return;
    cap->state = state;
    cap->available = available;
    telem_copy_str(cap->message, sizeof(cap->message), message);
    telem_copy_str(cap->condition, sizeof(cap->condition), condition);
}

void telem_set_capabilities_defaults(telem_state_t *st) {
    if (!st) return;
    telem_set_capability(&st->caps.gpio, TELEM_SUPPORT_NEEDS_RUNTIME_HOST, false,
                         _("GPIO unavailable: requires a connected runtime/host"), NULL);
    telem_set_capability(&st->caps.registers, TELEM_SUPPORT_NEEDS_RUNTIME_HOST, false,
                         _("Registers unavailable: requires a connected runtime/host"), NULL);
    telem_set_capability(&st->caps.memory, TELEM_SUPPORT_NEEDS_RUNTIME_HOST, false,
                         _("Memory unavailable: requires a connected runtime/host"), NULL);
    telem_set_capability(&st->caps.perf, TELEM_SUPPORT_NEEDS_RUNTIME_HOST, false,
                         _("Perf unavailable: requires a connected runtime/host"), NULL);
    telem_set_capability(&st->caps.cycles, TELEM_SUPPORT_CONDITIONAL, false,
                         _("Cycles unavailable without instrumentation/an exposed counter"), "esp-emu>=1.1.0 (runtime/host)");
    telem_set_capability(&st->caps.mips, TELEM_SUPPORT_CONDITIONAL, false,
                         _("MIPS unavailable without an exposed cycle counter"), "esp-emu>=1.1.0 (runtime/host)");
    telem_set_capability(&st->caps.trace, TELEM_SUPPORT_CONDITIONAL, false,
                         _("Chrome trace available with --trace <FILE> + --elf; stream/runtime deep data requires host"), "esp-emu>=1.1.0 (--trace --elf)");
    telem_set_capability(&st->caps.gdb, TELEM_SUPPORT_CONDITIONAL, false,
                         _("GDB stub available with --gdb <PORT> [--gdb-halt]; runtime data requires host/client"), "esp-emu>=1.1.0 (--gdb)");
}

void telem_init(telem_state_t *st) {
    if (!st) return;
    memset(st, 0, sizeof(*st));
    telem_set_capabilities_defaults(st);
}

void telem_reset(telem_state_t *st) {
    telem_sources_t sources;
    if (!st) return;
    sources = st->sources; /* preserva callbacks configurados */
    telem_init(st);
    st->sources = sources;
}

void telem_free(telem_state_t *st) {
    if (!st) return;
    memset(st, 0, sizeof(*st));
}

void telem_set_run_state(telem_state_t *st, telem_run_state_t state) {
    if (!st) return;
    st->run_state = state;
}

void telem_set_uptime_ms(telem_state_t *st, uint64_t uptime_ms) {
    if (!st) return;
    st->uptime_ms = uptime_ms;
}

void telem_set_target_info(telem_state_t *st, const telem_target_info_t *info) {
    if (!st) return;
    if (!info) {
        memset(&st->target, 0, sizeof(st->target));
        return;
    }
    st->target = *info;
}

void telem_set_target_board(telem_state_t *st, const char *id, const char *name) {
    if (!st) return;
    telem_copy_str(st->target.board_id, sizeof(st->target.board_id), id);
    telem_copy_str(st->target.board_name, sizeof(st->target.board_name), name);
}

void telem_set_target_firmware(telem_state_t *st, const char *name, const char *path) {
    if (!st) return;
    telem_copy_str(st->target.firmware_name, sizeof(st->target.firmware_name), name);
    telem_copy_str(st->target.firmware_path, sizeof(st->target.firmware_path), path);
}

void telem_set_target_profile(telem_state_t *st, const char *name, const char *path) {
    if (!st) return;
    telem_copy_str(st->target.profile_name, sizeof(st->target.profile_name), name);
    telem_copy_str(st->target.profile_path, sizeof(st->target.profile_path), path);
}

void telem_set_host_fps(telem_state_t *st, double fps) {
    if (!st) return;
    st->metrics.has_host_fps = true;
    st->metrics.host_fps = fps;
}

void telem_clear_host_fps(telem_state_t *st) {
    if (!st) return;
    st->metrics.has_host_fps = false;
    st->metrics.host_fps = 0.0;
}

void telem_set_frame_count(telem_state_t *st, uint64_t count) {
    if (!st) return;
    st->metrics.has_frame_count = true;
    st->metrics.frame_count = count;
}

int telem_set_counter(telem_state_t *st, const char *key, uint64_t value) {
    size_t i;
    if (!st || !key || !*key) return -1;
    for (i = 0; i < st->metrics.counter_count; i++) {
        if (strncmp(st->metrics.counters[i].key, key, TELEM_MAX_NAME) == 0) {
            st->metrics.counters[i].value = value;
            return 0;
        }
    }
    if (st->metrics.counter_count >= TELEM_MAX_COUNTERS) return -2;
    telem_copy_str(st->metrics.counters[st->metrics.counter_count].key,
                   sizeof(st->metrics.counters[st->metrics.counter_count].key), key);
    st->metrics.counters[st->metrics.counter_count].value = value;
    st->metrics.counter_count++;
    return 0;
}

void telem_clear_counters(telem_state_t *st) {
    if (!st) return;
    st->metrics.counter_count = 0;
    memset(st->metrics.counters, 0, sizeof(st->metrics.counters));
}

void telem_set_gpio_snapshot(telem_state_t *st, const telem_gpio_snapshot_t *snap) {
    if (!st) return;
    if (snap) st->gpio = *snap;
    else memset(&st->gpio, 0, sizeof(st->gpio));
}

void telem_set_register_snapshot(telem_state_t *st, const telem_register_snapshot_t *snap) {
    if (!st) return;
    if (snap) st->registers = *snap;
    else memset(&st->registers, 0, sizeof(st->registers));
}

void telem_set_memory_snapshot(telem_state_t *st, const telem_memory_snapshot_t *snap) {
    if (!st) return;
    if (snap) st->memory = *snap;
    else memset(&st->memory, 0, sizeof(st->memory));
}

void telem_set_perf_snapshot(telem_state_t *st, const telem_perf_snapshot_t *snap) {
    if (!st) return;
    if (snap) st->perf = *snap;
    else memset(&st->perf, 0, sizeof(st->perf));
}

void telem_set_trace_snapshot(telem_state_t *st, const telem_trace_snapshot_t *snap) {
    if (!st) return;
    if (snap) st->trace = *snap;
    else memset(&st->trace, 0, sizeof(st->trace));
}

void telem_set_gdb_snapshot(telem_state_t *st, const telem_gdb_snapshot_t *snap) {
    if (!st) return;
    if (snap) st->gdb = *snap;
    else memset(&st->gdb, 0, sizeof(st->gdb));
}

void telem_set_process_info(telem_state_t *st, const telem_process_info_t *info) {
    if (!st) return;
    if (info) st->process = *info;
    else memset(&st->process, 0, sizeof(st->process));
}

void telem_set_process_running(telem_state_t *st, bool running, bool reset_exit) {
    if (!st) return;
    telem_set_run_state(st, running ? TELEM_RUN_RUNNING : TELEM_RUN_STOPPED);
    if (reset_exit) {
        st->process.exit_known = false;
        st->process.exit_code = 0;
        st->process.ended_ms = 0;
    }
}

void telem_set_process_exit(telem_state_t *st, int exit_code) {
    if (!st) return;
    st->process.exit_code = exit_code;
    st->process.exit_known = true;
    st->run_state = TELEM_RUN_STOPPED;
}

void telem_set_log_paths(telem_state_t *st, const char *emu_log_path, const char *host_log_path) {
    if (!st) return;
    if (emu_log_path) telem_copy_str(st->logs.emu_log_path, sizeof(st->logs.emu_log_path), emu_log_path);
    if (host_log_path) telem_copy_str(st->logs.host_log_path, sizeof(st->logs.host_log_path), host_log_path);
}

void telem_set_trace_file(telem_state_t *st, const char *path, bool active, bool size_known, uint64_t size_bytes) {
    if (!st) return;
    if (path) telem_copy_str(st->trace_file.path, sizeof(st->trace_file.path), path);
    st->trace_file.active = active;
    st->trace_file.size_known = size_known;
    st->trace_file.size_bytes = size_known ? size_bytes : 0;
    telem_update_capabilities_from_observability(st);
}

void telem_set_gdb_config(telem_state_t *st, bool enabled, bool halt_on_start, int port, const char *endpoint) {
    if (!st) return;
    st->gdb_cfg.enabled = enabled;
    st->gdb_cfg.halt_on_start = halt_on_start;
    st->gdb_cfg.port = port;
    telem_copy_str(st->gdb_cfg.endpoint, sizeof(st->gdb_cfg.endpoint), endpoint);
    if (endpoint) telem_copy_str(st->gdb.endpoint, sizeof(st->gdb.endpoint), endpoint);
    st->gdb.active = enabled;
    telem_update_capabilities_from_observability(st);
}

void telem_set_cosim_info(telem_state_t *st, const char *transport, const char *endpoint,
                          const char *status, const char *board_path, bool board_associated) {
    if (!st) return;
    telem_copy_str(st->cosim.transport, sizeof(st->cosim.transport), transport);
    telem_copy_str(st->cosim.endpoint, sizeof(st->cosim.endpoint), endpoint);
    telem_copy_str(st->cosim.status, sizeof(st->cosim.status), status);
    telem_copy_str(st->cosim.board_path, sizeof(st->cosim.board_path), board_path);
    st->cosim.board_associated = board_associated || (board_path && *board_path);
}

void telem_set_control_endpoint(telem_state_t *st, const char *endpoint) {
    if (!st) return;
    telem_copy_str(st->control_endpoint, sizeof(st->control_endpoint), endpoint);
}

void telem_note_update(telem_state_t *st, uint64_t now_ms) {
    if (!st) return;
    st->last_update_ms = now_ms;
}

static void telem_format_endpoint(char *out, size_t outsz, const char *host, int port) {
    if (!out || outsz == 0) return;
    if (port <= 0) { out[0] = '\0'; return; }
    snprintf(out, outsz, "%s:%d", (host && *host) ? host : "127.0.0.1", port);
}

void telem_apply_emu_config(telem_state_t *st,
                            bool has_elf,
                            bool has_trace, const char *trace_path,
                            bool has_gdb, int gdb_port, const char *gdb_host, bool gdb_halt,
                            bool has_control_tcp, const char *control_endpoint,
                            bool has_cosim_board, const char *cosim_board_path,
                            bool has_cosim_tcp, const char *cosim_tcp_endpoint,
                            const char *emu_log_path) {
    if (!st) return;
    if (has_trace) telem_set_trace_file(st, trace_path, true, st->trace_file.size_known, st->trace_file.size_bytes);
    if (has_gdb) {
        char ep[TELEM_MAX_MSG];
        if (gdb_host && strchr(gdb_host, ':')) telem_copy_str(ep, sizeof(ep), gdb_host);
        else telem_format_endpoint(ep, sizeof(ep), gdb_host, gdb_port);
        telem_set_gdb_config(st, true, gdb_halt, gdb_port, ep);
    }
    if (has_control_tcp) {
        telem_set_control_endpoint(st, control_endpoint);
        if (!st->cosim.transport[0]) telem_set_cosim_info(st, "control-tcp", control_endpoint, "configured", st->cosim.board_path, st->cosim.board_associated);
    }
    if (has_cosim_board || cosim_board_path) {
        telem_set_cosim_info(st, st->cosim.transport[0] ? st->cosim.transport : "cosim-board",
                             has_cosim_tcp ? cosim_tcp_endpoint : st->cosim.endpoint,
                             st->cosim.status[0] ? st->cosim.status : "configured",
                             cosim_board_path, has_cosim_board || (cosim_board_path && *cosim_board_path));
    }
    if (has_cosim_tcp) {
        telem_set_cosim_info(st, "cosim-tcp", cosim_tcp_endpoint,
                             st->cosim.status[0] ? st->cosim.status : "configured",
                             st->cosim.board_path, st->cosim.board_associated);
    }
    if (emu_log_path) telem_set_log_paths(st, emu_log_path, NULL);

    /* Capability refinement from config (honest: configured vs runtime stream/data). */
    if (has_trace) {
        if (has_elf) {
            telem_set_capability(&st->caps.trace, TELEM_SUPPORT_SUPPORTED_NOW, true,
                                 _("Chrome trace configured (--trace) with ELF; file/state observable"), "esp-emu>=1.1.0 (--trace --elf)");
        } else {
            telem_set_capability(&st->caps.trace, TELEM_SUPPORT_CONDITIONAL, false,
                                 _("Trace needs --elf for full context/timeline; runtime stream needs host"), "esp-emu>=1.1.0 (--trace --elf)");
        }
    }
    if (has_gdb) {
        telem_set_capability(&st->caps.gdb, TELEM_SUPPORT_SUPPORTED_NOW, true,
                             _("GDB stub configured (--gdb); runtime attach/data needs a GDB client/host"), "esp-emu>=1.1.0 (--gdb)");
    }
    telem_update_capabilities_from_observability(st);
}

void telem_update_capabilities_from_observability(telem_state_t *st) {
    if (!st) return;
    /* Keep deep runtime-data hooks capability-gated unless snapshots prove availability. */
    if (!st->gpio.valid && st->caps.gpio.state == TELEM_SUPPORT_SUPPORTED_NOW && !st->sources.poll_gpio) {
        telem_set_capability(&st->caps.gpio, TELEM_SUPPORT_NEEDS_RUNTIME_HOST, false,
                             _("GPIO deep data unavailable: requires a connected runtime/host"), NULL);
    }
    if (!st->registers.valid && st->caps.registers.state == TELEM_SUPPORT_SUPPORTED_NOW && !st->sources.poll_registers) {
        telem_set_capability(&st->caps.registers, TELEM_SUPPORT_NEEDS_RUNTIME_HOST, false,
                             _("Registers unavailable: requires a connected runtime/host"), NULL);
    }
    if (!st->memory.valid && st->caps.memory.state == TELEM_SUPPORT_SUPPORTED_NOW && !st->sources.poll_memory) {
        telem_set_capability(&st->caps.memory, TELEM_SUPPORT_NEEDS_RUNTIME_HOST, false,
                             _("Memory unavailable: requires a connected runtime/host"), NULL);
    }
    if (!st->perf.valid && st->caps.perf.state == TELEM_SUPPORT_SUPPORTED_NOW && !st->sources.poll_perf) {
        telem_set_capability(&st->caps.perf, TELEM_SUPPORT_NEEDS_RUNTIME_HOST, false,
                             _("Perf/cycles/MIPS unavailable: requires a connected runtime/host"), "esp-emu>=1.1.0 (runtime/host)");
        telem_set_capability(&st->caps.cycles, TELEM_SUPPORT_CONDITIONAL, false,
                             _("Cycles unavailable without instrumentation/an exposed counter"), "esp-emu>=1.1.0 (runtime/host)");
        telem_set_capability(&st->caps.mips, TELEM_SUPPORT_CONDITIONAL, false,
                             _("MIPS unavailable without an exposed cycle counter"), "esp-emu>=1.1.0 (runtime/host)");
    }
    if (st->trace_file.active && st->trace_file.path[0] && st->caps.trace.state != TELEM_SUPPORT_SUPPORTED_NOW) {
        telem_set_capability(&st->caps.trace, TELEM_SUPPORT_CONDITIONAL, true,
                             _("Trace configured (file/endpoint observable); runtime deep stream requires host"), "esp-emu>=1.1.0 (--trace)");
    }
    if (!st->trace.valid && st->caps.trace.state == TELEM_SUPPORT_SUPPORTED_NOW && st->sources.poll_trace == NULL && !st->trace_file.active) {
        telem_set_capability(&st->caps.trace, TELEM_SUPPORT_CONDITIONAL, false,
                             _("Chrome trace available with --trace <FILE> + --elf; stream/runtime deep data requires host"), "esp-emu>=1.1.0 (--trace --elf)");
    }
    if (st->gdb_cfg.enabled && st->caps.gdb.state != TELEM_SUPPORT_SUPPORTED_NOW) {
        telem_set_capability(&st->caps.gdb, TELEM_SUPPORT_CONDITIONAL, true,
                             _("GDB endpoint configured; runtime attach/data requires a client/host"), "esp-emu>=1.1.0 (--gdb)");
    }
    if (!st->gdb.valid && st->caps.gdb.state == TELEM_SUPPORT_SUPPORTED_NOW && st->sources.poll_gdb == NULL && !st->gdb_cfg.enabled) {
        telem_set_capability(&st->caps.gdb, TELEM_SUPPORT_CONDITIONAL, false,
                             _("GDB stub available with --gdb <PORT> [--gdb-halt]; runtime data requires host/client"), "esp-emu>=1.1.0 (--gdb)");
    }
    if (st->gdb_cfg.enabled && st->gdb_cfg.endpoint[0] && !st->gdb.endpoint[0]) {
        telem_copy_str(st->gdb.endpoint, sizeof(st->gdb.endpoint), st->gdb_cfg.endpoint);
    }
}

void telem_set_sources(telem_state_t *st, const telem_sources_t *sources) {
    if (!st) return;
    if (sources) st->sources = *sources;
    else memset(&st->sources, 0, sizeof(st->sources));
}

int telem_update_from_sources(telem_state_t *st) {
    int updates = 0;
    if (!st) return -1;

    if (st->sources.poll_gpio) {
        telem_gpio_snapshot_t snap;
        memset(&snap, 0, sizeof(snap));
        if (st->sources.poll_gpio(&snap, st->sources.user_data)) {
            st->gpio = snap;
            st->caps.gpio.available = snap.valid;
            if (snap.valid) {
                st->caps.gpio.state = TELEM_SUPPORT_SUPPORTED_NOW;
                st->caps.gpio.message[0] = '\0';
            }
            updates++;
        }
    }
    if (st->sources.poll_registers) {
        telem_register_snapshot_t snap;
        memset(&snap, 0, sizeof(snap));
        if (st->sources.poll_registers(&snap, st->sources.user_data)) {
            st->registers = snap;
            st->caps.registers.available = snap.valid;
            if (snap.valid) {
                st->caps.registers.state = TELEM_SUPPORT_SUPPORTED_NOW;
                st->caps.registers.message[0] = '\0';
            }
            updates++;
        }
    }
    if (st->sources.poll_memory) {
        telem_memory_snapshot_t snap;
        memset(&snap, 0, sizeof(snap));
        if (st->sources.poll_memory(&snap, st->sources.user_data)) {
            st->memory = snap;
            st->caps.memory.available = snap.valid;
            if (snap.valid) {
                st->caps.memory.state = TELEM_SUPPORT_SUPPORTED_NOW;
                st->caps.memory.message[0] = '\0';
            }
            updates++;
        }
    }
    if (st->sources.poll_perf) {
        telem_perf_snapshot_t snap;
        memset(&snap, 0, sizeof(snap));
        if (st->sources.poll_perf(&snap, st->sources.user_data)) {
            st->perf = snap;
            st->caps.perf.available = snap.valid;
            st->caps.cycles.available = snap.valid;
            st->caps.mips.available = snap.valid;
            if (snap.valid) {
                st->caps.perf.state = TELEM_SUPPORT_SUPPORTED_NOW;
                st->caps.cycles.state = TELEM_SUPPORT_SUPPORTED_NOW;
                st->caps.mips.state = TELEM_SUPPORT_SUPPORTED_NOW;
                st->caps.perf.message[0] = '\0';
                st->caps.cycles.message[0] = '\0';
                st->caps.mips.message[0] = '\0';
            }
            updates++;
        }
    }
    if (st->sources.poll_trace) {
        telem_trace_snapshot_t snap;
        memset(&snap, 0, sizeof(snap));
        if (st->sources.poll_trace(&snap, st->sources.user_data)) {
            st->trace = snap;
            if (snap.valid) {
                st->caps.trace.available = true;
                st->caps.trace.state = TELEM_SUPPORT_SUPPORTED_NOW;
                st->caps.trace.message[0] = '\0';
            }
            updates++;
        }
    }
    if (st->sources.poll_gdb) {
        telem_gdb_snapshot_t snap;
        memset(&snap, 0, sizeof(snap));
        if (st->sources.poll_gdb(&snap, st->sources.user_data)) {
            st->gdb = snap;
            if (snap.valid) {
                st->caps.gdb.available = true;
                st->caps.gdb.state = TELEM_SUPPORT_SUPPORTED_NOW;
                st->caps.gdb.message[0] = '\0';
            }
            updates++;
        }
    }
    telem_update_capabilities_from_observability(st);
    return updates;
}

const char *telem_run_state_str(telem_run_state_t state) {
    switch (state) {
        case TELEM_RUN_RUNNING: return "running";
        case TELEM_RUN_PAUSED:  return "paused";
        case TELEM_RUN_STOPPED:
        default:                return "stopped";
    }
}

const char *telem_support_state_str(telem_support_state_t state) {
    switch (state) {
        case TELEM_SUPPORT_SUPPORTED_NOW:     return "supported-now";
        case TELEM_SUPPORT_NEEDS_RUNTIME_HOST:return "needs-runtime-host";
        case TELEM_SUPPORT_BLOCKED_BY_EMU:    return "blocked-by-emu";
        case TELEM_SUPPORT_CONDITIONAL:       return "conditional(version/tool)";
        default:                              return "unknown";
    }
}

static int telem_append(char *buf, size_t buflen, size_t *off, const char *fmt, ...) {
    va_list ap;
    int n;
    if (!buf || buflen == 0 || !off || *off >= buflen) return -1;
    va_start(ap, fmt);
    n = vsnprintf(buf + *off, buflen - *off, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if ((size_t)n >= buflen - *off) {
        *off = buflen - 1;
        return -1;
    }
    *off += (size_t)n;
    return n;
}

static void telem_format_cap_line(const telem_capability_t *cap, const char *name,
                                  char *buf, size_t buflen, size_t *off) {
    if (!cap) return;
    telem_append(buf, buflen, off, "  - %s: %s avail=%s",
                 name, telem_support_state_str(cap->state), cap->available ? "yes" : "no");
    if (cap->condition[0]) telem_append(buf, buflen, off, " cond=%s", cap->condition);
    if (cap->message[0]) telem_append(buf, buflen, off, " msg=\"%s\"", cap->message);
    telem_append(buf, buflen, off, "\n");
}

int telem_format_panel(const telem_state_t *st, char *buf, size_t buflen) {
    size_t off = 0;
    size_t i;
    if (!st || !buf || buflen == 0) return -1;
    buf[0] = '\0';
    telem_append(buf, buflen, &off, "telemetry.run=%s uptime_ms=%" PRIu64 " last_update_ms=%" PRIu64 "\n",
                 telem_run_state_str(st->run_state), st->uptime_ms, st->last_update_ms);
    telem_append(buf, buflen, &off, "process.exit_known=%s exit_code=%d started_ms=%" PRIu64 " ended_ms=%" PRIu64 "\n",
                 st->process.exit_known ? "yes" : "no", st->process.exit_code,
                 st->process.started_ms, st->process.ended_ms);
    telem_append(buf, buflen, &off, "target.board=%s(%s)\n",
                 st->target.board_id[0] ? st->target.board_id : "-",
                 st->target.board_name[0] ? st->target.board_name : "-");
    telem_append(buf, buflen, &off, "target.firmware=%s path=%s\n",
                 st->target.firmware_name[0] ? st->target.firmware_name : "-",
                 st->target.firmware_path[0] ? st->target.firmware_path : "-");
    telem_append(buf, buflen, &off, "target.profile=%s path=%s\n",
                 st->target.profile_name[0] ? st->target.profile_name : "-",
                 st->target.profile_path[0] ? st->target.profile_path : "-");
    telem_append(buf, buflen, &off, "logs.emu=%s\n", st->logs.emu_log_path[0] ? st->logs.emu_log_path : "-");
    telem_append(buf, buflen, &off, "logs.host=%s\n", st->logs.host_log_path[0] ? st->logs.host_log_path : "-");
    telem_append(buf, buflen, &off, "trace.file=%s active=%s size=%s",
                 st->trace_file.path[0] ? st->trace_file.path : "-",
                 st->trace_file.active ? "yes" : "no",
                 st->trace_file.size_known ? "" : "n/a");
    if (st->trace_file.size_known)
        telem_append(buf, buflen, &off, "%" PRIu64 "\n", st->trace_file.size_bytes);
    else
        telem_append(buf, buflen, &off, "\n");
    telem_append(buf, buflen, &off, "gdb.cfg.enabled=%s halt=%s port=%d endpoint=%s\n",
                 st->gdb_cfg.enabled ? "yes" : "no",
                 st->gdb_cfg.halt_on_start ? "yes" : "no",
                 st->gdb_cfg.port,
                 st->gdb_cfg.endpoint[0] ? st->gdb_cfg.endpoint : (st->gdb.endpoint[0] ? st->gdb.endpoint : "-"));
    telem_append(buf, buflen, &off, "cosim.transport=%s endpoint=%s status=%s board=%s assoc=%s\n",
                 st->cosim.transport[0] ? st->cosim.transport : "-",
                 st->cosim.endpoint[0] ? st->cosim.endpoint : "-",
                 st->cosim.status[0] ? st->cosim.status : "unknown",
                 st->cosim.board_path[0] ? st->cosim.board_path : "-",
                 st->cosim.board_associated ? "yes" : "no");
    telem_append(buf, buflen, &off, "control.endpoint=%s\n", st->control_endpoint[0] ? st->control_endpoint : "-");
    if (st->metrics.has_host_fps)
        telem_append(buf, buflen, &off, "metrics.host_fps=%.2f\n", st->metrics.host_fps);
    else
        telem_append(buf, buflen, &off, "metrics.host_fps=n/a\n");
    if (st->metrics.has_frame_count)
        telem_append(buf, buflen, &off, "metrics.frame_count=%" PRIu64 "\n", st->metrics.frame_count);
    else
        telem_append(buf, buflen, &off, "metrics.frame_count=n/a\n");
    for (i = 0; i < st->metrics.counter_count; i++) {
        telem_append(buf, buflen, &off, "metrics.counter.%s=%" PRIu64 "\n",
                     st->metrics.counters[i].key, st->metrics.counters[i].value);
    }
    telem_append(buf, buflen, &off, "capabilities:\n");
    telem_format_cap_line(&st->caps.gpio, "gpio", buf, buflen, &off);
    telem_format_cap_line(&st->caps.registers, "registers", buf, buflen, &off);
    telem_format_cap_line(&st->caps.memory, "memory", buf, buflen, &off);
    telem_format_cap_line(&st->caps.perf, "perf", buf, buflen, &off);
    telem_format_cap_line(&st->caps.cycles, "cycles", buf, buflen, &off);
    telem_format_cap_line(&st->caps.mips, "mips", buf, buflen, &off);
    telem_format_cap_line(&st->caps.trace, "trace", buf, buflen, &off);
    telem_format_cap_line(&st->caps.gdb, "gdb", buf, buflen, &off);
    return (int)off;
}

int telem_format_log_summary(const telem_state_t *st, char *buf, size_t buflen) {
    size_t off = 0;
    if (!st || !buf || buflen == 0) return -1;
    buf[0] = '\0';
    telem_append(buf, buflen, &off,
                 "state=%s uptime_ms=%" PRIu64 " exit=%s/%d board=%s fw=%s fps=%s frames=%s counters=%zu "
                 "trace=%s(%s) gdb=%s(%s) cosim=%s logs=%s/%s "
                 "gpio=%s regs=%s mem=%s perf=%s cycles=%s mips=%s",
                 telem_run_state_str(st->run_state), st->uptime_ms,
                 st->process.exit_known ? "yes" : "no", st->process.exit_code,
                 st->target.board_id[0] ? st->target.board_id : "-",
                 st->target.firmware_name[0] ? st->target.firmware_name : "-",
                 st->metrics.has_host_fps ? "set" : "n/a",
                 st->metrics.has_frame_count ? "set" : "n/a",
                 st->metrics.counter_count,
                 st->trace_file.path[0] ? st->trace_file.path : "-",
                 st->trace_file.active ? "active" : "off",
                 st->gdb_cfg.endpoint[0] ? st->gdb_cfg.endpoint : (st->gdb_cfg.enabled ? "enabled" : "-"),
                 telem_support_state_str(st->caps.gdb.state),
                 st->cosim.status[0] ? st->cosim.status : "-",
                 st->logs.emu_log_path[0] ? "emu" : "-",
                 st->logs.host_log_path[0] ? "host" : "-",
                 telem_support_state_str(st->caps.gpio.state),
                 telem_support_state_str(st->caps.registers.state),
                 telem_support_state_str(st->caps.memory.state),
                 telem_support_state_str(st->caps.perf.state),
                 telem_support_state_str(st->caps.cycles.state),
                 telem_support_state_str(st->caps.mips.state));
    return (int)off;
}
