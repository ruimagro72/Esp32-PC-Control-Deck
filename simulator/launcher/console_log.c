#define _POSIX_C_SOURCE 200809L

#include "console_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct clog_log {
    clog_line_t *lines;
    size_t capacity;
    size_t count;
    size_t head; /* indice da linha mais antiga quando count > 0 */
    uint64_t next_seq;
    uint64_t appended;
    uint64_t dropped;
    uint64_t unseen;
    uint64_t level_counts[CLOG_LEVEL_COUNT];
    uint64_t origin_counts[CLOG_ORIGIN_COUNT];
    clog_filter_t filter;
    bool follow;
    clog_line_cb on_line;
    void *on_line_data;
    clog_change_cb on_change;
    void *on_change_data;
};

static const char *const k_level_names[CLOG_LEVEL_COUNT] = {
    "UART", "INFO", "WARN", "ERROR", "HOST",
};

static const char *const k_origin_names[CLOG_ORIGIN_COUNT] = {
    "uart", "emu", "host", "launcher", "validation",
};

static void notify_change(clog_log_t *log)
{
    if (log->on_change != NULL) {
        log->on_change(log, log->on_change_data);
    }
}

static bool level_valid(clog_level_t level)
{
    return (int)level >= 0 && (int)level < (int)CLOG_LEVEL_COUNT;
}

static bool origin_valid(clog_origin_t origin)
{
    return (int)origin >= 0 && (int)origin < (int)CLOG_ORIGIN_COUNT;
}

static size_t clamp_capacity(size_t capacity)
{
    if (capacity == 0) {
        return CLOG_DEFAULT_CAPACITY;
    }
    if (capacity > CLOG_MAX_CAPACITY) {
        return CLOG_MAX_CAPACITY;
    }
    return capacity;
}

static size_t slot_of(const clog_log_t *log, size_t index)
{
    return (log->head + index) % log->capacity;
}

static void line_dispose(clog_line_t *line)
{
    free(line->text);
    memset(line, 0, sizeof(*line));
}

static void dispose_all_lines(clog_log_t *log)
{
    size_t i;
    for (i = 0; i < log->count; ++i) {
        line_dispose(&log->lines[slot_of(log, i)]);
    }
    log->count = 0;
    log->head = 0;
}

static char *dup_truncated(const char *text, size_t len)
{
    size_t limit = CLOG_MAX_LINE_TEXT - 1u;
    char *copy;

    if (len > limit) {
        len = limit;
    }
    copy = (char *)malloc(len + 1u);
    if (copy == NULL) {
        return NULL;
    }
    if (len > 0) {
        memcpy(copy, text, len);
    }
    copy[len] = '\0';
    return copy;
}

static bool append_owned(clog_log_t *log, clog_level_t level, clog_origin_t origin,
                         uint64_t timestamp_ms, char *text, size_t text_len)
{
    clog_line_t *line;

    if (log == NULL || log->lines == NULL || log->capacity == 0) {
        free(text);
        return false;
    }
    if (!level_valid(level) || !origin_valid(origin) || text == NULL) {
        free(text);
        return false;
    }

    if (log->count == log->capacity) {
        line_dispose(&log->lines[log->head]);
        log->head = (log->head + 1u) % log->capacity;
        --log->count;
        ++log->dropped;
    }

    line = &log->lines[slot_of(log, log->count)];
    line->seq = log->next_seq++;
    line->timestamp_ms = timestamp_ms;
    line->level = level;
    line->origin = origin;
    line->text = text;
    line->text_len = text_len;
    ++log->count;

    ++log->appended;
    ++log->unseen;
    ++log->level_counts[level];
    ++log->origin_counts[origin];

    if (log->on_line != NULL) {
        log->on_line(line, log->on_line_data);
    }
    notify_change(log);
    return true;
}

