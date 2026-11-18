#include "profile.h"
#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* ---------- utils ---------- */

static void sset(char *dst, size_t dstsz, const char *src) {
    if (!dst || dstsz == 0) return;
    if (!src) src = "";
    snprintf(dst, dstsz, "%s", src);
}

static char *sdup0(const char *s) {
    if (!s) s = "";
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

static bool file_exists(const char *path) {
    if (!path || !path[0]) return false;
    struct stat st;
    return stat(path, &st) == 0;
}

static bool is_abs_path(const char *p) {
    return p && p[0] == '/';
}

/* ---------- diagnostics ---------- */

void sim_profile_diags_init(sim_profile_diags_t *d) {
    if (!d) return;
    d->items = NULL; d->count = 0; d->cap = 0;
}

void sim_profile_diags_free(sim_profile_diags_t *d) {
    if (!d) return;
    free(d->items);
    d->items = NULL; d->count = 0; d->cap = 0;
}

void sim_profile_diags_add(sim_profile_diags_t *d, sim_profile_diag_level_t level,
                           const char *path, const char *fmt, ...) {
    if (!d) return;
    if (d->count == d->cap) {
        size_t ncap = d->cap ? d->cap * 2 : 8;
        sim_profile_diag_t *ni = (sim_profile_diag_t *)realloc(d->items, ncap * sizeof(*ni));
        if (!ni) return;
        d->items = ni; d->cap = ncap;
    }
    sim_profile_diag_t *it = &d->items[d->count++];
    it->level = level;
    sset(it->path, sizeof(it->path), path ? path : "");
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(it->message, sizeof(it->message), fmt ? fmt : "", ap);
    va_end(ap);
}

bool sim_profile_diags_has_errors(const sim_profile_diags_t *d) {
    if (!d) return false;
    for (size_t i = 0; i < d->count; i++) if (d->items[i].level == SIM_PROFILE_DIAG_ERROR) return true;
    return false;
}

/* ---------- lifecycle/defaults ---------- */

void sim_profile_init(sim_profile_t *p) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->board_hardware.display.hw_rotation = -1;
    p->board_hardware.cosim.tcp_port = 0;
    p->emu_runtime.timeout_sec = -1;
    p->ui_state.scale = 1.0;
    p->extensions_json = NULL;
    p->storage.card_enabled = true;
    snprintf(p->storage.card_image_path, sizeof(p->storage.card_image_path), "%s", LAUNCHER_SD_DEFAULT_IMAGE);
}

void sim_profile_free(sim_profile_t *p) {
    if (!p) return;
    free(p->extensions_json);
    p->extensions_json = NULL;
}

void sim_profile_defaults(sim_profile_t *p) {
    if (!p) return;
    sim_profile_free(p);
    sim_profile_init(p);
    sset(p->schema_version, sizeof(p->schema_version), SIM_PROFILE_SCHEMA_VERSION);
    sset(p->metadata.id, sizeof(p->metadata.id), "default-es3c28p");
    sset(p->metadata.name, sizeof(p->metadata.name), "Default ES3C28P");
    sset(p->metadata.board_preset, sizeof(p->metadata.board_preset), "es3c28p");

    sset(p->board_hardware.board_id, sizeof(p->board_hardware.board_id), "es3c28p");
    sset(p->network_radio.wifi_auth, sizeof(p->network_radio.wifi_auth), "unknown");
    sset(p->board_hardware.mcu.chip, sizeof(p->board_hardware.mcu.chip), "esp32s3");
    sset(p->board_hardware.mcu.psram, sizeof(p->board_hardware.mcu.psram), "2M");
    sset(p->board_hardware.mcu.flash, sizeof(p->board_hardware.mcu.flash), "16M");
    p->board_hardware.display.width = 240;
    p->board_hardware.display.height = 320;
    p->board_hardware.display.orientation = 0;
    p->board_hardware.display.hw_rotation = 0;
    sset(p->board_hardware.display.panel_name, sizeof(p->board_hardware.display.panel_name), "ILI9341");
    sset(p->board_hardware.touch.bus, sizeof(p->board_hardware.touch.bus), "spi");
    sset(p->board_hardware.cosim.mode, sizeof(p->board_hardware.cosim.mode), "board_json");
    sset(p->board_hardware.cosim.board_json, sizeof(p->board_hardware.cosim.board_json), "simulator/emu/board-es3c28p.json");

    sset(p->firmware.selection, sizeof(p->firmware.selection), "local_es3c28p_emu");
    sset(p->firmware.format, sizeof(p->firmware.format), "merged");
    sset(p->firmware.artifacts.merged, sizeof(p->firmware.artifacts.merged), "simulator/build/merged-emu.bin");
    sset(p->firmware.artifacts.elf, sizeof(p->firmware.artifacts.elf), "firmware/.pio/build/es3c28p_emu/firmware.elf");
    p->firmware.artifacts.use_elf = true;
    sset(p->firmware.flash.mode, sizeof(p->firmware.flash.mode), "dio");
    sset(p->firmware.flash.freq, sizeof(p->firmware.flash.freq), "80m");
    sset(p->firmware.flash.size, sizeof(p->firmware.flash.size), "16MB");
    sset(p->firmware.build_env, sizeof(p->firmware.build_env), "es3c28p_emu");

    p->audio.enabled = true;
    p->audio.realtime = false;

    sset(p->emu_runtime.esp_emu_path, sizeof(p->emu_runtime.esp_emu_path),
         EMU_DEFAULT_PATH);
    sset(p->emu_runtime.chip, sizeof(p->emu_runtime.chip), "esp32s3");
    sset(p->emu_runtime.psram, sizeof(p->emu_runtime.psram), "2M");
    p->emu_runtime.instance = 0;
    sset(p->emu_runtime.console_mode, sizeof(p->emu_runtime.console_mode), "tcp");

    p->ui_state.scale = 2.0;
    p->ui_state.window_maximized = false;

    p->extensions_json = sdup0("{}");
}

/* ---------- minimal JSON reader ---------- */

typedef struct {
    const char *s;
    size_t len;
    size_t i;
} jcur_t;

static void jskip_ws(jcur_t *c) {
    while (c->i < c->len) {
        unsigned char ch = (unsigned char)c->s[c->i];
        if (ch==' '||ch=='\n'||ch=='\r'||ch=='\t') c->i++; else break;
    }
}

static bool jpeek(jcur_t *c, char ch) { jskip_ws(c); return c->i < c->len && c->s[c->i] == ch; }
static bool jconsume(jcur_t *c, char ch) { if (jpeek(c,ch)) { c->i++; return true; } return false; }

/* parse JSON string, returns malloc'd unescaped string or NULL */
static char *jparse_string(jcur_t *c) {
    jskip_ws(c);
    if (c->i >= c->len || c->s[c->i] != '"') return NULL;
    c->i++;
    size_t cap = 64, n = 0;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;
    while (c->i < c->len) {
        char ch = c->s[c->i++];
        if (ch == '"') {
            out[n] = '\0';
            return out;
        }
        if (ch == '\\') {
            if (c->i >= c->len) { free(out); return NULL; }
            char e = c->s[c->i++];
            char v = e;
            switch (e) {
                case '"': v='"'; break; case '\\': v='\\'; break; case '/': v='/'; break;
                case 'b': v='\b'; break; case 'f': v='\f'; break; case 'n': v='\n'; break;
                case 'r': v='\r'; break; case 't': v='\t'; break;
                case 'u': {
                    /* minimal: skip 4 hex, emit '?' to stay simple but keep structure */
                    for (int k=0;k<4 && c->i<c->len;k++) c->i++;
                    v = '?';
                    break;
                }
                default: v = e; break;
            }
            ch = v;
        }
        if (n + 2 > cap) {
            cap *= 2;
            char *no = (char *)realloc(out, cap);
            if (!no) { free(out); return NULL; }
            out = no;
        }
        out[n++] = ch;
    }
    free(out);
    return NULL;
}

/* Skip any JSON value, returning pointer/len of raw text span in original buffer? easier copy raw */
static bool jskip_value(jcur_t *c); /* fwd */

static bool jskip_literal(jcur_t *c, const char *lit) {
    size_t n = strlen(lit);
    jskip_ws(c);
    if (c->i + n > c->len) return false;
    if (memcmp(c->s + c->i, lit, n) != 0) return false;
    c->i += n;
    return true;
}

static char *jcapture_raw(jcur_t *c) {
    jskip_ws(c);
    size_t start = c->i;
    if (!jskip_value(c)) return NULL;
    size_t end = c->i;
    char *out = (char *)malloc(end - start + 1);
    if (!out) return NULL;
    memcpy(out, c->s + start, end - start);
    out[end - start] = '\0';
    return out;
}

