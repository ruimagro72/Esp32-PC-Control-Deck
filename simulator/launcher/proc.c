#define _GNU_SOURCE
#include "proc.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define SIM_PROC_DEFAULT_KILL_TIMEOUT_MS 2000

struct SimProc {
    GPid pid;
    GPid pgid;
    gboolean running;
    gboolean stopping;
    guint child_watch_id;
    guint kill_timeout_id;
    gint last_wait_status;
    gchar *status_text;

    GIOChannel *out_ch;
    GIOChannel *err_ch;
    guint out_watch_id;
    guint err_watch_id;
    GString *out_part;   /* unterminated tail of stdout/stderr between reads */
    GString *err_part;

    SimProcLogFunc log_cb;
    gpointer log_user_data;
    SimProcExitFunc exit_cb;
    gpointer exit_user_data;
};

static void sim_proc_set_status(SimProc *proc, const char *status) {
    if (!proc) return;
    g_free(proc->status_text);
    proc->status_text = g_strdup(status ? status : "");
}

static void sim_proc_log_line(SimProc *proc, const char *line) {
    if (proc && proc->log_cb && line) {
        proc->log_cb(line, proc->log_user_data);
    }
}

static gboolean read_channel_lines(GIOChannel *ch, GIOCondition cond, gpointer data) {
    SimProc *proc = (SimProc *)data;
    if (!proc) return FALSE;

    if (cond & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
        /* Drain remaining data before closing. */
    }

    if (cond & (G_IO_IN | G_IO_PRI | G_IO_HUP)) {
        /* Raw reads + own line assembly: g_io_channel_read_line() on this unbuffered channel returned AGAIN on a
         * partial line (e.g. a prompt without newline) without consuming it, so the watch fired forever (100% CPU). */
        GString **part = (ch == proc->out_ch) ? &proc->out_part : &proc->err_part;
        if (!*part) *part = g_string_new(NULL);
        gchar buf[4096];
        GError *err = NULL;
        for (;;) {
            gsize n = 0;
            GIOStatus st = g_io_channel_read_chars(ch, buf, sizeof(buf), &n, &err);
            if (n > 0) {
                g_string_append_len(*part, buf, (gssize)n);
                char *nl;
                while ((nl = memchr((*part)->str, '\n', (*part)->len)) != NULL) {
                    gsize ll = (gsize)(nl - (*part)->str);
                    gchar *line = g_strndup((*part)->str, ll);
                    g_strchomp(line); /* removes trailing \r */
                    sim_proc_log_line(proc, line);
                    g_free(line);
                    g_string_erase(*part, 0, (gssize)(ll + 1));
                }
            }
            if (st == G_IO_STATUS_NORMAL) continue;
            if (st == G_IO_STATUS_AGAIN) break;
            if (err) {
                sim_proc_log_line(proc, err->message);
                g_clear_error(&err);
            }
            if ((*part)->len > 0) { /* EOF/ERROR: flush the unterminated tail */
                gchar *line = g_strdup((*part)->str);
                g_strchomp(line);
                sim_proc_log_line(proc, line);
                g_free(line);
                g_string_set_size(*part, 0);
            }
            break; /* EOF or ERROR */
        }
    }

    if (cond & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
        if (ch == proc->out_ch) proc->out_watch_id = 0;
        if (ch == proc->err_ch) proc->err_watch_id = 0;
        g_io_channel_shutdown(ch, FALSE, NULL);
        g_io_channel_unref(ch);
        if (ch == proc->out_ch) proc->out_ch = NULL;
        if (ch == proc->err_ch) proc->err_ch = NULL;
        return FALSE;
    }
    return TRUE;
}

