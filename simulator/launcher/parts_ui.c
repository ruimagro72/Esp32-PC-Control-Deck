#include "parts_ui.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void parts_ui_copy(char *dst, size_t dstsz, const char *src) {
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    snprintf(dst, dstsz, "%s", src);
}

static bool parts_ui_is_dir(const char *path) {
    struct stat st;
    if (!path || !*path) return false;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

static bool parts_ui_is_file(const char *path) {
    struct stat st;
    if (!path || !*path) return false;
    if (stat(path, &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

static bool parts_ui_has_suffix_ci(const char *s, const char *suf) {
    size_t ls, lsu;
    if (!s || !suf) return false;
    ls = strlen(s); lsu = strlen(suf);
    if (lsu > ls) return false;
    for (size_t i = 0; i < lsu; i++) {
        char a = s[ls - lsu + i];
        char b = suf[i];
        if (tolower((unsigned char)a) != tolower((unsigned char)b)) return false;
    }
    return true;
}

void parts_ui_context_init(parts_ui_context_t *ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->esp_emu_has_wasm_parts = false;
    ctx->esp_emu_has_control_part_cmds = false;
    ctx->host_cosim_tcp_available = false;
    ctx->host_cosim_board_available = false;
    ctx->esp_emu_has_cosim_board_flag = true; /* esp-emu 1.1.0 CLI flag exists */
    ctx->esp_emu_has_cosim_tcp_flag = true;   /* esp-emu 1.1.0 CLI flag exists */
    parts_ui_copy(ctx->esp_emu_version, sizeof(ctx->esp_emu_version), "1.1.0");
    parts_ui_copy(ctx->esp_emu_binary, sizeof(ctx->esp_emu_binary), "esp-emu");
}

void parts_ui_report_init(parts_ui_report_t *r) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
}

void parts_ui_report_add(parts_ui_report_t *r, parts_ui_diag_sev_t sev,
                         const char *code, const char *subject, const char *fmt, ...) {
    if (!r || r->count >= PARTS_UI_MAX_DIAGS) return;
    parts_ui_diag_t *d = &r->items[r->count++];
    memset(d, 0, sizeof(*d));
    d->severity = sev;
    parts_ui_copy(d->code, sizeof(d->code), code ? code : "");
    parts_ui_copy(d->subject, sizeof(d->subject), subject ? subject : "");
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(d->message, sizeof(d->message), fmt, ap);
        va_end(ap);
    }
    if (sev == PARTS_UI_DIAG_ERROR) r->errors++;
    else if (sev == PARTS_UI_DIAG_WARN) r->warnings++;
}

const char *parts_ui_support_state_name(parts_ui_support_state_t st) {
    switch (st) {
        case PARTS_UI_SUPPORT_SUPPORTED_NOW: return "supported-now";
        case PARTS_UI_SUPPORT_CONDITIONAL_VERSION_TOOL: return "conditional(version/tool)";
        case PARTS_UI_SUPPORT_NEEDS_RUNTIME_HOST: return "needs-runtime-host";
        case PARTS_UI_SUPPORT_BLOCKED_BY_EMU: return "blocked-by-emu";
        default: return "unknown";
    }
}

const char *parts_ui_claim_kind_name(parts_ui_claim_kind_t kind) {
    switch (kind) {
        case PARTS_UI_CLAIM_GPIO: return "gpio";
        case PARTS_UI_CLAIM_I2C_ADDR: return "i2c";
        case PARTS_UI_CLAIM_SPI_CS: return "spi_cs";
        case PARTS_UI_CLAIM_UART: return "uart";
        case PARTS_UI_CLAIM_PWM: return "pwm";
        case PARTS_UI_CLAIM_ADC: return "adc";
        case PARTS_UI_CLAIM_EN: return "en";
        default: return "claim";
    }
}

static parts_ui_part_meta_t *find_part(parts_ui_context_t *ctx, const char *name) {
    if (!ctx || !name) return NULL;
    for (int i = 0; i < ctx->part_count; i++) {
        if (strcmp(ctx->parts[i].name, name) == 0) return &ctx->parts[i];
    }
    return NULL;
}

static parts_ui_instance_t *find_instance(parts_ui_context_t *ctx, const char *id) {
    if (!ctx || !id) return NULL;
    for (int i = 0; i < ctx->instance_count; i++) {
        if (strcmp(ctx->instances[i].id, id) == 0) return &ctx->instances[i];
    }
    return NULL;
}

parts_ui_support_state_t parts_ui_classify_support(const parts_ui_context_t *ctx,
                                                   const parts_ui_part_meta_t *meta) {
    if (!ctx) return PARTS_UI_SUPPORT_CONDITIONAL_VERSION_TOOL;
    /* esp-emu 1.1.0: --cosim-board/--cosim-tcp/control-tcp part commands exist as flags,
     * but in-process WASM part execution and tooling/ABI remain conditional. Live integration
     * still needs runtime host/board wiring and valid sidecar metadata. */
    if (meta && meta->sidecar_json_path[0] == '\0' && ctx->esp_emu_has_wasm_parts) {
        /* discovered wasm without sidecar: metadata/wiring incomplete */
        return PARTS_UI_SUPPORT_CONDITIONAL_VERSION_TOOL;
    }
    if (!ctx->esp_emu_has_wasm_parts || !ctx->esp_emu_has_cosim_board_flag) {
        if (ctx->esp_emu_has_cosim_tcp_flag || ctx->esp_emu_has_control_part_cmds)
            return PARTS_UI_SUPPORT_NEEDS_RUNTIME_HOST;
        return PARTS_UI_SUPPORT_CONDITIONAL_VERSION_TOOL;
    }
    if (ctx->host_cosim_board_available || ctx->host_cosim_tcp_available || ctx->cosim_board_associated)
        return PARTS_UI_SUPPORT_NEEDS_RUNTIME_HOST;
    return PARTS_UI_SUPPORT_SUPPORTED_NOW;
}

static void path_join(char *out, size_t outsz, const char *dir, const char *name) {
    if (!out || outsz == 0) return;
    if (!dir || !*dir) { parts_ui_copy(out, outsz, name ? name : ""); return; }
    size_t dl = strlen(dir);
    if (dl > 0 && dir[dl - 1] == '/')
        snprintf(out, outsz, "%s%s", dir, name ? name : "");
    else
        snprintf(out, outsz, "%s/%s", dir, name ? name : "");
}

static void stem_from_wasm(const char *path, char *out, size_t outsz) {
    const char *base = path ? strrchr(path, '/') : NULL;
    base = base ? base + 1 : (path ? path : "");
    parts_ui_copy(out, outsz, base);
    size_t l = strlen(out);
    if (l >= 5 && parts_ui_has_suffix_ci(out, ".wasm")) out[l - 5] = '\0';
}

static int read_file_small(const char *path, char *buf, size_t bufsz) {
    FILE *f;
    size_t n;
    if (!path || !buf || bufsz == 0) return -1;
    f = fopen(path, "rb");
    if (!f) return -1;
    n = fread(buf, 1, bufsz - 1, f);
    fclose(f);
    buf[n] = '\0';
    return 0;
}

static bool json_extract_string(const char *json, const char *key, char *out, size_t outsz) {
    char pat[128];
    const char *p, *q;
    if (!json || !key || !out || outsz == 0) return false;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(json, pat);
    if (!p) return false;
    p = strchr(p + strlen(pat), ':');
    if (!p) return false;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '"') return false;
    p++;
    q = strchr(p, '"');
    if (!q) return false;
    size_t n = (size_t)(q - p);
    if (n >= outsz) n = outsz - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

static bool json_extract_int(const char *json, const char *key, int *out) {
    char pat[128];
    const char *p;
    if (!json || !key || !out) return false;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(json, pat);
    if (!p) return false;
    p = strchr(p + strlen(pat), ':');
    if (!p) return false;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '"') p++;
    *out = (int)strtol(p, NULL, 0);
    return true;
}

static void add_claim(parts_ui_part_meta_t *m, parts_ui_claim_kind_t kind, const char *bus,
                      int port, int pin, int addr, int cs_pin, int channel, const char *role) {
    if (!m || m->claim_count >= PARTS_UI_MAX_CLAIMS) return;
    parts_ui_claim_t *c = &m->claims[m->claim_count++];
    memset(c, 0, sizeof(*c));
    c->kind = kind;
    parts_ui_copy(c->bus, sizeof(c->bus), bus ? bus : "");
    c->port = port; c->pin = pin; c->addr = addr; c->cs_pin = cs_pin; c->channel = channel;
    parts_ui_copy(c->role, sizeof(c->role), role ? role : "");
}

static void parse_sidecar_claims(parts_ui_part_meta_t *m, const char *json) {
    /* Best-effort/minimal extraction: looks for common fields and simple claim hints. */
    int v;
    char s[PARTS_UI_MAX_SHORT];
    if (!m || !json) return;
    if (json_extract_int(json, "i2c_addr", &v) || json_extract_int(json, "addr", &v)) {
        s[0] = '\0'; json_extract_string(json, "bus", s, sizeof(s));
        add_claim(m, PARTS_UI_CLAIM_I2C_ADDR, s[0] ? s : "i2c", 0, -1, v, -1, -1, "addr");
    }
    if (json_extract_int(json, "cs", &v) || json_extract_int(json, "cs_pin", &v)) {
        s[0] = '\0'; json_extract_string(json, "bus", s, sizeof(s));
        add_claim(m, PARTS_UI_CLAIM_SPI_CS, s[0] ? s : "spi", 0, -1, -1, v, -1, "cs");
    }
    if (json_extract_int(json, "en_pin", &v) || json_extract_int(json, "en", &v)) {
        add_claim(m, PARTS_UI_CLAIM_EN, "", 0, v, -1, -1, -1, "en");
    }
    if (json_extract_int(json, "gpio", &v) || json_extract_int(json, "pin", &v)) {
        s[0] = '\0'; json_extract_string(json, "role", s, sizeof(s));
        add_claim(m, PARTS_UI_CLAIM_GPIO, "", 0, v, -1, -1, -1, s[0] ? s : "gpio");
    }
    if (json_extract_int(json, "uart", &v)) {
        add_claim(m, PARTS_UI_CLAIM_UART, "uart", v, -1, -1, -1, -1, "uart");
    }
    if (json_extract_int(json, "pwm", &v) || json_extract_int(json, "pwm_channel", &v)) {
        add_claim(m, PARTS_UI_CLAIM_PWM, "", 0, -1, -1, -1, v, "pwm");
    }
    if (json_extract_int(json, "adc", &v) || json_extract_int(json, "adc_channel", &v)) {
        add_claim(m, PARTS_UI_CLAIM_ADC, "", 0, -1, -1, -1, v, "adc");
    }
}

static void load_sidecar(parts_ui_part_meta_t *m, parts_ui_report_t *report) {
    char dir[PARTS_UI_MAX_PATH], stem[PARTS_UI_MAX_NAME], p1[PARTS_UI_MAX_PATH], p2[PARTS_UI_MAX_PATH];
    char buf[4096];
    const char *slash;
    if (!m) return;
    slash = strrchr(m->wasm_path, '/');
    if (slash) {
        size_t n = (size_t)(slash - m->wasm_path);
        if (n >= sizeof(dir)) n = sizeof(dir) - 1;
        memcpy(dir, m->wasm_path, n); dir[n] = '\0';
    } else {
        parts_ui_copy(dir, sizeof(dir), ".");
    }
    stem_from_wasm(m->wasm_path, stem, sizeof(stem));
    snprintf(p1, sizeof(p1), "%.*s/%.*s.json",
             (int)(sizeof(p1) - (int)sizeof(stem) - 8), dir,
             (int)sizeof(stem) - 1, stem);
    snprintf(p2, sizeof(p2), "%.*s/%.*s.part.json",
             (int)(sizeof(p2) - (int)sizeof(stem) - 13), dir,
             (int)sizeof(stem) - 1, stem);
    const char *use = NULL;
    if (parts_ui_is_file(p1)) use = p1;
    else if (parts_ui_is_file(p2)) use = p2;
    if (!use) return;
    m->has_sidecar = true;
    parts_ui_copy(m->sidecar_json_path, sizeof(m->sidecar_json_path), use);
    if (read_file_small(use, buf, sizeof(buf)) != 0) {
        parts_ui_report_add(report, PARTS_UI_DIAG_WARN, "SIDECAR_READ", m->name,
                            "could not read sidecar '%s'", use);
        return;
    }
    json_extract_string(buf, "name", m->name, sizeof(m->name));
    json_extract_string(buf, "abi", m->abi_version, sizeof(m->abi_version));
    if (!m->abi_version[0]) json_extract_string(buf, "abi_version", m->abi_version, sizeof(m->abi_version));
    json_extract_string(buf, "proto", m->proto_version, sizeof(m->proto_version));
    if (!m->proto_version[0]) json_extract_string(buf, "esp_cosim_proto", m->proto_version, sizeof(m->proto_version));
    {
        char note[PARTS_UI_MAX_TEXT];
        if (json_extract_string(buf, "support", note, sizeof(note)) ||
            json_extract_string(buf, "support_note", note, sizeof(note))) {
            parts_ui_copy(m->support_note, sizeof(m->support_note), note);
        }
    }
    parse_sidecar_claims(m, buf);
    /* store a few generic meta keys if present */
    const char *keys[] = {"board", "bus", "description", "version"};
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]) && m->meta_count < PARTS_UI_MAX_CLAIMS; i++) {
        char val[PARTS_UI_MAX_TEXT];
        if (json_extract_string(buf, keys[i], val, sizeof(val))) {
            parts_ui_kv_t *kv = &m->meta[m->meta_count++];
            parts_ui_copy(kv->key, sizeof(kv->key), keys[i]);
            parts_ui_copy(kv->value, sizeof(kv->value), val);
        }
    }
}