static bool jskip_value(jcur_t *c) {
    jskip_ws(c);
    if (c->i >= c->len) return false;
    char ch = c->s[c->i];
    if (ch == '"') { char *t = jparse_string(c); if (!t) return false; free(t); return true; }
    if (ch == '{') {
        c->i++;
        jskip_ws(c);
        if (jconsume(c, '}')) return true;
        for (;;) {
            char *k = jparse_string(c); if (!k) return false; free(k);
            if (!jconsume(c, ':')) return false;
            if (!jskip_value(c)) return false;
            jskip_ws(c);
            if (jconsume(c, '}')) return true;
            if (!jconsume(c, ',')) return false;
        }
    }
    if (ch == '[') {
        c->i++;
        jskip_ws(c);
        if (jconsume(c, ']')) return true;
        for (;;) {
            if (!jskip_value(c)) return false;
            jskip_ws(c);
            if (jconsume(c, ']')) return true;
            if (!jconsume(c, ',')) return false;
        }
    }
    if (jskip_literal(c, "true") || jskip_literal(c, "false") || jskip_literal(c, "null")) return true;
    /* number */
    if (ch == '-' || (ch >= '0' && ch <= '9')) {
        if (ch == '-') c->i++;
        while (c->i < c->len && isdigit((unsigned char)c->s[c->i])) c->i++;
        if (c->i < c->len && c->s[c->i] == '.') { c->i++; while (c->i < c->len && isdigit((unsigned char)c->s[c->i])) c->i++; }
        if (c->i < c->len && (c->s[c->i]=='e' || c->s[c->i]=='E')) {
            c->i++; if (c->i < c->len && (c->s[c->i]=='+'||c->s[c->i]=='-')) c->i++;
            while (c->i < c->len && isdigit((unsigned char)c->s[c->i])) c->i++;
        }
        return true;
    }
    return false;
}

static bool jparse_bool(jcur_t *c, bool *out) {
    jskip_ws(c);
    if (jskip_literal(c, "true")) { *out = true; return true; }
    if (jskip_literal(c, "false")) { *out = false; return true; }
    return false;
}

static bool jparse_int(jcur_t *c, int *out) {
    jskip_ws(c);
    size_t start = c->i;
    if (!jskip_value(c)) return false; /* reuse numeric parse validation loosely */
    char tmp[64]; size_t n = c->i - start; if (n >= sizeof(tmp)) n = sizeof(tmp)-1;
    memcpy(tmp, c->s + start, n); tmp[n] = '\0';
    char *e=NULL; long v = strtol(tmp, &e, 10);
    if (e == tmp) return false;
    *out = (int)v; return true;
}

static bool jparse_double(jcur_t *c, double *out) {
    jskip_ws(c);
    size_t start = c->i;
    if (!jskip_value(c)) return false;
    char tmp[64]; size_t n = c->i - start; if (n >= sizeof(tmp)) n = sizeof(tmp)-1;
    memcpy(tmp, c->s + start, n); tmp[n] = '\0';
    char *e=NULL; double v = strtod(tmp, &e);
    if (e == tmp) return false;
    *out = v; return true;
}

/* parse string-or-null-or-number-or-bool into string buffer */
static bool jparse_scalar_to_str(jcur_t *c, char *out, size_t outsz) {
    jskip_ws(c);
    if (c->i >= c->len) return false;
    if (c->s[c->i] == '"') {
        char *s = jparse_string(c); if (!s) return false; sset(out, outsz, s); free(s); return true;
    }
    if (jskip_literal(c, "null")) { sset(out, outsz, ""); return true; }
    if (jskip_literal(c, "true")) { sset(out, outsz, "true"); return true; }
    if (jskip_literal(c, "false")) { sset(out, outsz, "false"); return true; }
    /* number */
    size_t start = c->i;
    if (!jskip_value(c)) return false;
    size_t n = c->i - start; if (n >= outsz) n = outsz - 1;
    memcpy(out, c->s + start, n); out[n] = '\0';
    return true;
}

static bool jexpect_obj_start(jcur_t *c) { return jconsume(c, '{'); }

static bool jparse_string_array(jcur_t *c, char items[][SIM_PROFILE_MAX_TAGLEN], size_t max_items, size_t *count) {
    *count = 0;
    if (!jconsume(c, '[')) return false;
    jskip_ws(c);
    if (jconsume(c, ']')) return true;
    for (;;) {
        char *s = jparse_string(c);
        if (!s) return false;
        if (*count < max_items) sset(items[*count], SIM_PROFILE_MAX_TAGLEN, s);
        (*count)++;
        free(s);
        jskip_ws(c);
        if (jconsume(c, ']')) return true;
        if (!jconsume(c, ',')) return false;
    }
}

static bool jparse_hostfwd_array(jcur_t *c, char items[][SIM_PROFILE_MAX_ARGS], size_t max_items, size_t *count) {
    *count = 0;
    if (!jconsume(c, '[')) return false;
    jskip_ws(c);
    if (jconsume(c, ']')) return true;
    for (;;) {
        char *s = jparse_string(c);
        if (!s) return false;
        if (*count < max_items) sset(items[*count], SIM_PROFILE_MAX_ARGS, s);
        (*count)++;
        free(s);
        jskip_ws(c);
        if (jconsume(c, ']')) return true;
        if (!jconsume(c, ',')) return false;
    }
}

/* Forward decls for object parsers */
static bool parse_metadata(jcur_t *c, sim_profile_metadata_t *m);
static bool parse_board_hardware(jcur_t *c, sim_profile_board_hw_t *b);
static bool parse_firmware(jcur_t *c, sim_profile_firmware_t *f);
static bool parse_storage(jcur_t *c, sim_profile_storage_t *s);
static bool parse_audio(jcur_t *c, sim_profile_audio_t *a);
static bool parse_network(jcur_t *c, sim_profile_network_t *n);
static bool parse_emu_runtime(jcur_t *c, sim_profile_emu_runtime_t *e);
static bool parse_ui_state(jcur_t *c, sim_profile_ui_state_t *u);

static bool parse_metadata(jcur_t *c, sim_profile_metadata_t *m) {
    if (!jexpect_obj_start(c)) return false;
    jskip_ws(c);
    if (jconsume(c, '}')) return true;
    for (;;) {
        char *k = jparse_string(c); if (!k) return false;
        if (!jconsume(c, ':')) { free(k); return false; }
        if (strcmp(k, "id") == 0) { char *v=jparse_string(c); if(!v){free(k);return false;} sset(m->id,sizeof(m->id),v); free(v); }
        else if (strcmp(k, "name") == 0) { char *v=jparse_string(c); if(!v){free(k);return false;} sset(m->name,sizeof(m->name),v); free(v); }
        else if (strcmp(k, "description") == 0) { if (!jparse_scalar_to_str(c,m->description,sizeof(m->description))) {free(k);return false;} }
        else if (strcmp(k, "board_preset") == 0) { char *v=jparse_string(c); if(!v){free(k);return false;} sset(m->board_preset,sizeof(m->board_preset),v); free(v); }
        else if (strcmp(k, "tags") == 0) { if (!jparse_string_array(c, m->tags, SIM_PROFILE_MAX_TAGS, &m->tag_count)) {free(k);return false;} }
        else if (strcmp(k, "timestamps") == 0) {
            if (!jexpect_obj_start(c)) { free(k); return false; }
            jskip_ws(c);
            if (!jconsume(c, '}')) {
                for (;;) {
                    char *tk=jparse_string(c); if(!tk){free(k);return false;} if(!jconsume(c,':')){free(tk);free(k);return false;}
                    if (strcmp(tk,"created_at")==0) { if(!jparse_scalar_to_str(c,m->created_at,sizeof(m->created_at))){free(tk);free(k);return false;} }
                    else if (strcmp(tk,"updated_at")==0) { if(!jparse_scalar_to_str(c,m->updated_at,sizeof(m->updated_at))){free(tk);free(k);return false;} }
                    else { if(!jskip_value(c)){free(tk);free(k);return false;} }
                    free(tk);
                    jskip_ws(c); if (jconsume(c,'}')) break; if(!jconsume(c,',')){free(k);return false;}
                }
            }
        } else { if (!jskip_value(c)) { free(k); return false; } }
        free(k);
        jskip_ws(c);
        if (jconsume(c, '}')) return true;
        if (!jconsume(c, ',')) return false;
    }
}

static bool parse_mcu(jcur_t *c, sim_profile_mcu_t *m) {
    if (!jexpect_obj_start(c)) return false;
    jskip_ws(c); if (jconsume(c,'}')) return true;
    for (;;) {
        char *k=jparse_string(c); if(!k) return false; if(!jconsume(c,':')){free(k);return false;}
        if (strcmp(k,"chip")==0) { if(!jparse_scalar_to_str(c,m->chip,sizeof(m->chip))){free(k);return false;} }
        else if (strcmp(k,"psram")==0) { if(!jparse_scalar_to_str(c,m->psram,sizeof(m->psram))){free(k);return false;} }
        else if (strcmp(k,"flash")==0) { if(!jparse_scalar_to_str(c,m->flash,sizeof(m->flash))){free(k);return false;} }
        else { if(!jskip_value(c)){free(k);return false;} }
        free(k);
        jskip_ws(c); if (jconsume(c,'}')) return true; if(!jconsume(c,',')) return false;
    }
}