static int ascii_lower(int c)
{
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

static bool ascii_ci_contains(const char *haystack, const char *needle)
{
    size_t needle_len = strlen(needle);
    const char *p;

    if (needle_len == 0) {
        return true;
    }
    for (p = haystack; *p != '\0'; ++p) {
        size_t i = 0;
        while (i < needle_len && p[i] != '\0' &&
               ascii_lower((unsigned char)p[i]) ==
                   ascii_lower((unsigned char)needle[i])) {
            ++i;
        }
        if (i == needle_len) {
            return true;
        }
        if (p[i] == '\0') {
            break;
        }
    }
    return false;
}

static bool line_matches(const clog_log_t *log, const clog_line_t *line)
{
    if (log->filter.level_mask != 0u &&
        (log->filter.level_mask & CLOG_LEVEL_BIT(line->level)) == 0u) {
        return false;
    }
    if (log->filter.origin_mask != 0u &&
        (log->filter.origin_mask & CLOG_ORIGIN_BIT(line->origin)) == 0u) {
        return false;
    }
    if (log->filter.substring[0] != '\0' &&
        !ascii_ci_contains(line->text, log->filter.substring)) {
        return false;
    }
    return true;
}

clog_log_t *clog_new(size_t capacity)
{
    clog_log_t *log = (clog_log_t *)calloc(1u, sizeof(*log));
    if (log == NULL) {
        return NULL;
    }
    if (!clog_init(log, capacity)) {
        free(log);
        return NULL;
    }
    return log;
}

bool clog_init(clog_log_t *log, size_t capacity)
{
    if (log == NULL) {
        return false;
    }
    clog_free(log);

    capacity = clamp_capacity(capacity);
    log->lines = (clog_line_t *)calloc(capacity, sizeof(clog_line_t));
    if (log->lines == NULL) {
        memset(log, 0, sizeof(*log));
        return false;
    }
    log->capacity = capacity;
    log->follow = true;
    return true;
}

void clog_free(clog_log_t *log)
{
    if (log == NULL) {
        return;
    }
    if (log->lines != NULL) {
        dispose_all_lines(log);
        free(log->lines);
    }
    memset(log, 0, sizeof(*log));
}

void clog_destroy(clog_log_t *log)
{
    if (log == NULL) {
        return;
    }
    clog_free(log);
    free(log);
}

void clog_clear(clog_log_t *log)
{
    if (log == NULL) {
        return;
    }
    dispose_all_lines(log);
    log->appended = 0;
    log->dropped = 0;
    log->unseen = 0;
    memset(log->level_counts, 0, sizeof(log->level_counts));
    memset(log->origin_counts, 0, sizeof(log->origin_counts));
    notify_change(log);
}

void clog_reset(clog_log_t *log)
{
    if (log == NULL) {
        return;
    }
    dispose_all_lines(log);
    log->next_seq = 0;
    log->appended = 0;
    log->dropped = 0;
    log->unseen = 0;
    memset(log->level_counts, 0, sizeof(log->level_counts));
    memset(log->origin_counts, 0, sizeof(log->origin_counts));
    memset(&log->filter, 0, sizeof(log->filter));
    log->follow = true;
    notify_change(log);
}

bool clog_set_capacity(clog_log_t *log, size_t capacity)
{
    clog_line_t *new_lines;
    size_t new_capacity;
    size_t keep;
    size_t skip;
    size_t i;

    if (log == NULL) {
        return false;
    }
    new_capacity = clamp_capacity(capacity);
    if (new_capacity == log->capacity && log->lines != NULL) {
        return true;
    }

    new_lines = (clog_line_t *)calloc(new_capacity, sizeof(clog_line_t));
    if (new_lines == NULL) {
        return false;
    }

    keep = log->count < new_capacity ? log->count : new_capacity;
    skip = log->count - keep;
    for (i = 0; i < skip; ++i) {
        line_dispose(&log->lines[slot_of(log, i)]);
    }
    for (i = 0; i < keep; ++i) {
        new_lines[i] = log->lines[slot_of(log, skip + i)];
    }

    if (log->lines != NULL) {
        free(log->lines);
    }
    log->lines = new_lines;
    log->capacity = new_capacity;
    log->count = keep;
    log->head = 0;
    log->dropped += (uint64_t)skip;

    notify_change(log);
    return true;
}

size_t clog_capacity(const clog_log_t *log)
{
    return log != NULL ? log->capacity : 0u;
}

size_t clog_count(const clog_log_t *log)
{
    return log != NULL ? log->count : 0u;
}

bool clog_append(clog_log_t *log, clog_level_t level, clog_origin_t origin,
                 uint64_t timestamp_ms, const char *text)
{
    char *copy;

    if (log == NULL || text == NULL) {
        return false;
    }
    copy = dup_truncated(text, strlen(text));
    if (copy == NULL) {
        return false;
    }
    return append_owned(log, level, origin, timestamp_ms, copy, strlen(copy));
}

bool clog_append_now(clog_log_t *log, clog_level_t level, clog_origin_t origin,
                     const char *text)
{
    return clog_append(log, level, origin, clog_now_ms(), text);
}

bool clog_appendf(clog_log_t *log, clog_level_t level, clog_origin_t origin,
                  uint64_t timestamp_ms, const char *fmt, ...)
{
    va_list ap;
    va_list copy;
    int needed;
    size_t len;
    char *text;

    if (log == NULL || fmt == NULL) {
        return false;
    }

    va_start(ap, fmt);
    va_copy(copy, ap);
    needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);

    if (needed < 0) {
        va_end(ap);
        return false;
    }

    len = (size_t)needed;
    if (len >= CLOG_MAX_LINE_TEXT) {
        len = CLOG_MAX_LINE_TEXT - 1u;
    }
    text = (char *)malloc(len + 1u);
    if (text == NULL) {
        va_end(ap);
        return false;
    }
    vsnprintf(text, len + 1u, fmt, ap);
    va_end(ap);

    return append_owned(log, level, origin, timestamp_ms, text, strlen(text));
}