static void sim_proc_detach_channels(SimProc *proc) {
    if (!proc) return;
    if (proc->out_watch_id) { g_source_remove(proc->out_watch_id); proc->out_watch_id = 0; }
    if (proc->err_watch_id) { g_source_remove(proc->err_watch_id); proc->err_watch_id = 0; }
    if (proc->out_ch) { g_io_channel_shutdown(proc->out_ch, FALSE, NULL); g_io_channel_unref(proc->out_ch); proc->out_ch = NULL; }
    if (proc->err_ch) { g_io_channel_shutdown(proc->err_ch, FALSE, NULL); g_io_channel_unref(proc->err_ch); proc->err_ch = NULL; }
}

static void on_child_exit(GPid pid, gint status, gpointer data) {
    SimProc *proc = (SimProc *)data;
    if (!proc) return;

    proc->last_wait_status = status;
    g_spawn_close_pid(pid);

    if (proc->kill_timeout_id) {
        g_source_remove(proc->kill_timeout_id);
        proc->kill_timeout_id = 0;
    }
    proc->child_watch_id = 0;
    proc->pid = 0;
    proc->pgid = 0;
    proc->running = FALSE;
    proc->stopping = FALSE;

    if (WIFEXITED(status)) {
        gchar *s = g_strdup_printf("Exited (%d)", WEXITSTATUS(status));
        sim_proc_set_status(proc, s);
        g_free(s);
    } else if (WIFSIGNALED(status)) {
        gchar *s = g_strdup_printf("Signaled (%d)", WTERMSIG(status));
        sim_proc_set_status(proc, s);
        g_free(s);
    } else {
        sim_proc_set_status(proc, "Stopped");
    }

    /* Keep channels draining until HUP via watches if any; they usually close now. */
    if (proc->exit_cb) {
        proc->exit_cb(proc, status, proc->exit_user_data);
    }
}

static void child_setup_new_group(gpointer user_data) {
    (void)user_data;
    /* New process group/session leader so whole tree can be killed together. */
    (void)setsid();
    (void)setpgid(0, 0);
}

static gboolean on_kill_timeout(gpointer data) {
    SimProc *proc = (SimProc *)data;
    if (!proc) return G_SOURCE_REMOVE;
    proc->kill_timeout_id = 0;
    if (proc->running && proc->pgid > 0) {
        sim_proc_log_line(proc, "[proc] SIGTERM timeout, sending SIGKILL to process group");
        (void)kill(-proc->pgid, SIGKILL);
    } else if (proc->running && proc->pid > 0) {
        sim_proc_log_line(proc, "[proc] SIGTERM timeout, sending SIGKILL to pid");
        (void)kill(proc->pid, SIGKILL);
    }
    return G_SOURCE_REMOVE;
}

SimProc *sim_proc_new(void) {
    SimProc *proc = g_new0(SimProc, 1);
    sim_proc_set_status(proc, "Stopped");
    return proc;
}

void sim_proc_free(SimProc *proc) {
    if (!proc) return;
    /* Best-effort stop; caller should normally stop and let loop reap. */
    if (proc->running) {
        if (proc->pgid > 0) (void)kill(-proc->pgid, SIGKILL);
        else if (proc->pid > 0) (void)kill(proc->pid, SIGKILL);
    }
    if (proc->child_watch_id) g_source_remove(proc->child_watch_id);
    if (proc->kill_timeout_id) g_source_remove(proc->kill_timeout_id);
    sim_proc_detach_channels(proc);
    if (proc->out_part) g_string_free(proc->out_part, TRUE);
    if (proc->err_part) g_string_free(proc->err_part, TRUE);
    g_free(proc->status_text);
    g_free(proc);
}