static int add_part_from_wasm(parts_ui_context_t *ctx, const char *wasm_path, parts_ui_report_t *report) {
    parts_ui_part_meta_t *m;
    char name[PARTS_UI_MAX_NAME];
    if (!ctx || !wasm_path) return -1;
    stem_from_wasm(wasm_path, name, sizeof(name));
    m = find_part(ctx, name);
    if (!m) {
        if (ctx->part_count >= PARTS_UI_MAX_PARTS) {
            parts_ui_report_add(report, PARTS_UI_DIAG_WARN, "PARTS_FULL", name, "parts list full, skipping");
            return -1;
        }
        m = &ctx->parts[ctx->part_count++];
        memset(m, 0, sizeof(*m));
        parts_ui_copy(m->name, sizeof(m->name), name);
    } else {
        /* refresh path/meta */
        char keep_name[PARTS_UI_MAX_NAME]; parts_ui_copy(keep_name, sizeof(keep_name), m->name);
        memset(m, 0, sizeof(*m));
        parts_ui_copy(m->name, sizeof(m->name), keep_name);
    }
    parts_ui_copy(m->wasm_path, sizeof(m->wasm_path), wasm_path);
    m->support = parts_ui_classify_support(ctx, m);
    parts_ui_copy(m->support_note, sizeof(m->support_note),
                  "WASM part support depends on esp-emu version, cosim ABI/proto and toolchain");
    load_sidecar(m, report);
    m->support = parts_ui_classify_support(ctx, m);
    return 0;
}