static bool parse_display(jcur_t *c, sim_profile_display_t *d) {
    if (!jexpect_obj_start(c)) return false;
    jskip_ws(c); if (jconsume(c,'}')) return true;
    for (;;) {
        char *k=jparse_string(c); if(!k) return false; if(!jconsume(c,':')){free(k);return false;}
        if (strcmp(k,"width")==0) { if(!jparse_int(c,&d->width)){free(k);return false;} }
        else if (strcmp(k,"height")==0) { if(!jparse_int(c,&d->height)){free(k);return false;} }
        else if (strcmp(k,"orientation")==0) { if(!jparse_int(c,&d->orientation)){free(k);return false;} }
        else if (strcmp(k,"panel")==0) {
            if (!jexpect_obj_start(c)) {free(k);return false;}
            jskip_ws(c); if(!jconsume(c,'}')){
                for(;;){ char *pk=jparse_string(c); if(!pk){free(k);return false;} if(!jconsume(c,':')){free(pk);free(k);return false;}
                    if(strcmp(pk,"name")==0){ if(!jparse_scalar_to_str(c,d->panel_name,sizeof(d->panel_name))){free(pk);free(k);return false;} }
                    else { if(!jskip_value(c)){free(pk);free(k);return false;} }
                    free(pk); jskip_ws(c); if(jconsume(c,'}')) break; if(!jconsume(c,',')){free(k);return false;} }
            }
        } else if (strcmp(k,"overrides")==0) {
            if (!jexpect_obj_start(c)) {free(k);return false;}
            jskip_ws(c); if(!jconsume(c,'}')){
                for(;;){ char *ok=jparse_string(c); if(!ok){free(k);return false;} if(!jconsume(c,':')){free(ok);free(k);return false;}
                    if(strcmp(ok,"hw_rotation")==0 || strcmp(ok,"hw_rot")==0){ int v=0; if(!jparse_int(c,&v)){free(ok);free(k);return false;} d->hw_rotation=v; }
                    else { if(!jskip_value(c)){free(ok);free(k);return false;} }
                    free(ok); jskip_ws(c); if(jconsume(c,'}')) break; if(!jconsume(c,',')){free(k);return false;} }
            }
        } else { if(!jskip_value(c)){free(k);return false;} }
        free(k);
        jskip_ws(c); if (jconsume(c,'}')) return true; if(!jconsume(c,',')) return false;
    }
}

static bool parse_pin_map(jcur_t *c, sim_profile_pin_t *pins, size_t max_pins, size_t *count) {
    *count = 0;
    if (!jexpect_obj_start(c)) return false;
    jskip_ws(c); if (jconsume(c,'}')) return true;
    for (;;) {
        char *k=jparse_string(c); if(!k) return false; if(!jconsume(c,':')){free(k);return false;}
        char v[SIM_PROFILE_MAX_SHORT];
        if(!jparse_scalar_to_str(c,v,sizeof(v))){free(k);return false;}
        if (*count < max_pins) {
            sset(pins[*count].key, sizeof(pins[*count].key), k);
            sset(pins[*count].value, sizeof(pins[*count].value), v);
            (*count)++;
        }
        free(k);
        jskip_ws(c); if (jconsume(c,'}')) return true; if(!jconsume(c,',')) return false;
    }
}

static bool parse_touch(jcur_t *c, sim_profile_touch_t *t) {
    if (!jexpect_obj_start(c)) return false;
    jskip_ws(c); if (jconsume(c,'}')) return true;
    for (;;) {
        char *k=jparse_string(c); if(!k) return false; if(!jconsume(c,':')){free(k);return false;}
        if (strcmp(k,"controller")==0) { if(!jparse_scalar_to_str(c,t->controller,sizeof(t->controller))){free(k);return false;} }
        else if (strcmp(k,"bus")==0) { if(!jparse_scalar_to_str(c,t->bus,sizeof(t->bus))){free(k);return false;} }
        else if (strcmp(k,"pins")==0) { if(!parse_pin_map(c,t->pins,SIM_PROFILE_MAX_PINS,&t->pin_count)){free(k);return false;} }
        else { if(!jskip_value(c)){free(k);return false;} }
        free(k);
        jskip_ws(c); if (jconsume(c,'}')) return true; if(!jconsume(c,',')) return false;
    }
}

static bool parse_cosim(jcur_t *c, sim_profile_cosim_t *cs) {
    if (!jexpect_obj_start(c)) return false;
    jskip_ws(c); if (jconsume(c,'}')) return true;
    for (;;) {
        char *k=jparse_string(c); if(!k) return false; if(!jconsume(c,':')){free(k);return false;}
        if (strcmp(k,"mode")==0) { if(!jparse_scalar_to_str(c,cs->mode,sizeof(cs->mode))){free(k);return false;} }
        else if (strcmp(k,"board_json")==0) { if(!jparse_scalar_to_str(c,cs->board_json,sizeof(cs->board_json))){free(k);return false;} }
        else if (strcmp(k,"tcp")==0) {
            jskip_ws(c);
            if (jskip_literal(c,"null")) { /* keep */ }
            else {
                if(!jexpect_obj_start(c)){free(k);return false;} jskip_ws(c);
                if(!jconsume(c,'}')){
                    for(;;){ char *tk=jparse_string(c); if(!tk){free(k);return false;} if(!jconsume(c,':')){free(tk);free(k);return false;}
                        if(strcmp(tk,"host")==0){ if(!jparse_scalar_to_str(c,cs->tcp_host,sizeof(cs->tcp_host))){free(tk);free(k);return false;} }
                        else if(strcmp(tk,"port")==0){ if(!jparse_int(c,&cs->tcp_port)){free(tk);free(k);return false;} }
                        else { if(!jskip_value(c)){free(tk);free(k);return false;} }
                        free(tk); jskip_ws(c); if(jconsume(c,'}')) break; if(!jconsume(c,',')){free(k);return false;} }
                }
            }
        } else if (strcmp(k,"unix")==0) {
            jskip_ws(c);
            if (jskip_literal(c,"null")) { }
            else {
                if(!jexpect_obj_start(c)){free(k);return false;} jskip_ws(c);
                if(!jconsume(c,'}')){
                    for(;;){ char *uk=jparse_string(c); if(!uk){free(k);return false;} if(!jconsume(c,':')){free(uk);free(k);return false;}
                        if(strcmp(uk,"path")==0){ if(!jparse_scalar_to_str(c,cs->unix_path,sizeof(cs->unix_path))){free(uk);free(k);return false;} }
                        else { if(!jskip_value(c)){free(uk);free(k);return false;} }
                        free(uk); jskip_ws(c); if(jconsume(c,'}')) break; if(!jconsume(c,',')){free(k);return false;} }
                }
            }
        } else { if(!jskip_value(c)){free(k);return false;} }
        free(k);
        jskip_ws(c); if (jconsume(c,'}')) return true; if(!jconsume(c,',')) return false;
    }
}

static bool parse_board_hardware(jcur_t *c, sim_profile_board_hw_t *b) {
    if (!jexpect_obj_start(c)) return false;
    jskip_ws(c); if (jconsume(c,'}')) return true;
    for (;;) {
        char *k=jparse_string(c); if(!k) return false; if(!jconsume(c,':')){free(k);return false;}
        if (strcmp(k,"board_id")==0) { if(!jparse_scalar_to_str(c,b->board_id,sizeof(b->board_id))){free(k);return false;} }
        else if (strcmp(k,"mcu")==0) { if(!parse_mcu(c,&b->mcu)){free(k);return false;} }
        else if (strcmp(k,"display")==0) { if(!parse_display(c,&b->display)){free(k);return false;} }
        else if (strcmp(k,"touch")==0) { if(!parse_touch(c,&b->touch)){free(k);return false;} }
        else if (strcmp(k,"cosim")==0) { if(!parse_cosim(c,&b->cosim)){free(k);return false;} }
        else { if(!jskip_value(c)){free(k);return false;} }
        free(k);
        jskip_ws(c); if (jconsume(c,'}')) return true; if(!jconsume(c,',')) return false;
    }
}

static bool parse_artifacts(jcur_t *c, sim_profile_fw_artifacts_t *a) {
    if (!jexpect_obj_start(c)) return false;
    jskip_ws(c); if (jconsume(c,'}')) return true;
    for (;;) {
        char *k=jparse_string(c); if(!k) return false; if(!jconsume(c,':')){free(k);return false;}
        if (strcmp(k,"merged")==0) { if(!jparse_scalar_to_str(c,a->merged,sizeof(a->merged))){free(k);return false;} }
        else if (strcmp(k,"boot")==0) { if(!jparse_scalar_to_str(c,a->boot,sizeof(a->boot))){free(k);return false;} }
        else if (strcmp(k,"part")==0) { if(!jparse_scalar_to_str(c,a->part,sizeof(a->part))){free(k);return false;} }
        else if (strcmp(k,"app")==0) { if(!jparse_scalar_to_str(c,a->app,sizeof(a->app))){free(k);return false;} }
        else if (strcmp(k,"elf")==0) { if(!jparse_scalar_to_str(c,a->elf,sizeof(a->elf))){free(k);return false;} }
        else if (strcmp(k,"use_elf")==0) { if(!jparse_bool(c,&a->use_elf)){free(k);return false;} }
        else if (strcmp(k,"littlefs")==0) { if(!jparse_scalar_to_str(c,a->littlefs,sizeof(a->littlefs))){free(k);return false;} }
        else if (strcmp(k,"uf2")==0) { if(!jparse_scalar_to_str(c,a->uf2,sizeof(a->uf2))){free(k);return false;} }
        else { if(!jskip_value(c)){free(k);return false;} }
        free(k);
        jskip_ws(c); if (jconsume(c,'}')) return true; if(!jconsume(c,',')) return false;
    }
}