gboolean sim_proc_start_shell(SimProc *proc,
                              const char *command,
                              const char *working_dir,
                              SimProcLogFunc log_cb,
                              gpointer log_user_data,
                              SimProcExitFunc exit_cb,
                              gpointer exit_user_data,
                              GError **error) {
    if (!proc || !command || !*command) {
        g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_INVAL, "invalid proc or empty command");
        return FALSE;
    }
    if (proc->running) {
        g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED, "process already running");
        return FALSE;
    }

    sim_proc_detach_channels(proc);
    if (proc->kill_timeout_id) { g_source_remove(proc->kill_timeout_id); proc->kill_timeout_id = 0; }

    gchar *argv[] = {"/bin/sh", "-c", (gchar *)command, NULL};
    GPid pid = 0;
    gint out_fd = -1, err_fd = -1;

    gboolean ok = g_spawn_async_with_pipes(
        (working_dir && *working_dir) ? working_dir : NULL,
        argv,
        NULL,
        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
        child_setup_new_group,
        NULL,
        &pid,
        NULL,
        &out_fd,
        &err_fd,
        error);

    if (!ok) {
        sim_proc_set_status(proc, "Start failed");
        return FALSE;
    }

    proc->pid = pid;
    proc->pgid = pid; /* child_setup: setsid/setpgid(0,0) => leader of new group */
    proc->running = TRUE;
    proc->stopping = FALSE;
    proc->log_cb = log_cb;
    proc->log_user_data = log_user_data;
    proc->exit_cb = exit_cb;
    proc->exit_user_data = exit_user_data;

    {
        gchar *s = g_strdup_printf("Running (pid=%d)", (int)pid);
        sim_proc_set_status(proc, s);
        g_free(s);
    }

    if (out_fd >= 0) {
        proc->out_ch = g_io_channel_unix_new(out_fd);
        g_io_channel_set_encoding(proc->out_ch, NULL, NULL);
        g_io_channel_set_flags(proc->out_ch, G_IO_FLAG_NONBLOCK, NULL);
        g_io_channel_set_buffered(proc->out_ch, FALSE);
        proc->out_watch_id = g_io_add_watch(proc->out_ch, G_IO_IN | G_IO_HUP | G_IO_ERR, read_channel_lines, proc);
    }
    if (err_fd >= 0) {
        proc->err_ch = g_io_channel_unix_new(err_fd);
        g_io_channel_set_encoding(proc->err_ch, NULL, NULL);
        g_io_channel_set_flags(proc->err_ch, G_IO_FLAG_NONBLOCK, NULL);
        g_io_channel_set_buffered(proc->err_ch, FALSE);
        proc->err_watch_id = g_io_add_watch(proc->err_ch, G_IO_IN | G_IO_HUP | G_IO_ERR, read_channel_lines, proc);
    }

    proc->child_watch_id = g_child_watch_add(pid, on_child_exit, proc);
    return TRUE;
}

void sim_proc_stop(SimProc *proc, guint timeout_ms) {
    if (!proc || !proc->running) return;
    if (proc->stopping) return; /* TERM already sent, fallback pending */

    proc->stopping = TRUE;
    sim_proc_set_status(proc, "Stopping...");
    sim_proc_log_line(proc, "[proc] sending SIGTERM to process group");

    if (proc->pgid > 0) {
        if (kill(-proc->pgid, SIGTERM) != 0 && errno == ESRCH && proc->pid > 0) {
            (void)kill(proc->pid, SIGTERM);
        }
    } else if (proc->pid > 0) {
        (void)kill(proc->pid, SIGTERM);
    }

    if (timeout_ms == 0) timeout_ms = SIM_PROC_DEFAULT_KILL_TIMEOUT_MS;
    if (proc->kill_timeout_id) g_source_remove(proc->kill_timeout_id);
    proc->kill_timeout_id = g_timeout_add(timeout_ms, on_kill_timeout, proc);
}

gboolean sim_proc_is_running(const SimProc *proc) {
    return proc ? proc->running : FALSE;
}

GPid sim_proc_get_pid(const SimProc *proc) {
    return proc ? proc->pid : 0;
}

GPid sim_proc_get_pgid(const SimProc *proc) {
    return proc ? proc->pgid : 0;
}

const char *sim_proc_get_status(const SimProc *proc) {
    return (proc && proc->status_text) ? proc->status_text : "Unknown";
}