int parts_ui_discover_dir(parts_ui_context_t *ctx, const char *dir_path, parts_ui_report_t *report) {
    DIR *d;
    struct dirent *ent;
    int added = 0;
    if (!ctx || !dir_path) return -1;
    d = opendir(dir_path);
    if (!d) {
        parts_ui_report_add(report, PARTS_UI_DIAG_WARN, "DISCOVER_DIR", dir_path,
                            "cannot open dir: %s", strerror(errno));
        return -1;
    }
    while ((ent = readdir(d)) != NULL) {
        char full[PARTS_UI_MAX_PATH];
        if (ent->d_name[0] == '.') continue;
        if (!parts_ui_has_suffix_ci(ent->d_name, ".wasm")) continue;
        path_join(full, sizeof(full), dir_path, ent->d_name);
        if (!parts_ui_is_file(full)) continue;
        if (add_part_from_wasm(ctx, full, report) == 0) added++;
    }
    closedir(d);
    if (added == 0) {
        parts_ui_report_add(report, PARTS_UI_DIAG_INFO, "DISCOVER_EMPTY", dir_path,
                            "no .wasm parts found (conditional on toolchain/esp-part builds)");
    }
    return added;
}

int parts_ui_discover_paths(parts_ui_context_t *ctx, const char *paths, parts_ui_report_t *report) {
    char buf[2048];
    int added = 0;
    if (!ctx || !paths) return -1;
    parts_ui_copy(buf, sizeof(buf), paths);
    for (char *tok = strtok(buf, ",;"); tok; tok = strtok(NULL, ",;")) {
        while (*tok && isspace((unsigned char)*tok)) tok++;
        char *end = tok + strlen(tok);
        while (end > tok && isspace((unsigned char)end[-1])) *--end = '\0';
        if (!*tok) continue;
        if (parts_ui_is_dir(tok)) {
            int n = parts_ui_discover_dir(ctx, tok, report);
            if (n > 0) added += n;
        } else if (parts_ui_is_file(tok) && parts_ui_has_suffix_ci(tok, ".wasm")) {
            if (add_part_from_wasm(ctx, tok, report) == 0) added++;
        } else {
            parts_ui_report_add(report, PARTS_UI_DIAG_WARN, "DISCOVER_SKIP", tok,
                                "path not a dir or .wasm file");
        }
    }
    return added;
}