static bool parse_fw_flash(jcur_t *c, sim_profile_fw_flash_t *f) {
    if (!jexpect_obj_start(c)) return false;
    jskip_ws(c); if (jconsume(c,'}')) return true;
    for (;;) {
        char *k=jparse_string(c); if(!k) return false; if(!jconsume(c,':')){free(k);return false;}
        if (strcmp(k,"mode")==0) { if(!jparse_scalar_to_str(c,f->mode,sizeof(f->mode))){free(k);return false;} }
        else if (strcmp(k,"freq")==0) { if(!jparse_scalar_to_str(c,f->freq,sizeof(f->freq))){free(k);return false;} }
        else if (strcmp(k,"size")==0) { if(!jparse_scalar_to_str(c,f->size,sizeof(f->size))){free(k);return false;} }
        else if (strcmp(k,"offsets")==0) {
            if(!jexpect_obj_start(c)){free(k);return false;} jskip_ws(c); if(!jconsume(c,'}')){
                for(;;){ char *ok=jparse_string(c); if(!ok){free(k);return false;} if(!jconsume(c,':')){free(ok);free(k);return false;}
                    if(strcmp(ok,"bootloader")==0) jparse_scalar_to_str(c,f->bootloader_offset,sizeof(f->bootloader_offset));
                    else if(strcmp(ok,"partitions")==0) jparse_scalar_to_str(c,f->partitions_offset,sizeof(f->partitions_offset));
                    else if(strcmp(ok,"app")==0) jparse_scalar_to_str(c,f->app_offset,sizeof(f->app_offset));
                    else { if(!jskip_value(c)){free(ok);free(k);return false;} }
                    free(ok); jskip_ws(c); if(jconsume(c,'}')) break; if(!jconsume(c,',')){free(k);return false;} }
            }
        } else { if(!jskip_value(c)){free(k);return false;} }
        free(k);
        jskip_ws(c); if (jconsume(c,'}')) return true; if(!jconsume(c,',')) return false;
    }
}

static bool parse_firmware(jcur_t *c, sim_profile_firmware_t *f) {
    if (!jexpect_obj_start(c)) return false;
    jskip_ws(c); if (jconsume(c,'}')) return true;
    for (;;) {
        char *k=jparse_string(c); if(!k) return false; if(!jconsume(c,':')){free(k);return false;}
        if (strcmp(k,"selection")==0) { if(!jparse_scalar_to_str(c,f->selection,sizeof(f->selection))){free(k);return false;} }
        else if (strcmp(k,"format")==0) { if(!jparse_scalar_to_str(c,f->format,sizeof(f->format))){free(k);return false;} }
        else if (strcmp(k,"artifacts")==0) { if(!parse_artifacts(c,&f->artifacts)){free(k);return false;} }
        else if (strcmp(k,"flash")==0) { if(!parse_fw_flash(c,&f->flash)){free(k);return false;} }
        else if (strcmp(k,"build")==0) {
            if(!jexpect_obj_start(c)){free(k);return false;} jskip_ws(c); if(!jconsume(c,'}')){
                for(;;){ char *bk=jparse_string(c); if(!bk){free(k);return false;} if(!jconsume(c,':')){free(bk);free(k);return false;}
                    if(strcmp(bk,"env")==0){ if(!jparse_scalar_to_str(c,f->build_env,sizeof(f->build_env))){free(bk);free(k);return false;} }
                    else { if(!jskip_value(c)){free(bk);free(k);return false;} }
                    free(bk); jskip_ws(c); if(jconsume(c,'}')) break; if(!jconsume(c,',')){free(k);return false;} }
            }
        } else { if(!jskip_value(c)){free(k);return false;} }
        free(k);
        jskip_ws(c); if (jconsume(c,'}')) return true; if(!jconsume(c,',')) return false;
    }
}

static bool parse_storage(jcur_t *c, sim_profile_storage_t *s) {
    if (!jexpect_obj_start(c)) return false;
    jskip_ws(c); if (jconsume(c,'}')) return true;
    for (;;) {
        char *k=jparse_string(c); if(!k) return false; if(!jconsume(c,':')){free(k);return false;}
        if (strcmp(k,"littlefs")==0) {
            if(!jexpect_obj_start(c)){free(k);return false;} jskip_ws(c); if(!jconsume(c,'}')){
                for(;;){ char *lk=jparse_string(c); if(!lk){free(k);return false;} if(!jconsume(c,':')){free(lk);free(k);return false;}
                    if(strcmp(lk,"enabled")==0) jparse_bool(c,&s->littlefs_enabled);
                    else if(strcmp(lk,"image_path")==0) jparse_scalar_to_str(c,s->littlefs_image_path,sizeof(s->littlefs_image_path));
                    else if(strcmp(lk,"mount_path")==0) jparse_scalar_to_str(c,s->littlefs_mount_path,sizeof(s->littlefs_mount_path));
                    else { if(!jskip_value(c)){free(lk);free(k);return false;} }
                    free(lk); jskip_ws(c); if(jconsume(c,'}')) break; if(!jconsume(c,',')){free(k);return false;} }
            }
        } else if (strcmp(k,"sd_enabled")==0) {
            jparse_bool(c,&s->card_enabled);
        } else if (strcmp(k,"sd_image_path")==0) {
            jparse_scalar_to_str(c,s->card_image_path,sizeof(s->card_image_path));
        } else if (strcmp(k,"sd_dir")==0) {
            if(!jparse_scalar_to_str(c,s->sd_dir,sizeof(s->sd_dir))){free(k);return false;}
        } else if (strcmp(k,"sd_card")==0) {
            if(!jexpect_obj_start(c)){free(k);return false;} jskip_ws(c); if(!jconsume(c,'}')){
                for(;;){ char *sk=jparse_string(c); if(!sk){free(k);return false;} if(!jconsume(c,':')){free(sk);free(k);return false;}
                    if(strcmp(sk,"enabled")==0) jparse_bool(c,&s->sd_enabled);
                    else if(strcmp(sk,"image_path")==0) jparse_scalar_to_str(c,s->sd_image_path,sizeof(s->sd_image_path));
                    else if(strcmp(sk,"mount_path")==0) jparse_scalar_to_str(c,s->sd_mount_path,sizeof(s->sd_mount_path));
                    else { if(!jskip_value(c)){free(sk);free(k);return false;} }
                    free(sk); jskip_ws(c); if(jconsume(c,'}')) break; if(!jconsume(c,',')){free(k);return false;} }
            }
        } else { if(!jskip_value(c)){free(k);return false;} }
        free(k);
        jskip_ws(c); if (jconsume(c,'}')) return true; if(!jconsume(c,',')) return false;
    }
}

static bool parse_audio(jcur_t *c, sim_profile_audio_t *a) {
    if (!jexpect_obj_start(c)) return false;
    jskip_ws(c); if (jconsume(c,'}')) return true;
    for (;;) {
        char *k=jparse_string(c); if(!k) return false; if(!jconsume(c,':')){free(k);return false;}
        if (strcmp(k,"enabled")==0) { if(!jparse_bool(c,&a->enabled)){free(k);return false;} }
        else if (strcmp(k,"realtime")==0) { if(!jparse_bool(c,&a->realtime)){free(k);return false;} }
        else if (strcmp(k,"out")==0) { if(!jparse_scalar_to_str(c,a->out,sizeof(a->out))){free(k);return false;} }
        else if (strcmp(k,"in")==0) { if(!jparse_scalar_to_str(c,a->in,sizeof(a->in))){free(k);return false;} }
        else { if(!jskip_value(c)){free(k);return false;} }
        free(k);
        jskip_ws(c); if (jconsume(c,'}')) return true; if(!jconsume(c,',')) return false;
    }
}