bool clog_appendf_now(clog_log_t *log, clog_level_t level, clog_origin_t origin,
                      const char *fmt, ...)
{
    va_list ap;
    va_list copy;
    int needed;
    size_t len;
    char *text;

    if (log == NULL || fmt == NULL) {
        return false;
    }

    va_start(ap, fmt);
    va_copy(copy, ap);
    needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);

    if (needed < 0) {
        va_end(ap);
        return false;
    }

    len = (size_t)needed;
    if (len >= CLOG_MAX_LINE_TEXT) {
        len = CLOG_MAX_LINE_TEXT - 1u;
    }
    text = (char *)malloc(len + 1u);
    if (text == NULL) {
        va_end(ap);
        return false;
    }
    vsnprintf(text, len + 1u, fmt, ap);
    va_end(ap);

    return append_owned(log, level, origin, clog_now_ms(), text, strlen(text));
}

uint64_t clog_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000L);
}

uint64_t clog_monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000L);
}

void clog_filter_reset(clog_log_t *log)
{
    if (log == NULL) {
        return;
    }
    memset(&log->filter, 0, sizeof(log->filter));
    notify_change(log);
}

clog_filter_t clog_filter_get(const clog_log_t *log)
{
    clog_filter_t empty;
    memset(&empty, 0, sizeof(empty));
    return log != NULL ? log->filter : empty;
}

bool clog_filter_set_levels(clog_log_t *log, uint32_t level_mask)
{
    if (log == NULL) {
        return false;
    }
    log->filter.level_mask = level_mask & CLOG_ALL_LEVELS;
    notify_change(log);
    return true;
}

bool clog_filter_set_origins(clog_log_t *log, uint32_t origin_mask)
{
    if (log == NULL) {
        return false;
    }
    log->filter.origin_mask = origin_mask & CLOG_ALL_ORIGINS;
    notify_change(log);
    return true;
}

bool clog_filter_set_substring(clog_log_t *log, const char *substring)
{
    size_t len;

    if (log == NULL) {
        return false;
    }
    if (substring == NULL || substring[0] == '\0') {
        log->filter.substring[0] = '\0';
        notify_change(log);
        return true;
    }
    len = strlen(substring);
    if (len >= CLOG_MAX_SUBSTRING) {
        return false;
    }
    memcpy(log->filter.substring, substring, len + 1u);
    notify_change(log);
    return true;
}

uint32_t clog_filter_levels(const clog_log_t *log)
{
    return log != NULL ? log->filter.level_mask : 0u;
}

uint32_t clog_filter_origins(const clog_log_t *log)
{
    return log != NULL ? log->filter.origin_mask : 0u;
}

const char *clog_filter_substring(const clog_log_t *log)
{
    return log != NULL ? log->filter.substring : "";
}