int parts_ui_remove_instance(parts_ui_context_t *ctx, const char *instance_id) {
    if (!ctx || !instance_id) return -1;
    for (int i = 0; i < ctx->instance_count; i++) {
        if (strcmp(ctx->instances[i].id, instance_id) == 0) {
            for (int j = i; j + 1 < ctx->instance_count; j++) ctx->instances[j] = ctx->instances[j + 1];
            ctx->instance_count--;
            return 0;
        }
    }
    return -1;
}

int parts_ui_configure_instance(parts_ui_context_t *ctx, const char *instance_id,
                                const char *part_name, const char *board_id,
                                const char *profile_id, const parts_ui_claim_t *wiring,
                                int wiring_count, bool enabled, parts_ui_report_t *report) {
    parts_ui_instance_t *inst;
    parts_ui_part_meta_t *meta;
    if (!ctx || !instance_id || !*instance_id || !part_name) return -1;
    meta = find_part(ctx, part_name);
    if (!meta) {
        parts_ui_report_add(report, PARTS_UI_DIAG_WARN, "PART_UNKNOWN", part_name,
                            "configuring instance '%s' for undiscovered part", instance_id);
    }
    inst = find_instance(ctx, instance_id);
    if (!inst) {
        if (ctx->instance_count >= PARTS_UI_MAX_INSTANCES) {
            parts_ui_report_add(report, PARTS_UI_DIAG_ERROR, "INST_FULL", instance_id, "instance list full");
            return -1;
        }
        inst = &ctx->instances[ctx->instance_count++];
        memset(inst, 0, sizeof(*inst));
        parts_ui_copy(inst->id, sizeof(inst->id), instance_id);
    }
    parts_ui_copy(inst->part_name, sizeof(inst->part_name), part_name);
    if (meta) parts_ui_copy(inst->wasm_path, sizeof(inst->wasm_path), meta->wasm_path);
    parts_ui_copy(inst->board_id, sizeof(inst->board_id), board_id ? board_id : "");
    parts_ui_copy(inst->profile_id, sizeof(inst->profile_id), profile_id ? profile_id : "");
    inst->enabled = enabled;
    inst->wiring_count = 0;
    if (wiring && wiring_count > 0) {
        if (wiring_count > PARTS_UI_MAX_CLAIMS) wiring_count = PARTS_UI_MAX_CLAIMS;
        memcpy(inst->wiring, wiring, (size_t)wiring_count * sizeof(parts_ui_claim_t));
        inst->wiring_count = wiring_count;
    } else if (meta && meta->claim_count > 0) {
        memcpy(inst->wiring, meta->claims, (size_t)meta->claim_count * sizeof(parts_ui_claim_t));
        inst->wiring_count = meta->claim_count;
    }
    inst->state = parts_ui_classify_support(ctx, meta);
    if (ctx->cosim_board_path[0] && inst->cosim_board_path[0] == '\0')
        parts_ui_copy(inst->cosim_board_path, sizeof(inst->cosim_board_path), ctx->cosim_board_path);
    inst->needs_sync = true;
    inst->generation++;
    parts_ui_copy(inst->status_text, sizeof(inst->status_text), "configured");
    inst->last_error[0] = '\0';
    return 0;
}

static bool same_bus(const char *a, const char *b) {
    if (!a || !*a || !b || !*b) return true; /* unspecified bus treated as potentially same */
    return strcmp(a, b) == 0;
}

static bool claims_conflict(const parts_ui_claim_t *a, const parts_ui_claim_t *b, char *why, size_t whysz) {
    if (!a || !b) return false;
    if (a->kind != b->kind) {
        /* GPIO pin overlap across different claim kinds (e.g. GPIO vs EN/PWM/ADC) */
        if (a->pin >= 0 && b->pin >= 0 && a->pin == b->pin &&
            (a->kind == PARTS_UI_CLAIM_GPIO || a->kind == PARTS_UI_CLAIM_EN ||
             b->kind == PARTS_UI_CLAIM_GPIO || b->kind == PARTS_UI_CLAIM_EN)) {
            snprintf(why, whysz, "pin %d reused", a->pin);
            return true;
        }
        return false;
    }
    switch (a->kind) {
        case PARTS_UI_CLAIM_GPIO:
        case PARTS_UI_CLAIM_EN:
            if (a->pin >= 0 && a->pin == b->pin) { snprintf(why, whysz, "GPIO/EN pin %d", a->pin); return true; }
            break;
        case PARTS_UI_CLAIM_I2C_ADDR:
            if (a->addr >= 0 && a->addr == b->addr && a->port == b->port && same_bus(a->bus, b->bus)) {
                snprintf(why, whysz, "I2C addr 0x%02X bus=%s port=%d", a->addr, a->bus[0]?a->bus:"?", a->port);
                return true;
            }
            break;
        case PARTS_UI_CLAIM_SPI_CS:
            if (a->cs_pin >= 0 && a->cs_pin == b->cs_pin && same_bus(a->bus, b->bus)) {
                snprintf(why, whysz, "SPI CS pin %d bus=%s", a->cs_pin, a->bus[0]?a->bus:"?");
                return true;
            }
            break;
        case PARTS_UI_CLAIM_UART:
            if (a->port >= 0 && a->port == b->port && same_bus(a->bus, b->bus)) {
                snprintf(why, whysz, "UART port %d", a->port);
                return true;
            }
            break;
        case PARTS_UI_CLAIM_PWM:
            if (a->channel >= 0 && a->channel == b->channel) {
                snprintf(why, whysz, "PWM channel %d", a->channel); return true;
            }
            break;
        case PARTS_UI_CLAIM_ADC:
            if (a->channel >= 0 && a->channel == b->channel) {
                snprintf(why, whysz, "ADC channel %d", a->channel); return true;
            }
            break;
        default: break;
    }
    return false;
}