static bool parse_network(jcur_t *c, sim_profile_network_t *n) {
    if (!jexpect_obj_start(c)) return false;
    jskip_ws(c); if (jconsume(c,'}')) return true;
    for (;;) {
        char *k=jparse_string(c); if(!k) return false; if(!jconsume(c,':')){free(k);return false;}
        if (strcmp(k,"net_user")==0) { if(!jparse_bool(c,&n->net_user)){free(k);return false;} }
        else if (strcmp(k,"net_mode")==0) { if(!jparse_scalar_to_str(c,n->net_mode,sizeof(n->net_mode))){free(k);return false;} }
        else if (strcmp(k,"hostfwd")==0) { if(!jparse_hostfwd_array(c,n->hostfwd,SIM_PROFILE_MAX_HOSTFWD,&n->hostfwd_count)){free(k);return false;} }
        else if (strcmp(k,"wifi")==0) {
            if(!jexpect_obj_start(c)){free(k);return false;} jskip_ws(c); if(!jconsume(c,'}')){
                for(;;){ char *wk=jparse_string(c); if(!wk){free(k);return false;} if(!jconsume(c,':')){free(wk);free(k);return false;}
                    if(strcmp(wk,"enabled")==0) jparse_bool(c,&n->wifi_enabled);
                    else if(strcmp(wk,"ssid")==0) { if(!jparse_scalar_to_str(c,n->wifi_ssid,sizeof(n->wifi_ssid))){free(wk);free(k);return false;} }
                    else if(strcmp(wk,"auth")==0) { if(!jparse_scalar_to_str(c,n->wifi_auth,sizeof(n->wifi_auth))){free(wk);free(k);return false;} }
                    else { if(!jskip_value(c)){free(wk);free(k);return false;} }
                    free(wk); jskip_ws(c); if(jconsume(c,'}')) break; if(!jconsume(c,',')){free(k);return false;} }
            }
        } else if (strcmp(k,"ble")==0) {
            if(!jexpect_obj_start(c)){free(k);return false;} jskip_ws(c); if(!jconsume(c,'}')){
                for(;;){ char *bk=jparse_string(c); if(!bk){free(k);return false;} if(!jconsume(c,':')){free(bk);free(k);return false;}
                    if(strcmp(bk,"enabled")==0) jparse_bool(c,&n->ble_enabled);
                    else if(strcmp(bk,"hci")==0) { if(!jparse_scalar_to_str(c,n->ble_hci,sizeof(n->ble_hci))){free(bk);free(k);return false;} }
                    else { if(!jskip_value(c)){free(bk);free(k);return false;} }
                    free(bk); jskip_ws(c); if(jconsume(c,'}')) break; if(!jconsume(c,',')){free(k);return false;} }
            }
        } else { if(!jskip_value(c)){free(k);return false;} }
        free(k);
        jskip_ws(c); if (jconsume(c,'}')) return true; if(!jconsume(c,',')) return false;
    }
}

static bool parse_emu_runtime(jcur_t *c, sim_profile_emu_runtime_t *e) {
    if (!jexpect_obj_start(c)) return false;
    jskip_ws(c); if (jconsume(c,'}')) return true;
    for (;;) {
        char *k=jparse_string(c); if(!k) return false; if(!jconsume(c,':')){free(k);return false;}
        if (strcmp(k,"esp_emu_path")==0) { if(!jparse_scalar_to_str(c,e->esp_emu_path,sizeof(e->esp_emu_path))){free(k);return false;} }
        else if (strcmp(k,"chip")==0) { if(!jparse_scalar_to_str(c,e->chip,sizeof(e->chip))){free(k);return false;} }
        else if (strcmp(k,"psram")==0) { if(!jparse_scalar_to_str(c,e->psram,sizeof(e->psram))){free(k);return false;} }
        else if (strcmp(k,"timeout")==0) {
            jskip_ws(c); if (jskip_literal(c,"null")) e->timeout_sec = -1; else { if(!jparse_int(c,&e->timeout_sec)){free(k);return false;} }
        } else if (strcmp(k,"instance")==0) { if(!jparse_int(c,&e->instance)){free(k);return false;} }
        else if (strcmp(k,"extra_args")==0) {
            jskip_ws(c);
            if (c->i < c->len && c->s[c->i] == '[') {
                /* join array into space-separated string */
                if(!jconsume(c,'[')){free(k);return false;} jskip_ws(c);
                e->extra_args[0]='\0';
                if(!jconsume(c,']')){
                    for(;;){ char *s=jparse_string(c); if(!s){free(k);return false;}
                        if(e->extra_args[0]) strncat(e->extra_args, " ", sizeof(e->extra_args)-strlen(e->extra_args)-1);
                        strncat(e->extra_args, s, sizeof(e->extra_args)-strlen(e->extra_args)-1);
                        free(s); jskip_ws(c); if(jconsume(c,']')) break; if(!jconsume(c,',')){free(k);return false;} }
                }
            } else { if(!jparse_scalar_to_str(c,e->extra_args,sizeof(e->extra_args))){free(k);return false;} }
        } else if (strcmp(k,"console_mode")==0) { if(!jparse_scalar_to_str(c,e->console_mode,sizeof(e->console_mode))){free(k);return false;} }
        else { if(!jskip_value(c)){free(k);return false;} }
        free(k);
        jskip_ws(c); if (jconsume(c,'}')) return true; if(!jconsume(c,',')) return false;
    }
}

static bool parse_ui_state(jcur_t *c, sim_profile_ui_state_t *u) {
    if (!jexpect_obj_start(c)) return false;
    jskip_ws(c); if (jconsume(c,'}')) return true;
    for (;;) {
        char *k=jparse_string(c); if(!k) return false; if(!jconsume(c,':')){free(k);return false;}
        if (strcmp(k,"scale")==0) { if(!jparse_double(c,&u->scale)){free(k);return false;} }
        else if (strcmp(k,"window")==0) {
            if(!jexpect_obj_start(c)){free(k);return false;} jskip_ws(c); u->has_window=true; if(!jconsume(c,'}')){
                for(;;){ char *wk=jparse_string(c); if(!wk){free(k);return false;} if(!jconsume(c,':')){free(wk);free(k);return false;}
                    if(strcmp(wk,"x")==0) jparse_int(c,&u->window_x);
                    else if(strcmp(wk,"y")==0) jparse_int(c,&u->window_y);
                    else if(strcmp(wk,"width")==0) jparse_int(c,&u->window_w);
                    else if(strcmp(wk,"height")==0) jparse_int(c,&u->window_h);
                    else if(strcmp(wk,"maximized")==0) jparse_bool(c,&u->window_maximized);
                    else { if(!jskip_value(c)){free(wk);free(k);return false;} }
                    free(wk); jskip_ws(c); if(jconsume(c,'}')) break; if(!jconsume(c,',')){free(k);return false;} }
            }
        } else { if(!jskip_value(c)){free(k);return false;} }
        free(k);
        jskip_ws(c); if (jconsume(c,'}')) return true; if(!jconsume(c,',')) return false;
    }
}

static char *read_file_all(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    if (out_len) *out_len = rd;
    return buf;
}

int sim_profile_load_json(sim_profile_t *p, const char *profile_path, sim_profile_diags_t *diags) {
    if (!p || !profile_path) return -1;
    sim_profile_free(p);
    sim_profile_init(p);
    size_t len = 0;
    char *data = read_file_all(profile_path, &len);
    if (!data) {
        if (diags) sim_profile_diags_add(diags, SIM_PROFILE_DIAG_ERROR, "file", "cannot read '%s': %s", profile_path, strerror(errno));
        return -1;
    }
    jcur_t c = { data, len, 0 };
    if (!jexpect_obj_start(&c)) {
        if (diags) sim_profile_diags_add(diags, SIM_PROFILE_DIAG_ERROR, "root", "expected JSON object");
        free(data); return -1;
    }
    jskip_ws(&c);
    bool ok = true;
    if (!jconsume(&c, '}')) {
        for (;;) {
            char *k = jparse_string(&c);
            if (!k || !jconsume(&c, ':')) { free(k); ok = false; break; }
            if (strcmp(k, "schema_version") == 0) { ok = jparse_scalar_to_str(&c, p->schema_version, sizeof(p->schema_version)); }
            else if (strcmp(k, "$schema") == 0) { ok = jparse_scalar_to_str(&c, p->schema_ref, sizeof(p->schema_ref)); }
            else if (strcmp(k, "metadata") == 0) { ok = parse_metadata(&c, &p->metadata); }
            else if (strcmp(k, "board_hardware") == 0) { ok = parse_board_hardware(&c, &p->board_hardware); }
            else if (strcmp(k, "firmware") == 0) { ok = parse_firmware(&c, &p->firmware); }
            else if (strcmp(k, "storage") == 0) { ok = parse_storage(&c, &p->storage); }
            else if (strcmp(k, "audio") == 0) { ok = parse_audio(&c, &p->audio); }
            else if (strcmp(k, "network_radio") == 0) { ok = parse_network(&c, &p->network_radio); }
            else if (strcmp(k, "emu_runtime") == 0) { ok = parse_emu_runtime(&c, &p->emu_runtime); }
            else if (strcmp(k, "ui_state") == 0) { ok = parse_ui_state(&c, &p->ui_state); }
            else if (strcmp(k, "extensions") == 0) {
                free(p->extensions_json);
                p->extensions_json = jcapture_raw(&c);
                ok = (p->extensions_json != NULL);
            } else {
                ok = jskip_value(&c);
            }
            free(k);
            if (!ok) break;
            jskip_ws(&c);
            if (jconsume(&c, '}')) break;
            if (!jconsume(&c, ',')) { ok = false; break; }
        }
    }
    free(data);
    if (!ok) {
        if (diags) sim_profile_diags_add(diags, SIM_PROFILE_DIAG_ERROR, "json", "failed to parse profile JSON");
        return -1;
    }
    if (!p->extensions_json) p->extensions_json = sdup0("{}");
    return 0;
}