bool clog_line_visible(const clog_log_t *log, const clog_line_t *line)
{
    if (log == NULL || line == NULL) {
        return false;
    }
    return line_matches(log, line);
}

size_t clog_visible_count(const clog_log_t *log)
{
    size_t i;
    size_t visible = 0;

    if (log == NULL) {
        return 0;
    }
    for (i = 0; i < log->count; ++i) {
        if (line_matches(log, &log->lines[slot_of(log, i)])) {
            ++visible;
        }
    }
    return visible;
}

const clog_line_t *clog_visible_at(const clog_log_t *log, size_t visible_index)
{
    size_t i;
    size_t visible = 0;

    if (log == NULL) {
        return NULL;
    }
    for (i = 0; i < log->count; ++i) {
        const clog_line_t *line = &log->lines[slot_of(log, i)];
        if (!line_matches(log, line)) {
            continue;
        }
        if (visible == visible_index) {
            return line;
        }
        ++visible;
    }
    return NULL;
}

bool clog_foreach_visible(const clog_log_t *log, clog_visit_cb cb, void *user_data)
{
    size_t i;

    if (log == NULL || cb == NULL) {
        return false;
    }
    for (i = 0; i < log->count; ++i) {
        const clog_line_t *line = &log->lines[slot_of(log, i)];
        if (!line_matches(log, line)) {
            continue;
        }
        if (!cb(line, user_data)) {
            return false;
        }
    }
    return true;
}

bool clog_foreach(const clog_log_t *log, clog_visit_cb cb, void *user_data)
{
    size_t i;

    if (log == NULL || cb == NULL) {
        return false;
    }
    for (i = 0; i < log->count; ++i) {
        const clog_line_t *line = &log->lines[slot_of(log, i)];
        if (!cb(line, user_data)) {
            return false;
        }
    }
    return true;
}

static int format_timestamp(uint64_t timestamp_ms, char *buf, size_t bufsz)
{
    time_t seconds;
    unsigned millis;
    struct tm tm_value;

    if (timestamp_ms == CLOG_TIMESTAMP_NONE) {
        return snprintf(buf, bufsz, "--:--:--.---");
    }
    seconds = (time_t)(timestamp_ms / 1000ull);
    millis = (unsigned)(timestamp_ms % 1000ull);
    memset(&tm_value, 0, sizeof(tm_value));
    if (localtime_r(&seconds, &tm_value) == NULL) {
        return snprintf(buf, bufsz, "--:--:--.---");
    }
    return snprintf(buf, bufsz, "%02d:%02d:%02d.%03u",
                    tm_value.tm_hour, tm_value.tm_min, tm_value.tm_sec,
                    millis % 1000u);
}

size_t clog_format_line(const clog_line_t *line, char *buf, size_t bufsz)
{
    char timestamp[16];
    int written;

    if (buf == NULL) {
        bufsz = 0;
    }
    if (line == NULL) {
        if (bufsz > 0) {
            buf[0] = '\0';
        }
        return 0;
    }

    format_timestamp(line->timestamp_ms, timestamp, sizeof(timestamp));
    written = snprintf(buf, bufsz, "[%s] [%-5s] [%-10s] %s",
                       timestamp,
                       clog_level_name(line->level),
                       clog_origin_name(line->origin),
                       line->text != NULL ? line->text : "");
    if (written < 0) {
        return 0;
    }
    return (size_t)written;
}

size_t clog_snapshot_text(const clog_log_t *log, bool only_visible,
                          char *buf, size_t bufsz)
{
    size_t needed = 0;
    size_t i;
    size_t written = 0;

    if (buf == NULL) {
        bufsz = 0;
    }
    if (log == NULL) {
        if (bufsz > 0) {
            buf[0] = '\0';
        }
        return 0;
    }

    for (i = 0; i < log->count; ++i) {
        const clog_line_t *line = &log->lines[slot_of(log, i)];
        size_t line_len;

        if (only_visible && !line_matches(log, line)) {
            continue;
        }
        line_len = clog_format_line(line, NULL, 0);
        needed += line_len + 1u; /* '\n' */
        if (written < bufsz) {
            size_t avail = bufsz - written;
            if (line_len + 1u <= avail) {
                clog_format_line(line, buf + written, avail);
                written += line_len;
                buf[written] = '\n';
                ++written;
                if (written < bufsz) {
                    buf[written] = '\0';
                } else {
                    buf[bufsz - 1u] = '\0';
                }
            } else {
                /* Truncado: o buffer fica NUL-terminado no limite. */
                clog_format_line(line, buf + written, avail);
                written = bufsz;
            }
        }
    }

    if (bufsz > 0 && written < bufsz) {
        buf[written] = '\0';
    } else if (bufsz > 0) {
        buf[bufsz - 1u] = '\0';
    }
    return needed;
}