int parts_ui_validate(parts_ui_context_t *ctx, parts_ui_report_t *report) {
    if (!ctx) return -1;
    int conflicts = 0;
    for (int i = 0; i < ctx->instance_count; i++) {
        parts_ui_instance_t *a = &ctx->instances[i];
        parts_ui_part_meta_t *meta = find_part(ctx, a->part_name);
        a->state = parts_ui_classify_support(ctx, meta);
        if (!a->enabled) {
            parts_ui_copy(a->status_text, sizeof(a->status_text), "disabled");
            continue;
        }
        if (!meta) {
            a->state = PARTS_UI_SUPPORT_BLOCKED_BY_EMU;
            parts_ui_copy(a->last_error, sizeof(a->last_error), "part metadata not discovered");
            parts_ui_report_add(report, PARTS_UI_DIAG_ERROR, "PART_MISSING", a->id,
                                "instance references missing part '%s'", a->part_name);
            conflicts++;
            continue;
        }
        if (a->wiring_count == 0) {
            parts_ui_report_add(report, PARTS_UI_DIAG_WARN, "NO_WIRING", a->id,
                                "no wiring/claims configured; integration needs explicit mapping");
        }
        for (int j = i + 1; j < ctx->instance_count; j++) {
            parts_ui_instance_t *b = &ctx->instances[j];
            if (!b->enabled) continue;
            for (int ca = 0; ca < a->wiring_count; ca++) {
                for (int cb = 0; cb < b->wiring_count; cb++) {
                    char why[128];
                    if (claims_conflict(&a->wiring[ca], &b->wiring[cb], why, sizeof(why))) {
                        parts_ui_report_add(report, PARTS_UI_DIAG_ERROR, "CLAIM_CONFLICT", a->id,
                                            "conflict with '%s': %s (%s/%s)", b->id, why,
                                            parts_ui_claim_kind_name(a->wiring[ca].kind),
                                            parts_ui_claim_kind_name(b->wiring[cb].kind));
                        parts_ui_copy(a->last_error, sizeof(a->last_error), "claim/wiring conflict");
                        parts_ui_copy(b->last_error, sizeof(b->last_error), "claim/wiring conflict");
                        a->state = PARTS_UI_SUPPORT_BLOCKED_BY_EMU;
                        b->state = PARTS_UI_SUPPORT_BLOCKED_BY_EMU;
                        conflicts++;
                    }
                }
            }
        }
        if (a->state == PARTS_UI_SUPPORT_CONDITIONAL_VERSION_TOOL) {
            parts_ui_copy(a->status_text, sizeof(a->status_text),
                          "conditional: needs esp-emu/ABI/toolchain with WASM part support");
        } else if (a->state == PARTS_UI_SUPPORT_NEEDS_RUNTIME_HOST) {
            if (a->attach_requested && a->attach_intent[0])
                snprintf(a->status_text, sizeof(a->status_text),
                         "needs runtime host/cosim integration (intent=%s target=%s)", a->attach_intent, a->attach_target[0] ? a->attach_target : "-");
            else
                parts_ui_copy(a->status_text, sizeof(a->status_text),
                              "needs runtime host/cosim integration (esp-emu --cosim-board/--cosim-tcp/--control-tcp available when configured)");
        } else if (a->state == PARTS_UI_SUPPORT_SUPPORTED_NOW) {
            parts_ui_copy(a->status_text, sizeof(a->status_text), "supported by current capability hints");
        }
    }
    if (ctx->part_count == 0) {
        parts_ui_report_add(report, PARTS_UI_DIAG_INFO, "NO_PARTS", "parts",
                            "no parts discovered in configured paths");
    }
    if (ctx->esp_emu_has_cosim_board_flag || ctx->esp_emu_has_cosim_tcp_flag) {
        parts_ui_report_add(report, PARTS_UI_DIAG_INFO, "ESP_EMU_FLAGS", "esp-emu",
                            "esp-emu %s exposes cosim flags (board=%s tcp=%s control-parts=%s); WASM part runtime remains conditional on module/toolchain/ABI",
                            ctx->esp_emu_version[0] ? ctx->esp_emu_version : "1.1.0",
                            ctx->esp_emu_has_cosim_board_flag ? "yes" : "no",
                            ctx->esp_emu_has_cosim_tcp_flag ? "yes" : "no",
                            ctx->esp_emu_has_control_part_cmds ? "yes" : "no");
    }
    if (ctx->cosim_board_associated && ctx->cosim_board_path[0]) {
        parts_ui_report_add(report, PARTS_UI_DIAG_INFO, "COSIM_BOARD", "cosim",
                            "associated cosim-board JSON: %s", ctx->cosim_board_path);
    }
    return (conflicts > 0 || report->errors > 0) ? -1 : 0;
}