/* ---------- JSON writer ---------- */

static void json_escape(FILE *f, const char *s) {
    if (!s) s = "";
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
            case '"': fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\b': fputs("\\b", f); break;
            case '\f': fputs("\\f", f); break;
            case '\n': fputs("\\n", f); break;
            case '\r': fputs("\\r", f); break;
            case '\t': fputs("\\t", f); break;
            default:
                if (*p < 0x20) fprintf(f, "\\u%04x", *p);
                else fputc(*p, f);
        }
    }
    fputc('"', f);
}

/* True for a plain JSON integer (no leading zeros): written bare, not quoted. */
static bool is_json_int(const char *s) {
    const char *q = s;
    if (*q == '-') q++;
    if (!isdigit((unsigned char)*q)) return false;
    if (*q == '0' && q[1]) return false;
    for (; *q; q++) if (!isdigit((unsigned char)*q)) return false;
    return true;
}

static void json_kv_str(FILE *f, const char *k, const char *v, bool comma) {
    fprintf(f, "    \"%s\": ", k); json_escape(f, v ? v : ""); if (comma) fputc(',', f); fputc('\n', f);
}
/* json_kv_* helpers reserved for future extension; writer uses inline formatting now. */

int sim_profile_save_json(const sim_profile_t *p, const char *profile_path, sim_profile_diags_t *diags) {
    if (!p || !profile_path) return -1;
    FILE *f = fopen(profile_path, "w");
    if (!f) {
        if (diags) sim_profile_diags_add(diags, SIM_PROFILE_DIAG_ERROR, "file", "cannot write '%s': %s", profile_path, strerror(errno));
        return -1;
    }
    const char *ext = (p->extensions_json && p->extensions_json[0]) ? p->extensions_json : "{}";
    fprintf(f, "{\n");
    if (p->schema_ref[0]) { fprintf(f, "  \"$schema\": "); json_escape(f, p->schema_ref); fprintf(f, ",\n"); }
    fprintf(f, "  \"schema_version\": "); json_escape(f, p->schema_version[0]?p->schema_version:SIM_PROFILE_SCHEMA_VERSION); fprintf(f, ",\n");
    /* metadata */
    fprintf(f, "  \"metadata\": {\n");
    json_kv_str(f, "id", p->metadata.id, true);
    json_kv_str(f, "name", p->metadata.name, true);
    json_kv_str(f, "description", p->metadata.description, true);
    fprintf(f, "    \"tags\": [");
    for (size_t i=0;i<p->metadata.tag_count;i++){ if(i) fputs(", ",f); json_escape(f,p->metadata.tags[i]); }
    fprintf(f, "],\n");
    json_kv_str(f, "board_preset", p->metadata.board_preset, true);
    fprintf(f, "    \"timestamps\": {");
    bool ts_any = false;
    if (p->metadata.created_at[0]) { fprintf(f, "\n      \"created_at\": "); json_escape(f,p->metadata.created_at); ts_any=true; }
    if (p->metadata.updated_at[0]) { if(ts_any) fputs(",",f); fprintf(f, "\n      \"updated_at\": "); json_escape(f,p->metadata.updated_at); ts_any=true; }
    if (ts_any) fprintf(f, "\n    }\n"); else fprintf(f, "}\n");
    fprintf(f, "  },\n");
    /* board_hardware */
    fprintf(f, "  \"board_hardware\": {\n");
    fprintf(f, "    \"board_id\": "); json_escape(f,p->board_hardware.board_id); fprintf(f, ",\n");
    fprintf(f, "    \"mcu\": { \"chip\": "); json_escape(f,p->board_hardware.mcu.chip);
    fprintf(f, ", \"psram\": "); if(p->board_hardware.mcu.psram[0]) json_escape(f,p->board_hardware.mcu.psram); else fputs("null",f);
    fprintf(f, ", \"flash\": "); if(p->board_hardware.mcu.flash[0]) json_escape(f,p->board_hardware.mcu.flash); else fputs("null",f);
    fprintf(f, " },\n");
    fprintf(f, "    \"display\": { \"width\": %d, \"height\": %d, \"orientation\": %d, \"overrides\": { \"hw_rotation\": %d }, \"panel\": { \"name\": ",
            p->board_hardware.display.width, p->board_hardware.display.height,
            p->board_hardware.display.orientation,
            p->board_hardware.display.hw_rotation >= 0 ? p->board_hardware.display.hw_rotation : 0);
    json_escape(f,p->board_hardware.display.panel_name); fprintf(f, " } },\n");
    fprintf(f, "    \"touch\": { \"controller\": "); if(p->board_hardware.touch.controller[0]) json_escape(f,p->board_hardware.touch.controller); else fputs("null",f);
    fprintf(f, ", \"bus\": "); if(p->board_hardware.touch.bus[0]) json_escape(f,p->board_hardware.touch.bus); else fputs("null",f);
    fprintf(f, ", \"pins\": {");
    for (size_t i = 0; i < p->board_hardware.touch.pin_count; i++) {
        const sim_profile_pin_t *pn = &p->board_hardware.touch.pins[i];
        fprintf(f, "%s", i ? ", " : " ");
        json_escape(f, pn->key); fputs(": ", f);
        if (!pn->value[0]) fputs("null", f);
        else if (is_json_int(pn->value)) fputs(pn->value, f);
        else json_escape(f, pn->value);
    }
    fprintf(f, "%s}, \"calibration\": {} },\n", p->board_hardware.touch.pin_count ? " " : "");
    fprintf(f, "    \"buses_pins\": {},\n");
    fprintf(f, "    \"cosim\": { \"mode\": "); json_escape(f,p->board_hardware.cosim.mode[0]?p->board_hardware.cosim.mode:"board_json");
    fprintf(f, ", \"board_json\": "); if(p->board_hardware.cosim.board_json[0]) json_escape(f,p->board_hardware.cosim.board_json); else fputs("null",f);
    fprintf(f, ", \"tcp\": "); if(p->board_hardware.cosim.tcp_port>0){ fprintf(f,"{ \"host\": "); json_escape(f,p->board_hardware.cosim.tcp_host[0]?p->board_hardware.cosim.tcp_host:"127.0.0.1"); fprintf(f,", \"port\": %d }", p->board_hardware.cosim.tcp_port);} else fputs("null",f);
    fprintf(f, ", \"unix\": "); if(p->board_hardware.cosim.unix_path[0]){ fprintf(f,"{ \"path\": "); json_escape(f,p->board_hardware.cosim.unix_path); fputs(" }",f);} else fputs("null",f);
    fprintf(f, " }\n");
    fprintf(f, "  },\n");
    /* firmware */
    fprintf(f, "  \"firmware\": {\n");
    fprintf(f, "    \"selection\": "); json_escape(f,p->firmware.selection); fprintf(f, ",\n");
    fprintf(f, "    \"format\": "); json_escape(f,p->firmware.format[0]?p->firmware.format:"merged"); fprintf(f, ",\n");
    fprintf(f, "    \"artifacts\": { \"merged\": "); if(p->firmware.artifacts.merged[0]) json_escape(f,p->firmware.artifacts.merged); else fputs("null",f);
    fprintf(f, ", \"boot\": "); if(p->firmware.artifacts.boot[0]) json_escape(f,p->firmware.artifacts.boot); else fputs("null",f);
    fprintf(f, ", \"part\": "); if(p->firmware.artifacts.part[0]) json_escape(f,p->firmware.artifacts.part); else fputs("null",f);
    fprintf(f, ", \"app\": "); if(p->firmware.artifacts.app[0]) json_escape(f,p->firmware.artifacts.app); else fputs("null",f);
    fprintf(f, ", \"elf\": "); if(p->firmware.artifacts.elf[0]) json_escape(f,p->firmware.artifacts.elf); else fputs("null",f);
    fprintf(f, ", \"use_elf\": %s, \"littlefs\": ", p->firmware.artifacts.use_elf?"true":"false");
    if(p->firmware.artifacts.littlefs[0]) json_escape(f,p->firmware.artifacts.littlefs); else fputs("null",f);
    fprintf(f, ", \"uf2\": "); if(p->firmware.artifacts.uf2[0]) json_escape(f,p->firmware.artifacts.uf2); else fputs("null",f);
    fprintf(f, " },\n");
    fprintf(f, "    \"flash\": { \"mode\": "); if(p->firmware.flash.mode[0]) json_escape(f,p->firmware.flash.mode); else fputs("null",f);
    fprintf(f, ", \"freq\": "); if(p->firmware.flash.freq[0]) json_escape(f,p->firmware.flash.freq); else fputs("null",f);
    fprintf(f, ", \"size\": "); if(p->firmware.flash.size[0]) json_escape(f,p->firmware.flash.size); else fputs("null",f);
    fprintf(f, ", \"offsets\": {");
    bool off_any=false;
    if(p->firmware.flash.bootloader_offset[0]){ fprintf(f," \"bootloader\": "); json_escape(f,p->firmware.flash.bootloader_offset); off_any=true; }
    if(p->firmware.flash.partitions_offset[0]){ if(off_any) fputc(',',f); fprintf(f," \"partitions\": "); json_escape(f,p->firmware.flash.partitions_offset); off_any=true; }
    if(p->firmware.flash.app_offset[0]){ if(off_any) fputc(',',f); fprintf(f," \"app\": "); json_escape(f,p->firmware.flash.app_offset); off_any=true; }
    if(off_any) fputs(" } },\n",f); else fputs("} },\n",f);
    fprintf(f, "    \"build\": { \"env\": "); if(p->firmware.build_env[0]) json_escape(f,p->firmware.build_env); else fputs("null",f);
    fprintf(f, " },\n    \"resolved\": {}\n  },\n");
    /* storage */
    fprintf(f, "  \"storage\": {\n");
    fprintf(f, "    \"littlefs\": { \"enabled\": %s, \"image_path\": ", p->storage.littlefs_enabled?"true":"false");
    if(p->storage.littlefs_image_path[0]) json_escape(f,p->storage.littlefs_image_path); else fputs("null",f);
    fprintf(f, ", \"mount_path\": "); if(p->storage.littlefs_mount_path[0]) json_escape(f,p->storage.littlefs_mount_path); else fputs("null",f);
    fprintf(f, " },\n");
    fprintf(f, "    \"sd_enabled\": %s,\n    \"sd_image_path\": ", p->storage.card_enabled?"true":"false");
    json_escape(f,p->storage.card_image_path); fputs(",\n",f);
    fprintf(f, "    \"sd_dir\": "); if(p->storage.sd_dir[0]) json_escape(f,p->storage.sd_dir); else fputs("null",f); fputs(",\n",f);
    fprintf(f, "    \"sd_card\": { \"enabled\": %s, \"image_path\": ", p->storage.sd_enabled?"true":"false");
    if(p->storage.sd_image_path[0]) json_escape(f,p->storage.sd_image_path); else fputs("null",f);
    fprintf(f, ", \"mount_path\": "); if(p->storage.sd_mount_path[0]) json_escape(f,p->storage.sd_mount_path); else fputs("null",f);
    fprintf(f, ", \"note\": \"esp-emu 1.1.0 mounts the card image with --sd\" }\n  },\n");
    /* audio */
    fprintf(f, "  \"audio\": { \"enabled\": %s, \"out\": ", p->audio.enabled?"true":"false");
    if(p->audio.out[0]) json_escape(f,p->audio.out); else fputs("null",f);
    fprintf(f, ", \"in\": "); if(p->audio.in[0]) json_escape(f,p->audio.in); else fputs("null",f);
    fprintf(f, ", \"realtime\": %s },\n", p->audio.realtime?"true":"false");
    /* network_radio */
    /* No Wi-Fi passphrase here or in launcher.conf: only SIM_WIFI_PASSWORD sets it. */
    fprintf(f, "  \"network_radio\": { \"net_user\": %s, \"net_mode\": ", p->network_radio.net_user?"true":"false");
    json_escape(f,p->network_radio.net_mode);
    fprintf(f, ", \"hostfwd\": [");
    for(size_t i=0;i<p->network_radio.hostfwd_count;i++){ if(i) fputs(", ",f); json_escape(f,p->network_radio.hostfwd[i]); }
    fprintf(f, "], \"wifi\": { \"enabled\": %s, \"ssid\": ", p->network_radio.wifi_enabled?"true":"false");
    json_escape(f,p->network_radio.wifi_ssid);
    fprintf(f, ", \"auth\": ");
    json_escape(f,p->network_radio.wifi_auth[0]?p->network_radio.wifi_auth:"unknown");
    fprintf(f, " }, \"ble\": { \"enabled\": %s, \"hci\": ", p->network_radio.ble_enabled?"true":"false");
    json_escape(f,p->network_radio.ble_hci);
    fprintf(f, " } },\n");
    /* emu_runtime */
    fprintf(f, "  \"emu_runtime\": { \"esp_emu_path\": "); json_escape(f,p->emu_runtime.esp_emu_path);
    fprintf(f, ", \"chip\": "); if(p->emu_runtime.chip[0]) json_escape(f,p->emu_runtime.chip); else fputs("null",f);
    fprintf(f, ", \"psram\": "); if(p->emu_runtime.psram[0]) json_escape(f,p->emu_runtime.psram); else fputs("null",f);
    fprintf(f, ", \"timeout\": "); if(p->emu_runtime.timeout_sec>=0) fprintf(f,"%d",p->emu_runtime.timeout_sec); else fputs("null",f);
    fprintf(f, ", \"instance\": %d", p->emu_runtime.instance);
    fprintf(f, ", \"extra_args\": "); json_escape(f,p->emu_runtime.extra_args);
    fprintf(f, ", \"console_mode\": "); json_escape(f,p->emu_runtime.console_mode[0]?p->emu_runtime.console_mode:"tcp");
    fprintf(f, " },\n");
    /* ui_state */
    fprintf(f, "  \"ui_state\": { \"scale\": %.6g, \"window\": { \"maximized\": %s }",
            p->ui_state.scale, p->ui_state.window_maximized?"true":"false");
    if(p->ui_state.has_window){
        /* overwrite simpler: but easier to include fields */
    }
    fprintf(f, ", \"panels\": {}, \"last_run\": {} },\n");
    /* extensions */
    fprintf(f, "  \"extensions\": %s\n", ext);
    fprintf(f, "}\n");
    fclose(f);
    return 0;
}

