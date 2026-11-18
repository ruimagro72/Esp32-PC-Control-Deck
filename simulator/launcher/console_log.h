#ifndef SIMULATOR_LAUNCHER_CONSOLE_LOG_H
#define SIMULATOR_LAUNCHER_CONSOLE_LOG_H

/*
 * console_log - ring buffer de linhas de consola/logs sem dependencias de UI.
 *
 * Objetivo: modelo reutilizavel para a consola integrada no launcher (T15).
 * Nao usa GTK/GLib. O chamador e responsavel pela sincronizacao entre threads
 * (nao existem locks internos). Callbacks sao invocados de forma sincrona
 * dentro da chamada que altera o estado; nao reentrar na mesma instancia.
 *
 * Formato textual de linha (compativel com simulator/logs/):
 *   [HH:MM:SS.mmm] [LEVEL] [origin] mensagem
 *
 * timestamp_ms: milissegundos desde epoch (wall clock) para hora legivel.
 *               0 (CLOG_TIMESTAMP_NONE) imprime [--:--:--.---].
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLOG_DEFAULT_CAPACITY 2000u
#define CLOG_MAX_CAPACITY     200000u
#define CLOG_MAX_LINE_TEXT    2048u
#define CLOG_MAX_SUBSTRING    128u
#define CLOG_TIMESTAMP_NONE   0ull

typedef enum {
    CLOG_LEVEL_UART = 0,
    CLOG_LEVEL_INFO,
    CLOG_LEVEL_WARN,
    CLOG_LEVEL_ERROR,
    CLOG_LEVEL_HOST,
    CLOG_LEVEL_COUNT
} clog_level_t;

typedef enum {
    CLOG_ORIGIN_UART = 0,
    CLOG_ORIGIN_EMU,
    CLOG_ORIGIN_HOST,
    CLOG_ORIGIN_LAUNCHER,
    CLOG_ORIGIN_VALIDATION,
    CLOG_ORIGIN_COUNT
} clog_origin_t;

#define CLOG_LEVEL_BIT(level)  (1u << (unsigned)(level))
#define CLOG_ORIGIN_BIT(origin) (1u << (unsigned)(origin))
#define CLOG_ALL_LEVELS  ((1u << CLOG_LEVEL_COUNT) - 1u)
#define CLOG_ALL_ORIGINS ((1u << CLOG_ORIGIN_COUNT) - 1u)

/* Linha armazenada. text e propriedade do log e emprestado ao chamador:
 * valido apenas ate a proxima mutacao (append/clear/reset/set_capacity/free). */
typedef struct {
    uint64_t seq;          /* id monotonico dentro da instancia */
    uint64_t timestamp_ms; /* epoch ms ou CLOG_TIMESTAMP_NONE */
    clog_level_t level;
    clog_origin_t origin;
    char *text;            /* NUL-terminated, truncado em CLOG_MAX_LINE_TEXT-1 */
    size_t text_len;
} clog_line_t;

/* Filtro de visibilidade. level_mask/origin_mask 0 significa "sem filtro". */
typedef struct {
    uint32_t level_mask;
    uint32_t origin_mask;
    char substring[CLOG_MAX_SUBSTRING]; /* "" desativa; match ASCII case-insensitive */
} clog_filter_t;

typedef struct {
    size_t capacity;
    size_t count;       /* linhas atualmente no ring */
    size_t visible;     /* linhas que passam o filtro atual */
    uint64_t appended;  /* total de append bem-sucedidos desde clear/reset */
    uint64_t dropped;   /* linhas descartadas por limite de capacidade */
    uint64_t unseen;    /* linhas novas desde o ultimo clog_mark_seen() */
    uint64_t level_counts[CLOG_LEVEL_COUNT];
    uint64_t origin_counts[CLOG_ORIGIN_COUNT];
    bool follow;
} clog_stats_t;

typedef struct clog_log clog_log_t;

typedef void (*clog_line_cb)(const clog_line_t *line, void *user_data);
typedef void (*clog_change_cb)(const clog_log_t *log, void *user_data);

/* Callback de iteracao. Devolver false interrompe a iteracao. */
typedef bool (*clog_visit_cb)(const clog_line_t *line, void *user_data);

/* Ciclo de vida. clog_free() liberta o interior mas mantem o objeto alocavel
 * para clog_init(); clog_destroy() liberta o objeto. Ambas aceitam NULL. */
clog_log_t *clog_new(size_t capacity);
bool clog_init(clog_log_t *log, size_t capacity);
void clog_free(clog_log_t *log);
void clog_destroy(clog_log_t *log);

/* clear: descarta linhas, contadores e unseen; mantem capacidade, filtros,
 * follow, callbacks e a sequencia (seq continua a crescer).
 * reset: clear + filtros repostos, follow=true, contadores/seq a zero.
 * Ambos mantem capacidade e callbacks. */