int parts_ui_push_event(parts_ui_context_t *ctx, const char *kind, const char *instance_id,
                        const char *name, const char *value, uint64_t timestamp_ms) {
    parts_ui_event_t *e;
    int idx;
    if (!ctx) return -1;
    idx = (ctx->event_head + ctx->event_count) % PARTS_UI_MAX_EVENTS;
    if (ctx->event_count == PARTS_UI_MAX_EVENTS) {
        /* overwrite oldest */
        idx = ctx->event_head;
        ctx->event_head = (ctx->event_head + 1) % PARTS_UI_MAX_EVENTS;
    } else {
        ctx->event_count++;
    }
    e = &ctx->events[idx];
    memset(e, 0, sizeof(*e));
    parts_ui_copy(e->kind, sizeof(e->kind), kind ? kind : "event");
    parts_ui_copy(e->instance_id, sizeof(e->instance_id), instance_id ? instance_id : "");
    parts_ui_copy(e->name, sizeof(e->name), name ? name : "");
    parts_ui_copy(e->value, sizeof(e->value), value ? value : "");
    e->timestamp_ms = timestamp_ms;
    return 0;
}

int parts_ui_set_instance_state(parts_ui_context_t *ctx, const char *instance_id,
                                parts_ui_support_state_t st, bool attached,
                                const char *status_text, const char *last_error) {
    parts_ui_instance_t *inst = find_instance(ctx, instance_id);
    if (!inst) return -1;
    inst->state = st;
    inst->attached = attached;
    if (status_text) parts_ui_copy(inst->status_text, sizeof(inst->status_text), status_text);
    if (last_error) parts_ui_copy(inst->last_error, sizeof(inst->last_error), last_error);
    inst->generation++;
    return 0;
}

static int emit_instance_json(const parts_ui_instance_t *inst, char *out, size_t outsz) {
    if (!inst || !out || outsz == 0) return -1;
    /* Minimal JSON fragment for future profile.extensions / cosim board wiring. */
    snprintf(out, outsz,
             "{\"id\":\"%s\",\"part\":\"%s\",\"wasm\":\"%s\",\"enabled\":%s,\"state\":\"%s\",\"claims\":%d,"
             "\"board\":\"%s\",\"attach\":\"%s\",\"target\":\"%s\"}",
             inst->id, inst->part_name, inst->wasm_path, inst->enabled ? "true" : "false",
             parts_ui_support_state_name(inst->state), inst->wiring_count,
             inst->cosim_board_path[0] ? inst->cosim_board_path : "",
             inst->attach_intent[0] ? inst->attach_intent : "none",
             inst->attach_target[0] ? inst->attach_target : "");
    return 0;
}

int parts_ui_apply_profile_extensions(parts_ui_context_t *ctx, const parts_ui_hooks_t *hooks,
                                      const char *profile_id, parts_ui_report_t *report) {
    if (!ctx) return -1;
    if (!hooks || !hooks->apply_profile_extension) {
        parts_ui_report_add(report, PARTS_UI_DIAG_INFO, "HOOK_PROFILE", profile_id ? profile_id : "",
                            "no profile extension hook set (placeholder)");
        return 0;
    }
    for (int i = 0; i < ctx->instance_count; i++) {
        char payload[1024], err[256] = {0};
        parts_ui_instance_t *inst = &ctx->instances[i];
        if (profile_id && *profile_id && inst->profile_id[0] && strcmp(inst->profile_id, profile_id) != 0)
            continue;
        if (emit_instance_json(inst, payload, sizeof(payload)) != 0) continue;
        int rc = hooks->apply_profile_extension(hooks->user,
                                                inst->profile_id[0] ? inst->profile_id : (profile_id ? profile_id : ""),
                                                "sim.parts", payload, err, sizeof(err));
        if (rc != 0) {
            parts_ui_report_add(report, PARTS_UI_DIAG_WARN, "HOOK_PROFILE_FAIL", inst->id,
                                "profile extension hook failed: %s", err[0] ? err : "unknown");
        } else {
            inst->needs_sync = false;
        }
    }
    return 0;
}

int parts_ui_emit_cosim_requests(parts_ui_context_t *ctx, const parts_ui_hooks_t *hooks,
                                 parts_ui_report_t *report) {
    if (!ctx) return -1;
    if (!hooks || !hooks->emit_cosim_json) {
        parts_ui_report_add(report, PARTS_UI_DIAG_INFO, "HOOK_COSIM", "cosim",
                            "no cosim emit hook set (needs-runtime-host/integration placeholder; esp-emu 1.1.0 supports --cosim-board/--cosim-tcp)");
        return 0;
    }
    for (int i = 0; i < ctx->instance_count; i++) {
        char frag[1024], err[256] = {0};
        parts_ui_instance_t *inst = &ctx->instances[i];
        if (!inst->enabled) continue;
        if (emit_instance_json(inst, frag, sizeof(frag)) != 0) continue;
        int rc = hooks->emit_cosim_json(hooks->user, inst->id, frag, err, sizeof(err));
        if (rc != 0) {
            parts_ui_report_add(report, PARTS_UI_DIAG_WARN, "HOOK_COSIM_FAIL", inst->id,
                                "cosim emit failed: %s", err[0] ? err : "unknown");
        }
    }
    return 0;
}

int parts_ui_dispatch_input(parts_ui_context_t *ctx, const parts_ui_hooks_t *hooks,
                            const char *instance_id, const char *name, const char *value,
                            parts_ui_report_t *report) {
    char out[PARTS_UI_MAX_TEXT] = {0};
    parts_ui_instance_t *inst;
    if (!ctx || !instance_id) return -1;
    inst = find_instance(ctx, instance_id);
    if (!inst) {
        parts_ui_report_add(report, PARTS_UI_DIAG_ERROR, "INST_NOT_FOUND", instance_id, "unknown instance");
        return -1;
    }
    parts_ui_push_event(ctx, "input", instance_id, name ? name : "", value ? value : "", 0);
    if (hooks && hooks->send_input) {
        int rc = hooks->send_input(hooks->user, instance_id, name ? name : "", value ? value : "", out, sizeof(out));
        if (rc != 0) {
            parts_ui_report_add(report, PARTS_UI_DIAG_WARN, "INPUT_FAIL", instance_id,
                                "runtime input hook failed");
            return -1;
        }
        if (out[0]) parts_ui_push_event(ctx, "output", instance_id, name ? name : "ack", out, 0);
    } else {
        parts_ui_report_add(report, PARTS_UI_DIAG_INFO, "INPUT_PLACEHOLDER", instance_id,
                            "input queued as data event (no sim_host/cosim runtime hook)");
    }
    return 0;
}