char *clog_snapshot_alloc(const clog_log_t *log, bool only_visible)
{
    size_t needed;
    char *buf;

    if (log == NULL) {
        return NULL;
    }
    needed = clog_snapshot_text(log, only_visible, NULL, 0);
    buf = (char *)malloc(needed + 1u);
    if (buf == NULL) {
        return NULL;
    }
    clog_snapshot_text(log, only_visible, buf, needed + 1u);
    return buf;
}

bool clog_export_text(const clog_log_t *log, const char *path, bool only_visible)
{
    FILE *file;
    size_t i;
    char line_buf[CLOG_MAX_LINE_TEXT + 64u];
    bool ok = true;

    if (log == NULL || path == NULL) {
        return false;
    }
    file = fopen(path, "w");
    if (file == NULL) {
        return false;
    }

    for (i = 0; i < log->count; ++i) {
        const clog_line_t *line = &log->lines[slot_of(log, i)];
        if (only_visible && !line_matches(log, line)) {
            continue;
        }
        clog_format_line(line, line_buf, sizeof(line_buf));
        if (fprintf(file, "%s\n", line_buf) < 0) {
            ok = false;
            break;
        }
    }

    if (fclose(file) != 0) {
        ok = false;
    }
    return ok;
}

const char *clog_level_name(clog_level_t level)
{
    if (!level_valid(level)) {
        return "?";
    }
    return k_level_names[level];
}

const char *clog_origin_name(clog_origin_t origin)
{
    if (!origin_valid(origin)) {
        return "?";
    }
    return k_origin_names[origin];
}

uint64_t clog_mark_seen(clog_log_t *log)
{
    uint64_t unseen;

    if (log == NULL) {
        return 0;
    }
    unseen = log->unseen;
    log->unseen = 0;
    return unseen;
}

uint64_t clog_unseen_count(const clog_log_t *log)
{
    return log != NULL ? log->unseen : 0u;
}

void clog_set_follow(clog_log_t *log, bool follow)
{
    if (log == NULL || log->follow == follow) {
        return;
    }
    log->follow = follow;
    notify_change(log);
}

bool clog_follow(const clog_log_t *log)
{
    return log != NULL ? log->follow : false;
}

void clog_set_on_line(clog_log_t *log, clog_line_cb cb, void *user_data)
{
    if (log == NULL) {
        return;
    }
    log->on_line = cb;
    log->on_line_data = user_data;
}

void clog_set_on_change(clog_log_t *log, clog_change_cb cb, void *user_data)
{
    if (log == NULL) {
        return;
    }
    log->on_change = cb;
    log->on_change_data = user_data;
}

uint64_t clog_level_count(const clog_log_t *log, clog_level_t level)
{
    if (log == NULL || !level_valid(level)) {
        return 0;
    }
    return log->level_counts[level];
}

uint64_t clog_origin_count(const clog_log_t *log, clog_origin_t origin)
{
    if (log == NULL || !origin_valid(origin)) {
        return 0;
    }
    return log->origin_counts[origin];
}

bool clog_get_stats(const clog_log_t *log, clog_stats_t *out)
{
    if (log == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->capacity = log->capacity;
    out->count = log->count;
    out->visible = clog_visible_count(log);
    out->appended = log->appended;
    out->dropped = log->dropped;
    out->unseen = log->unseen;
    memcpy(out->level_counts, log->level_counts, sizeof(out->level_counts));
    memcpy(out->origin_counts, log->origin_counts, sizeof(out->origin_counts));
    out->follow = log->follow;
    return true;
}