void clog_clear(clog_log_t *log);
void clog_reset(clog_log_t *log);

/* Redimensiona o ring. capacity==0 usa CLOG_DEFAULT_CAPACITY; valores acima
 * de CLOG_MAX_CAPACITY sao limitados. Ao encolher, descarta as linhas mais
 * antigas (contabilizadas em dropped). Devolve false apenas em OOM (estado
 * antigo preservado) ou log invalido. */
bool clog_set_capacity(clog_log_t *log, size_t capacity);
size_t clog_capacity(const clog_log_t *log);
size_t clog_count(const clog_log_t *log);

/* Append. timestamp_ms e controlado pelo chamador (ver nota no topo).
 * Texto e truncado em CLOG_MAX_LINE_TEXT-1 bytes. Devolve false em
 * argumentos invalidos ou OOM. */
bool clog_append(clog_log_t *log, clog_level_t level, clog_origin_t origin,
                 uint64_t timestamp_ms, const char *text);
bool clog_append_now(clog_log_t *log, clog_level_t level, clog_origin_t origin,
                     const char *text);
bool clog_appendf(clog_log_t *log, clog_level_t level, clog_origin_t origin,
                  uint64_t timestamp_ms, const char *fmt, ...);
bool clog_appendf_now(clog_log_t *log, clog_level_t level, clog_origin_t origin,
                      const char *fmt, ...);

/* Relogios de conveniencia. */
uint64_t clog_now_ms(void);       /* wall clock, epoch ms */
uint64_t clog_monotonic_ms(void); /* clock monotonico, ms arbitrarios */

/* Filtros. Os setters devolvem false se log==NULL ou substring demasiado
 * longa (filtro anterior preservado). Alteracoes notificam on_change. */
void clog_filter_reset(clog_log_t *log);
clog_filter_t clog_filter_get(const clog_log_t *log);
bool clog_filter_set_levels(clog_log_t *log, uint32_t level_mask);
bool clog_filter_set_origins(clog_log_t *log, uint32_t origin_mask);
bool clog_filter_set_substring(clog_log_t *log, const char *substring);
uint32_t clog_filter_levels(const clog_log_t *log);
uint32_t clog_filter_origins(const clog_log_t *log);
const char *clog_filter_substring(const clog_log_t *log);
bool clog_line_visible(const clog_log_t *log, const clog_line_t *line);

/* Iteracao em ordem cronologica (mais antiga primeiro). */
size_t clog_visible_count(const clog_log_t *log);
const clog_line_t *clog_visible_at(const clog_log_t *log, size_t visible_index);
bool clog_foreach_visible(const clog_log_t *log, clog_visit_cb cb, void *user_data);
bool clog_foreach(const clog_log_t *log, clog_visit_cb cb, void *user_data);

/* Formatacao/exportacao. clog_format_line e clog_snapshot_text seguem a
 * semantica de snprintf: devolvem o tamanho necessario (sem NUL) e escrevem
 * no maximo bufsz bytes. snapshot_alloc devolve buffer malloc'ed (chamador
 * liberta com free()) ou NULL em OOM. export_text escreve uma linha por
 * entrada com '\n'; devolve true em sucesso. */
size_t clog_format_line(const clog_line_t *line, char *buf, size_t bufsz);
size_t clog_snapshot_text(const clog_log_t *log, bool only_visible,
                          char *buf, size_t bufsz);
char *clog_snapshot_alloc(const clog_log_t *log, bool only_visible);
bool clog_export_text(const clog_log_t *log, const char *path, bool only_visible);

const char *clog_level_name(clog_level_t level);
const char *clog_origin_name(clog_origin_t origin);

/* Estado de novidade e autoscroll. */
uint64_t clog_mark_seen(clog_log_t *log); /* devolve unseen e repoe a zero */
uint64_t clog_unseen_count(const clog_log_t *log);
void clog_set_follow(clog_log_t *log, bool follow);
bool clog_follow(const clog_log_t *log);

/* Hooks opcionais (sem GTK). on_line recebe cada linha aceite no ring,
 * independentemente do filtro. on_change e chamado apos append, clear,
 * reset, alteracao de filtro/capacidade/follow. Nao chamado em mark_seen. */
void clog_set_on_line(clog_log_t *log, clog_line_cb cb, void *user_data);
void clog_set_on_change(clog_log_t *log, clog_change_cb cb, void *user_data);

uint64_t clog_level_count(const clog_log_t *log, clog_level_t level);
uint64_t clog_origin_count(const clog_log_t *log, clog_origin_t origin);
bool clog_get_stats(const clog_log_t *log, clog_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SIMULATOR_LAUNCHER_CONSOLE_LOG_H */