int parts_ui_collect_outputs(parts_ui_context_t *ctx, const parts_ui_hooks_t *hooks,
                             parts_ui_report_t *report) {
    if (!ctx) return -1;
    if (!hooks || !hooks->poll_output) {
        parts_ui_report_add(report, PARTS_UI_DIAG_INFO, "OUTPUT_PLACEHOLDER", "parts",
                            "no output poll hook set");
        return 0;
    }
    for (int i = 0; i < ctx->instance_count; i++) {
        char out[PARTS_UI_MAX_TEXT] = {0};
        parts_ui_instance_t *inst = &ctx->instances[i];
        if (!inst->enabled) continue;
        int rc = hooks->poll_output(hooks->user, inst->id, "state", "", out, sizeof(out));
        if (rc == 0 && out[0]) {
            parts_ui_push_event(ctx, "output", inst->id, "state", out, 0);
        }
    }
    return 0;
}

void parts_ui_set_context_capabilities(parts_ui_context_t *ctx,
                                       const char *esp_emu_version,
                                       const char *esp_emu_binary,
                                       bool has_wasm_parts,
                                       bool has_control_part_cmds,
                                       bool has_cosim_board_flag,
                                       bool has_cosim_tcp_flag,
                                       bool host_cosim_tcp_available,
                                       bool host_cosim_board_available) {
    if (!ctx) return;
    if (esp_emu_version) parts_ui_copy(ctx->esp_emu_version, sizeof(ctx->esp_emu_version), esp_emu_version);
    if (esp_emu_binary) parts_ui_copy(ctx->esp_emu_binary, sizeof(ctx->esp_emu_binary), esp_emu_binary);
    ctx->esp_emu_has_wasm_parts = has_wasm_parts;
    ctx->esp_emu_has_control_part_cmds = has_control_part_cmds;
    ctx->esp_emu_has_cosim_board_flag = has_cosim_board_flag;
    ctx->esp_emu_has_cosim_tcp_flag = has_cosim_tcp_flag;
    ctx->host_cosim_tcp_available = host_cosim_tcp_available;
    ctx->host_cosim_board_available = host_cosim_board_available;
    for (int i = 0; i < ctx->part_count; i++) {
        ctx->parts[i].support = parts_ui_classify_support(ctx, &ctx->parts[i]);
    }
    for (int i = 0; i < ctx->instance_count; i++) {
        parts_ui_part_meta_t *meta = find_part(ctx, ctx->instances[i].part_name);
        ctx->instances[i].state = parts_ui_classify_support(ctx, meta);
    }
}

void parts_ui_set_cosim_board_path(parts_ui_context_t *ctx, const char *path, bool associated) {
    if (!ctx) return;
    parts_ui_copy(ctx->cosim_board_path, sizeof(ctx->cosim_board_path), path ? path : "");
    ctx->cosim_board_associated = associated || (path && *path);
    if (ctx->cosim_board_associated) ctx->host_cosim_board_available = true;
    for (int i = 0; i < ctx->instance_count; i++) {
        if (path && *path && ctx->instances[i].cosim_board_path[0] == '\0')
            parts_ui_copy(ctx->instances[i].cosim_board_path, sizeof(ctx->instances[i].cosim_board_path), path);
        parts_ui_part_meta_t *meta = find_part(ctx, ctx->instances[i].part_name);
        ctx->instances[i].state = parts_ui_classify_support(ctx, meta);
    }
}

int parts_ui_set_instance_runtime_intent(parts_ui_context_t *ctx, const char *instance_id,
                                         const char *attach_intent, const char *attach_target,
                                         bool attach_requested, const char *runtime_cmd,
                                         const char *cosim_board_path) {
    parts_ui_instance_t *inst = find_instance(ctx, instance_id);
    if (!inst) return -1;
    if (attach_intent) parts_ui_copy(inst->attach_intent, sizeof(inst->attach_intent), attach_intent);
    if (attach_target) parts_ui_copy(inst->attach_target, sizeof(inst->attach_target), attach_target);
    if (runtime_cmd) parts_ui_copy(inst->runtime_cmd, sizeof(inst->runtime_cmd), runtime_cmd);
    if (cosim_board_path) parts_ui_copy(inst->cosim_board_path, sizeof(inst->cosim_board_path), cosim_board_path);
    inst->attach_requested = attach_requested;
    inst->needs_sync = true;
    inst->generation++;
    return 0;
}

int parts_ui_format_cosim_board_json(const parts_ui_context_t *ctx, char *buf, size_t buflen,
                                     int only_enabled, parts_ui_report_t *report) {
    size_t off = 0;
    int n = 0;
    if (!ctx || !buf || buflen == 0) return -1;
    buf[0] = '\0';
    off += (size_t)snprintf(buf + off, buflen > off ? buflen - off : 0, "{\n  \"parts\": {");
    for (int i = 0; i < ctx->instance_count; i++) {
        const parts_ui_instance_t *inst = &ctx->instances[i];
        if (only_enabled && !inst->enabled) continue;
        if (inst->wasm_path[0] == '\0') {
            parts_ui_report_add(report, PARTS_UI_DIAG_WARN, "BOARD_WASM_MISSING", inst->id,
                                "instance has no wasm path; generated board entry may be incomplete");
        }
        off += (size_t)snprintf(buf + off, buflen > off ? buflen - off : 0,
                                "%s\n    \"%s\": { \"module\": \"%s\" }",
                                n++ ? "," : "", inst->id[0] ? inst->id : inst->part_name,
                                inst->wasm_path[0] ? inst->wasm_path : inst->part_name);
    }
    off += (size_t)snprintf(buf + off, buflen > off ? buflen - off : 0, "\n  }\n}\n");
    if (n == 0) {
        parts_ui_report_add(report, PARTS_UI_DIAG_INFO, "BOARD_EMPTY", "cosim",
                            "no instances selected for cosim board JSON (requires esp-emu --cosim-board when supported)");
    } else {
        parts_ui_report_add(report, PARTS_UI_DIAG_INFO, "BOARD_JSON", "cosim",
                            "generated board JSON for %d instance(s); pin/wiring mapping may need manual refinement", n);
    }
    return (int)off;
}