/* ---------- validation ---------- */

int sim_profile_validate(const sim_profile_t *p, const char *base_dir_for_paths, sim_profile_diags_t *diags) {
    if (!p) return -1;
    if (strcmp(p->schema_version, SIM_PROFILE_SCHEMA_VERSION) != 0) {
        if (diags) sim_profile_diags_add(diags, SIM_PROFILE_DIAG_ERROR, "schema_version", "expected '%s'", SIM_PROFILE_SCHEMA_VERSION);
    }
    if (!p->metadata.id[0]) sim_profile_diags_add(diags, SIM_PROFILE_DIAG_ERROR, "metadata.id", "required");
    if (!p->metadata.name[0]) sim_profile_diags_add(diags, SIM_PROFILE_DIAG_ERROR, "metadata.name", "required");
    if (!p->metadata.board_preset[0]) sim_profile_diags_add(diags, SIM_PROFILE_DIAG_ERROR, "metadata.board_preset", "required");
    if (!p->board_hardware.board_id[0]) sim_profile_diags_add(diags, SIM_PROFILE_DIAG_ERROR, "board_hardware.board_id", "required");
    if (p->board_hardware.display.orientation < 0 || p->board_hardware.display.orientation > 3)
        sim_profile_diags_add(diags, SIM_PROFILE_DIAG_ERROR, "board_hardware.display.orientation", "must be 0-3");
    if (p->board_hardware.display.hw_rotation < -1 || p->board_hardware.display.hw_rotation > 3)
        sim_profile_diags_add(diags, SIM_PROFILE_DIAG_ERROR, "board_hardware.display.overrides.hw_rotation", "must be 0-3");
    if (p->ui_state.scale < 1.0 || p->ui_state.scale > 4.0)
        sim_profile_diags_add(diags, SIM_PROFILE_DIAG_WARNING, "ui_state.scale", "recommended range 1-4");
    if (!p->firmware.selection[0]) sim_profile_diags_add(diags, SIM_PROFILE_DIAG_ERROR, "firmware.selection", "required");
    if (!p->emu_runtime.esp_emu_path[0]) sim_profile_diags_add(diags, SIM_PROFILE_DIAG_ERROR, "emu_runtime.esp_emu_path", "required");
    /* enum-ish warnings */
    if (p->emu_runtime.console_mode[0] && strcmp(p->emu_runtime.console_mode,"none")&&strcmp(p->emu_runtime.console_mode,"stdio")&&strcmp(p->emu_runtime.console_mode,"tcp")&&strcmp(p->emu_runtime.console_mode,"pty"))
        sim_profile_diags_add(diags, SIM_PROFILE_DIAG_WARNING, "emu_runtime.console_mode", "unknown value '%s'", p->emu_runtime.console_mode);

    /* path existence checks */
    const char *paths[][2] = {
        {"emu_runtime.esp_emu_path", p->emu_runtime.esp_emu_path},
        {"board_hardware.cosim.board_json", p->board_hardware.cosim.board_json},
        {"firmware.artifacts.merged", p->firmware.artifacts.merged},
        {"firmware.artifacts.elf", p->firmware.artifacts.elf},
        {"firmware.artifacts.littlefs", p->firmware.artifacts.littlefs},
        {NULL,NULL}
    };
    for (int i=0; paths[i][0]; i++) {
        const char *fld = paths[i][0]; const char *val = paths[i][1];
        if (!val || !val[0]) continue;
        char resolved[PATH_MAX];
        if (base_dir_for_paths && !is_abs_path(val)) sim_profile_resolve_path(base_dir_for_paths, val, resolved, sizeof(resolved));
        else sset(resolved, sizeof(resolved), val);
        if (!file_exists(resolved)) {
            sim_profile_diag_level_t lvl = SIM_PROFILE_DIAG_WARNING;
            if (strcmp(fld, "emu_runtime.esp_emu_path")==0) lvl = SIM_PROFILE_DIAG_WARNING;
            sim_profile_diags_add(diags, lvl, fld, "path does not exist: '%s'", resolved);
        }
    }
    return sim_profile_diags_has_errors(diags) ? -1 : 0;
}

