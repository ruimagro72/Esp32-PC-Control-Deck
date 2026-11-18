#ifndef SIM_LAUNCHER_PROC_H
#define SIM_LAUNCHER_PROC_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct SimProc SimProc;

typedef void (*SimProcLogFunc)(const char *line, gpointer user_data);
typedef void (*SimProcExitFunc)(SimProc *proc, gint wait_status, gpointer user_data);

SimProc *sim_proc_new(void);
void sim_proc_free(SimProc *proc);

/* Start shell command via /bin/sh -c <command>. Spawns in new process group.
 * Captures stdout/stderr line-by-line to log_cb when provided.
 * On success installs child watch and returns TRUE; on failure sets error. */
gboolean sim_proc_start_shell(SimProc *proc,
                              const char *command,
                              const char *working_dir,
                              SimProcLogFunc log_cb,
                              gpointer log_user_data,
                              SimProcExitFunc exit_cb,
                              gpointer exit_user_data,
                              GError **error);

/* Request stop: SIGTERM to process group, then SIGKILL fallback after timeout_ms
 * if still running. timeout_ms <= 0 uses default. Safe if not running. */
void sim_proc_stop(SimProc *proc, guint timeout_ms);

gboolean sim_proc_is_running(const SimProc *proc);
GPid sim_proc_get_pid(const SimProc *proc);
GPid sim_proc_get_pgid(const SimProc *proc);

/* Status text for UI (e.g. "Stopped", "Running (pid=123)", "Stopping..."). */
const char *sim_proc_get_status(const SimProc *proc);

G_END_DECLS

#endif /* SIM_LAUNCHER_PROC_H */