int parts_ui_describe_external_steps(const parts_ui_context_t *ctx, char *buf, size_t buflen,
                                     int include_validation) {
    size_t off = 0;
    int enabled = 0;
    if (!ctx || !buf || buflen == 0) return -1;
    buf[0] = '\0';
    for (int i = 0; i < ctx->instance_count; i++) if (ctx->instances[i].enabled) enabled++;
    off += (size_t)snprintf(buf + off, buflen > off ? buflen - off : 0,
                            "cosim/parts external steps (module-side, no runtime execution):\n");
    off += (size_t)snprintf(buf + off, buflen > off ? buflen - off : 0,
                            "- esp-emu=%s flags: cosim-board=%s cosim-tcp=%s control-parts=%s wasm-parts=%s\n",
                            ctx->esp_emu_version[0] ? ctx->esp_emu_version : "?",
                            ctx->esp_emu_has_cosim_board_flag ? "yes" : "no",
                            ctx->esp_emu_has_cosim_tcp_flag ? "yes" : "no",
                            ctx->esp_emu_has_control_part_cmds ? "yes" : "no",
                            ctx->esp_emu_has_wasm_parts ? "yes" : "conditional/no");
    off += (size_t)snprintf(buf + off, buflen > off ? buflen - off : 0,
                            "- instances=%d enabled=%d board_path=%s associated=%s\n",
                            ctx->instance_count, enabled,
                            ctx->cosim_board_path[0] ? ctx->cosim_board_path : "-",
                            ctx->cosim_board_associated ? "yes" : "no");
    if (!ctx->esp_emu_has_wasm_parts) {
        off += (size_t)snprintf(buf + off, buflen > off ? buflen - off : 0,
                                "- conditional: provide compatible .wasm parts + sidecar ABI/claims and esp-emu build/tool support for in-process parts\n");
    }
    off += (size_t)snprintf(buf + off, buflen > off ? buflen - off : 0,
                            "- if runtime host integration unavailable: generate cosim board JSON, run esp-emu with --cosim-board <FILE> and optional --cosim-tcp/--control-tcp, then attach external host/tools\n");
    if (include_validation) {
        off += (size_t)snprintf(buf + off, buflen > off ? buflen - off : 0,
                                "- run parts_ui_validate() and resolve claim conflicts/missing sidecars before attach/apply\n");
    }
    for (int i = 0; i < ctx->instance_count; i++) {
        const parts_ui_instance_t *inst = &ctx->instances[i];
        if (!inst->enabled) continue;
        off += (size_t)snprintf(buf + off, buflen > off ? buflen - off : 0,
                                "  * %s: part=%s wasm=%s intent=%s target=%s state=%s\n",
                                inst->id, inst->part_name,
                                inst->wasm_path[0] ? inst->wasm_path : "-",
                                inst->attach_intent[0] ? inst->attach_intent : "none",
                                inst->attach_target[0] ? inst->attach_target : "-",
                                parts_ui_support_state_name(inst->state));
    }
    return (int)off;
}

int parts_ui_build_cosim_cli_args(const parts_ui_context_t *ctx, const char *instance_id_or_null,
                                  char *buf, size_t buflen) {
    size_t off = 0;
    const parts_ui_instance_t *inst = NULL;
    if (!ctx || !buf || buflen == 0) return -1;
    buf[0] = '\0';
    if (instance_id_or_null && *instance_id_or_null) {
        for (int i = 0; i < ctx->instance_count; i++) {
            if (strcmp(ctx->instances[i].id, instance_id_or_null) == 0) { inst = &ctx->instances[i]; break; }
        }
    }
    if (ctx->cosim_board_path[0] || (inst && inst->cosim_board_path[0])) {
        off += (size_t)snprintf(buf + off, buflen > off ? buflen - off : 0,
                                "--cosim-board %s",
                                (inst && inst->cosim_board_path[0]) ? inst->cosim_board_path : ctx->cosim_board_path);
    }
    if (inst && inst->attach_requested && strcmp(inst->attach_intent, "tcp") == 0 && inst->attach_target[0]) {
        if (off) off += (size_t)snprintf(buf + off, buflen > off ? buflen - off : 0, " ");
        off += (size_t)snprintf(buf + off, buflen > off ? buflen - off : 0, "--cosim-tcp %s", inst->attach_target);
    } else if (inst && inst->attach_requested && strcmp(inst->attach_intent, "control") == 0 && inst->attach_target[0]) {
        if (off) off += (size_t)snprintf(buf + off, buflen > off ? buflen - off : 0, " ");
        off += (size_t)snprintf(buf + off, buflen > off ? buflen - off : 0, "--control-tcp %s", inst->attach_target);
    }
    if (!off) {
        off += (size_t)snprintf(buf + off, buflen > off ? buflen - off : 0,
                                "# conditional: no cosim board/attach args resolved (esp-emu --cosim-board/--cosim-tcp available in 1.1.0)");
    }
    return (int)off;
}