/* ---------- paths ---------- */

int sim_profile_resolve_path(const char *base_dir, const char *in, char *out, size_t outsz) {
    if (!out || outsz == 0) return -1;
    out[0] = '\0';
    if (!in || !in[0]) return 0;
    if (is_abs_path(in) || !base_dir || !base_dir[0]) {
        if (snprintf(out, outsz, "%s", in) >= (int)outsz) return -1;
        return 0;
    }
    /* base + "/" + in, simple normalization of duplicate slashes not required */
    size_t bl = strlen(base_dir);
    const char *sep = (bl > 0 && base_dir[bl-1] == '/') ? "" : "/";
    if (snprintf(out, outsz, "%s%s%s", base_dir, sep, in) >= (int)outsz) return -1;
    return 0;
}

int sim_profile_relativize_path(const char *base_dir, const char *in, char *out, size_t outsz) {
    if (!out || outsz == 0) return -1;
    if (!in) { out[0]='\0'; return 0; }
    if (!base_dir || !base_dir[0] || !is_abs_path(in)) {
        if (snprintf(out, outsz, "%s", in) >= (int)outsz) return -1;
        return 0;
    }
    size_t bl = strlen(base_dir);
    while (bl > 1 && base_dir[bl-1] == '/') bl--;
    if (strncmp(in, base_dir, bl) == 0 && in[bl] == '/') {
        if (snprintf(out, outsz, "%s", in + bl + 1) >= (int)outsz) return -1;
        return 0;
    }
    if (snprintf(out, outsz, "%s", in) >= (int)outsz) return -1;
    return 0;
}

/* ---------- mapping ---------- */

void sim_profile_from_launcher_config(sim_profile_t *p, const launcher_config_t *cfg) {
    if (!p || !cfg) return;
    sim_profile_defaults(p);
    const char *bid = launcher_board_id(cfg->board);
    sset(p->metadata.board_preset, sizeof(p->metadata.board_preset), bid);
    sset(p->board_hardware.board_id, sizeof(p->board_hardware.board_id), bid);
    sset(p->board_hardware.display.panel_name, sizeof(p->board_hardware.display.panel_name),
         cfg->board == LAUNCHER_BOARD_ES3C28P ? "ILI9341" : "");
    p->board_hardware.display.hw_rotation = cfg->hw_rotation;
    p->board_hardware.display.orientation = 0;
    if (cfg->lcd_w > 0) p->board_hardware.display.width = cfg->lcd_w;
    if (cfg->lcd_h > 0) p->board_hardware.display.height = cfg->lcd_h;
    sset(p->board_hardware.cosim.board_json, sizeof(p->board_hardware.cosim.board_json), cfg->cosim_board_path);
    if (cfg->cosim_board_path[0]) sset(p->board_hardware.cosim.mode, sizeof(p->board_hardware.cosim.mode), "board_json");

    sset(p->firmware.selection, sizeof(p->firmware.selection), launcher_firmware_name(cfg->firmware));
    sset(p->firmware.artifacts.merged, sizeof(p->firmware.artifacts.merged), cfg->merged_image_path);
    sset(p->firmware.artifacts.elf, sizeof(p->firmware.artifacts.elf), cfg->elf_path);
    p->firmware.artifacts.use_elf = cfg->use_elf;

    p->audio.enabled = cfg->audio;
    p->storage.card_enabled = cfg->sd_enabled;
    sset(p->storage.card_image_path, sizeof(p->storage.card_image_path), cfg->sd_image_path);
    sset(p->storage.sd_dir, sizeof(p->storage.sd_dir), cfg->sd_dir);
    p->network_radio.net_user = cfg->net_user;
    p->emu_runtime.instance = cfg->instance;
    sset(p->emu_runtime.extra_args, sizeof(p->emu_runtime.extra_args), cfg->extra_args);
    p->ui_state.scale = (double)cfg->scale;
    if (p->ui_state.scale < 1.0) p->ui_state.scale = 1.0;
    if (p->ui_state.scale > 4.0) p->ui_state.scale = 4.0;
}

void sim_profile_to_launcher_config(const sim_profile_t *p, launcher_config_t *cfg) {
    if (!p || !cfg) return;
    launcher_config_defaults(cfg);
    launcher_board_t b;
    if (launcher_board_from_id(p->board_hardware.board_id[0]?p->board_hardware.board_id:p->metadata.board_preset, &b) == 0)
        cfg->board = b;
    launcher_firmware_t fw;
    if (launcher_firmware_from_name(p->firmware.selection, &fw) == 0)
        cfg->firmware = fw;
    /* apply known fields before resolve for custom handling */
    sset(cfg->merged_image_path, sizeof(cfg->merged_image_path), p->firmware.artifacts.merged);
    sset(cfg->elf_path, sizeof(cfg->elf_path), p->firmware.artifacts.elf);
    cfg->use_elf = p->firmware.artifacts.use_elf;
    int sc = (int)(p->ui_state.scale + 0.5);
    if (sc < 1) sc = 1;
    if (sc > 4) sc = 4;
    cfg->scale = sc;
    int rot = p->board_hardware.display.hw_rotation >= 0 ? p->board_hardware.display.hw_rotation : p->board_hardware.display.orientation;
    if (rot < 0) rot = 0;
    if (rot > 3) rot = 3;
    cfg->hw_rotation = rot;
    cfg->instance = p->emu_runtime.instance;
    sset(cfg->extra_args, sizeof(cfg->extra_args), p->emu_runtime.extra_args);
    cfg->net_user = p->network_radio.net_user;
    cfg->audio = p->audio.enabled;
    cfg->sd_enabled = p->storage.card_enabled;
    sset(cfg->sd_image_path, sizeof(cfg->sd_image_path), p->storage.card_image_path); /* "" stays "" (no default leak) */
    sset(cfg->sd_dir, sizeof(cfg->sd_dir), p->storage.sd_dir);
    sset(cfg->cosim_board_path, sizeof(cfg->cosim_board_path), p->board_hardware.cosim.board_json);
    cfg->lcd_w = 0; cfg->lcd_h = 0; /* launcher uses overrides; profile width/height are base display */
    if (cfg->firmware == LAUNCHER_FW_CUSTOM) {
        sset(cfg->custom_firmware_path, sizeof(cfg->custom_firmware_path), p->firmware.artifacts.merged);
    }
    /* resolve defaults/consistency for non-custom selections where appropriate */
    if (cfg->firmware != LAUNCHER_FW_CUSTOM) {
        /* keep explicit profile paths if provided; resolve fills blanks */
        if (!cfg->merged_image_path[0] || !cfg->elf_path[0]) launcher_config_resolve_paths(cfg);
    } else {
        if (cfg->custom_firmware_path[0] && !cfg->merged_image_path[0])
            sset(cfg->merged_image_path, sizeof(cfg->merged_image_path), cfg->custom_firmware_path);
    }
}

/* ---------- import legacy launcher.conf ---------- */

int sim_profile_import_launcher_conf(sim_profile_t *p, const char *conf_path,
                                     const char *repo_root, sim_profile_diags_t *diags) {
    (void)repo_root;
    if (!p || !conf_path) return -1;
    launcher_config_t cfg;
    launcher_config_defaults(&cfg);
    if (launcher_config_load(&cfg, conf_path) != 0) {
        if (diags) sim_profile_diags_add(diags, SIM_PROFILE_DIAG_ERROR, "file", "cannot load launcher.conf '%s'", conf_path);
        return -1;
    }
    sim_profile_from_launcher_config(p, &cfg);
    sset(p->metadata.id, sizeof(p->metadata.id), "imported-launcher-conf");
    sset(p->metadata.name, sizeof(p->metadata.name), "Imported launcher.conf");
    sset(p->metadata.description, sizeof(p->metadata.description), "Migrated from legacy simulator/launcher/launcher.conf");
    if (diags) sim_profile_diags_add(diags, SIM_PROFILE_DIAG_INFO, "import", "imported legacy launcher.conf");
    return 0;
}
