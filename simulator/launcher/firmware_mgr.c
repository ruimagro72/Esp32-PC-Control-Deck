#include "firmware_mgr.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void fw_set_str(char *dst, size_t dstsz, const char *src) {
    if (!dst || dstsz == 0) return;
    if (!src) src = "";
    snprintf(dst, dstsz, "%s", src);
}

static int fw_appendf(char *buf, size_t buflen, size_t *off, const char *fmt, ...) {
    if (!buf || !off || *off >= buflen) return -1;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *off, buflen - *off, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if ((size_t)n >= buflen - *off) { *off = buflen - 1; return -1; }
    *off += (size_t)n;
    return 0;
}

static void msg_append(char *buf, size_t buflen, const char *prefix, const char *msg) {
    if (!buf || buflen == 0 || !msg || !msg[0]) return;
    size_t off = strlen(buf);
    if (off > 0 && off + 1 < buflen) {
        buf[off++] = '\n';
        buf[off] = '\0';
    }
    if (prefix && prefix[0]) fw_appendf(buf, buflen, &off, "%s%s", prefix, msg);
    else fw_appendf(buf, buflen, &off, "%s", msg);
}

static bool ends_with_ci(const char *s, const char *suf) {
    if (!s || !suf) return false;
    size_t ls = strlen(s), lf = strlen(suf);
    if (lf > ls) return false;
    const char *p = s + ls - lf;
    for (size_t i = 0; i < lf; i++) {
        unsigned char a = (unsigned char)p[i], b = (unsigned char)suf[i];
        if (tolower(a) != tolower(b)) return false;
    }
    return true;
}

static bool contains_ci(const char *s, const char *sub) {
    if (!s || !sub || !*sub) return false;
    size_t ls = strlen(s), lt = strlen(sub);
    if (lt > ls) return false;
    for (size_t i = 0; i + lt <= ls; i++) {
        bool ok = true;
        for (size_t j = 0; j < lt; j++) {
            if (tolower((unsigned char)s[i + j]) != tolower((unsigned char)sub[j])) { ok = false; break; }
        }
        if (ok) return true;
    }
    return false;
}

static const char *path_basename(const char *path) {
    if (!path) return "";
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

bool fw_path_exists(const char *path) {
    if (!path || !path[0]) return false;
    return access(path, F_OK) == 0;
}

bool fw_path_is_file(const char *path) {
    if (!path || !path[0]) return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

bool fw_path_is_dir(const char *path) {
    if (!path || !path[0]) return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

uint64_t fw_file_size(const char *path) {
    if (!path || !path[0]) return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (!S_ISREG(st.st_mode)) return 0;
    return (uint64_t)st.st_size;
}

const char *fw_known_local_merged_path(void) { return "simulator/build/merged-emu.bin"; }
const char *fw_known_local_elf_path(void)    { return "firmware/.pio/build/es3c28p_emu/firmware.elf"; }
const char *fw_known_asset_es3c28p_dir(void) { return "simulator/emu/firmware/es3c28p"; }
const char *fw_known_kryonos_v3_root(void)   { return "simulator/emu/firmware/kryonos-v3"; }

int fw_path_asset_es3c28p_merged(char *out, size_t outsz) {
    if (!out || outsz == 0) return -1;
    snprintf(out, outsz, "%s/merged.bin", fw_known_asset_es3c28p_dir());
    return 0;
}

int fw_path_asset_es3c28p_elf(char *out, size_t outsz) {
    if (!out || outsz == 0) return -1;
    snprintf(out, outsz, "%s/firmware.elf", fw_known_asset_es3c28p_dir());
    return 0;
}

int fw_path_kryonos_v3_board_dir(const char *board, char *out, size_t outsz) {
    if (!board || !board[0] || !out || outsz == 0) return -1;
    snprintf(out, outsz, "%s/%s", fw_known_kryonos_v3_root(), board);
    return 0;
}

int fw_path_kryonos_v3_merged(const char *board, char *out, size_t outsz) {
    if (!board || !board[0] || !out || outsz == 0) return -1;
    snprintf(out, outsz, "%s/%s/merged.bin", fw_known_kryonos_v3_root(), board);
    return 0;
}

void fw_artifacts_init(fw_artifacts_t *a) {
    if (!a) return;
    memset(a, 0, sizeof(*a));
    a->kind = FW_KIND_UNKNOWN;
    a->off_bootloader = FW_OFF_BOOTLOADER;
    a->off_partitions = FW_OFF_PARTITIONS;
    a->off_app = FW_OFF_APP;
    a->off_fs = FW_OFF_FS;
}

static void detect_file_role(const char *path, fw_artifacts_t *a) {
    const char *base = path_basename(path);
    uint64_t sz = fw_file_size(path);
    if (ends_with_ci(base, ".elf")) {
        if (!a->elf[0]) { fw_set_str(a->elf, sizeof(a->elf), path); a->size_elf = sz; a->has_elf = (sz > 0); }
        return;
    }
    if (!ends_with_ci(base, ".bin")) return;

    if (strcmp(base, "bootloader.bin") == 0) {
        fw_set_str(a->bootloader, sizeof(a->bootloader), path); a->size_bootloader = sz; return;
    }
    if (strcmp(base, "partitions.bin") == 0) {
        fw_set_str(a->partitions, sizeof(a->partitions), path); a->size_partitions = sz; return;
    }
    if (strcmp(base, "firmware.bin") == 0 || strcmp(base, "app.bin") == 0) {
        fw_set_str(a->app, sizeof(a->app), path); a->size_app = sz; return;
    }
    if (strcmp(base, "littlefs.bin") == 0 || strcmp(base, "spiffs.bin") == 0 || contains_ci(base, "littlefs")) {
        if (!a->fs[0]) { fw_set_str(a->fs, sizeof(a->fs), path); a->size_fs = sz; a->has_fs = (sz > 0); }
        return;
    }
    if (contains_ci(base, "merged") || contains_ci(base, "merge")) {
        if (!a->merged[0]) { fw_set_str(a->merged, sizeof(a->merged), path); a->size_merged = sz; a->has_merged = (sz > 0); }
        return;
    }
    /* Generic .bin: treat as merged candidate if none set and no split role */
    if (!a->merged[0]) {
        fw_set_str(a->merged, sizeof(a->merged), path); a->size_merged = sz; a->has_merged = (sz > 0);
    }
}

static void finalize_detection(fw_artifacts_t *a) {
    a->has_merged = (a->merged[0] && fw_file_size(a->merged) > 0);
    a->size_merged = a->has_merged ? fw_file_size(a->merged) : 0;
    a->size_bootloader = a->bootloader[0] ? fw_file_size(a->bootloader) : 0;
    a->size_partitions = a->partitions[0] ? fw_file_size(a->partitions) : 0;
    a->size_app = a->app[0] ? fw_file_size(a->app) : 0;
    a->size_elf = a->elf[0] ? fw_file_size(a->elf) : 0;
    a->size_fs = a->fs[0] ? fw_file_size(a->fs) : 0;
    a->has_elf = (a->elf[0] && a->size_elf > 0);
    a->has_fs = (a->fs[0] && a->size_fs > 0);
    a->has_split = (a->bootloader[0] && a->partitions[0] && a->app[0] &&
                    a->size_bootloader > 0 && a->size_partitions > 0 && a->size_app > 0);

    if (a->has_merged && !a->has_split) a->kind = FW_KIND_MERGED;
    else if (a->has_split) a->kind = FW_KIND_SPLIT;
    else if (a->has_elf && !a->has_merged) a->kind = FW_KIND_ELF;
    else if (a->has_fs && !a->has_merged && !a->has_split) a->kind = FW_KIND_FS;
    else a->kind = FW_KIND_UNKNOWN;
}

int fw_detect(const char *path, fw_artifacts_t *out) {
    if (!path || !path[0] || !out) return -1;
    fw_artifacts_init(out);
    fw_set_str(out->source_path, sizeof(out->source_path), path);

    if (fw_path_is_dir(path)) {
        DIR *d = opendir(path);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
                char full[FW_MAX_PATH];
                snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
                if (fw_path_is_file(full)) detect_file_role(full, out);
            }
            closedir(d);
        }
        /* If split components found, prefer them; keep merged if also present (both valid) */
    } else if (fw_path_is_file(path)) {
        detect_file_role(path, out);
        /* If a single app/boot/part file given, also probe sibling split files in same dir */
        const char *base = path_basename(path);
        if (ends_with_ci(base, ".bin")) {
            char dir[FW_MAX_PATH];
            fw_set_str(dir, sizeof(dir), path);
            char *slash = strrchr(dir, '/');
            if (slash) {
                *slash = '\0';
                const char *names[] = {"bootloader.bin", "partitions.bin", "firmware.bin", "app.bin", "littlefs.bin", "spiffs.bin", NULL};
                size_t dlen = strlen(dir);
                for (int i = 0; names[i]; i++) {
                    size_t nlen = strlen(names[i]);
                    if (dlen + 1 + nlen >= FW_MAX_PATH) continue;
                    char sib[FW_MAX_PATH];
                    memcpy(sib, dir, dlen);
                    sib[dlen] = '/';
                    memcpy(sib + dlen + 1, names[i], nlen + 1);
                    if (fw_path_is_file(sib)) detect_file_role(sib, out);
                }
                /* probe merged siblings */
                DIR *d = opendir(dir);
                if (d) {
                    struct dirent *e;
                    while ((e = readdir(d)) != NULL) {
                        size_t elen = strlen(e->d_name);
                        if (dlen + 1 + elen >= FW_MAX_PATH) continue;
                        if (ends_with_ci(e->d_name, ".bin") && contains_ci(e->d_name, "merged")) {
                            char full[FW_MAX_PATH];
                            memcpy(full, dir, dlen);
                            full[dlen] = '/';
                            memcpy(full + dlen + 1, e->d_name, elen + 1);
                            detect_file_role(full, out);
                        }
                        if (ends_with_ci(e->d_name, ".elf") && !out->elf[0]) {
                            char full[FW_MAX_PATH];
                            memcpy(full, dir, dlen);
                            full[dlen] = '/';
                            memcpy(full + dlen + 1, e->d_name, elen + 1);
                            detect_file_role(full, out);
                        }
                    }
                    closedir(d);
                }
            }
        }
    } else {
        /* path missing: try to infer by name */
        const char *base = path_basename(path);
        if (ends_with_ci(base, ".elf")) { fw_set_str(out->elf, sizeof(out->elf), path); out->kind = FW_KIND_ELF; }
        else if (contains_ci(base, "merged")) { fw_set_str(out->merged, sizeof(out->merged), path); }
    }

    finalize_detection(out);
    return 0;
}

static const char *k_preset_names[FW_PRESET_COUNT] = {
    "local_es3c28p_emu",
    "asset_es3c28p",
    "kryonos_v3",
    "custom",
};

const char *fw_preset_name(fw_preset_t preset) {
    if (preset < 0 || preset >= FW_PRESET_COUNT) return "unknown";
    return k_preset_names[preset];
}

int fw_preset_from_name(const char *name, fw_preset_t *out) {
    if (!name || !out) return -1;
    for (int i = 0; i < FW_PRESET_COUNT; i++) {
        if (strcmp(name, k_preset_names[i]) == 0) { *out = (fw_preset_t)i; return 0; }
    }
    return -1;
}

int fw_resolve_preset(fw_preset_t preset, const char *board, fw_preset_info_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->preset = preset;
    fw_artifacts_init(&out->artifacts);
    if (board) fw_set_str(out->board, sizeof(out->board), board);

    if (preset == FW_PRESET_LOCAL_ES3C28P_EMU) {
        fw_set_str(out->artifacts.merged, sizeof(out->artifacts.merged), fw_known_local_merged_path());
        fw_set_str(out->artifacts.elf, sizeof(out->artifacts.elf), fw_known_local_elf_path());
        fw_set_str(out->artifacts.source_path, sizeof(out->artifacts.source_path), fw_known_local_merged_path());
        finalize_detection(&out->artifacts);
        out->exists = out->artifacts.has_merged; /* elf optional but expected */
        fw_set_str(out->note, sizeof(out->note), out->artifacts.has_elf ? "local es3c28p emu merged+elf" : "local es3c28p emu merged (elf missing)");
        return 0;
    }

    if (preset == FW_PRESET_ASSET_ES3C28P) {
        char p[FW_MAX_PATH];
        fw_path_asset_es3c28p_merged(p, sizeof(p));
        fw_set_str(out->artifacts.merged, sizeof(out->artifacts.merged), p);
        fw_set_str(out->artifacts.source_path, sizeof(out->artifacts.source_path), p);
        fw_path_asset_es3c28p_elf(p, sizeof(p));
        fw_set_str(out->artifacts.elf, sizeof(out->artifacts.elf), p);
        /* also detect split in asset dir */
        fw_artifacts_t det; fw_detect(fw_known_asset_es3c28p_dir(), &det);
        if (det.has_split) {
            fw_set_str(out->artifacts.bootloader, sizeof(out->artifacts.bootloader), det.bootloader);
            fw_set_str(out->artifacts.partitions, sizeof(out->artifacts.partitions), det.partitions);
            fw_set_str(out->artifacts.app, sizeof(out->artifacts.app), det.app);
        }
        if (det.has_fs) fw_set_str(out->artifacts.fs, sizeof(out->artifacts.fs), det.fs);
        finalize_detection(&out->artifacts);
        out->exists = out->artifacts.has_merged || out->artifacts.has_split;
        fw_set_str(out->note, sizeof(out->note), "asset es3c28p");
        return 0;
    }

    if (preset == FW_PRESET_KRYONOS_V3) {
        if (!board || !board[0]) return -1;
        char dir[FW_MAX_PATH], merged[FW_MAX_PATH];
        fw_path_kryonos_v3_board_dir(board, dir, sizeof(dir));
        fw_path_kryonos_v3_merged(board, merged, sizeof(merged));
        fw_artifacts_t det; fw_detect(dir, &det);
        out->artifacts = det;
        fw_set_str(out->artifacts.source_path, sizeof(out->artifacts.source_path), dir);
        /* ensure canonical merged path preference */
        if (fw_path_is_file(merged)) { fw_set_str(out->artifacts.merged, sizeof(out->artifacts.merged), merged); }
        out->artifacts.elf[0] = '\0'; out->artifacts.has_elf = false; out->artifacts.size_elf = 0;
        finalize_detection(&out->artifacts);
        out->exists = out->artifacts.has_merged || out->artifacts.has_split;
        snprintf(out->note, sizeof(out->note), "kryonos-v3/%s (no elf)", board);
        return 0;
    }

    if (preset == FW_PRESET_CUSTOM) {
        fw_set_str(out->note, sizeof(out->note), "custom path (resolve/detect required)");
        out->exists = false;
        return 0;
    }

    return -1;
}

int fw_list_presets(const char *board_filter, fw_preset_info_t *out, int max) {
    if (!out || max <= 0) return -1;
    int n = 0;

    /* static presets */
    if (n < max) { fw_resolve_preset(FW_PRESET_LOCAL_ES3C28P_EMU, NULL, &out[n++]); }
    if (n < max) { fw_resolve_preset(FW_PRESET_ASSET_ES3C28P, NULL, &out[n++]); }

    if (board_filter && board_filter[0]) {
        if (n < max) {
            if (fw_resolve_preset(FW_PRESET_KRYONOS_V3, board_filter, &out[n]) == 0) n++;
        }
    } else {
        DIR *d = opendir(fw_known_kryonos_v3_root());
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL && n < max) {
                if (e->d_name[0] == '.') continue;
                char sub[FW_MAX_PATH];
                snprintf(sub, sizeof(sub), "%s/%s", fw_known_kryonos_v3_root(), e->d_name);
                if (!fw_path_is_dir(sub)) continue;
                if (fw_resolve_preset(FW_PRESET_KRYONOS_V3, e->d_name, &out[n]) == 0) n++;
            }
            closedir(d);
        }
    }

    return n;
}

void fw_validation_init(fw_validation_t *v) {
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->ok = true;
}

static void v_err(fw_validation_t *v, const char *msg) {
    v->ok = false;
    v->error_count++;
    msg_append(v->errors, sizeof(v->errors), "ERROR: ", msg);
}
static void v_warn(fw_validation_t *v, const char *msg) {
    v->warning_count++;
    msg_append(v->warnings, sizeof(v->warnings), "WARN: ", msg);
}

static void check_file(fw_validation_t *v, const char *label, const char *path, uint64_t sz, bool required) {
    char buf[FW_MAX_PATH + 96];
    if (!path || !path[0]) {
        if (required) { snprintf(buf, sizeof(buf), "%s: path missing", label); v_err(v, buf); }
        return;
    }
    if (!fw_path_is_file(path)) {
        snprintf(buf, sizeof(buf), "%s: file not found: %s", label, path);
        if (required) v_err(v, buf); else v_warn(v, buf);
        return;
    }
    if (sz == 0) sz = fw_file_size(path);
    if (sz == 0) {
        snprintf(buf, sizeof(buf), "%s: file is empty: %s", label, path);
        if (required) v_err(v, buf); else v_warn(v, buf);
    }
}

int fw_validate(const fw_artifacts_t *a, bool is_release_hint, fw_validation_t *out) {
    if (!a || !out) return -1;
    fw_validation_init(out);

    bool has_merged = a->has_merged || (a->merged[0] && fw_file_size(a->merged) > 0);
    bool has_split = (a->bootloader[0] && a->partitions[0] && a->app[0]);

    if (!has_merged && !has_split) {
        v_err(out, "no usable firmware artifacts (need merged.bin or complete split: bootloader+partitions+app)");
    }

    if (has_merged) {
        check_file(out, "merged", a->merged, a->size_merged, true);
    }
    if (has_split) {
        check_file(out, "bootloader", a->bootloader, a->size_bootloader, true);
        check_file(out, "partitions", a->partitions, a->size_partitions, true);
        check_file(out, "app", a->app, a->size_app, true);
    } else if (a->bootloader[0] || a->partitions[0] || a->app[0]) {
        /* partial split */
        v_err(out, "incomplete split image set (require bootloader.bin, partitions.bin, firmware/app.bin)");
        check_file(out, "bootloader", a->bootloader, a->size_bootloader, a->bootloader[0] != '\0');
        check_file(out, "partitions", a->partitions, a->size_partitions, a->partitions[0] != '\0');
        check_file(out, "app", a->app, a->size_app, a->app[0] != '\0');
    }

    if (a->fs[0]) {
        check_file(out, "fs/littlefs", a->fs, a->size_fs, false);
    }
    if (a->elf[0]) {
        check_file(out, "elf", a->elf, a->size_elf, false);
        if (!fw_path_is_file(a->elf) || fw_file_size(a->elf) == 0) {
            /* already warned if path set; for release hint keep as warning context */
            if (is_release_hint) v_warn(out, "ELF missing/invalid for release image (symbols/BLE hooks unavailable)");
        }
    } else {
        if (is_release_hint) v_warn(out, "ELF missing for release image (expected; reduced debug/symbol hooks)");
        else v_warn(out, "ELF missing (symbols/debug hooks may be unavailable)");
    }

    return 0;
}

void fw_merge_opts_defaults(fw_merge_opts_t *o) {
    if (!o) return;
    memset(o, 0, sizeof(*o));
    fw_set_str(o->chip, sizeof(o->chip), "esp32s3");
    fw_set_str(o->flash_mode, sizeof(o->flash_mode), "qio");
    fw_set_str(o->flash_freq, sizeof(o->flash_freq), "80m");
    fw_set_str(o->flash_size, sizeof(o->flash_size), "16MB");
}

int fw_find_esptool(char *out, size_t outsz) {
    if (!out || outsz == 0) return -1;
    const char *home = getenv("HOME");
    if (home && home[0]) {
        char p[FW_MAX_PATH];
        snprintf(p, sizeof(p), "%s/.platformio/penv/bin/esptool", home);
        if (access(p, X_OK) == 0) { fw_set_str(out, outsz, p); return 0; }
    }
    /* fallbacks by name (resolved by PATH at exec time) */
    if (access("/usr/bin/esptool", X_OK) == 0) { fw_set_str(out, outsz, "esptool"); return 0; }
    if (access("/usr/local/bin/esptool", X_OK) == 0) { fw_set_str(out, outsz, "esptool"); return 0; }
    if (access("/usr/bin/esptool.py", X_OK) == 0) { fw_set_str(out, outsz, "esptool.py"); return 0; }
    /* best-effort names */
    fw_set_str(out, outsz, "esptool");
    return 0;
}

static int cmd_push(fw_merge_cmd_t *c, const char *arg) {
    if (!c || !arg) return -1;
    if (c->argc >= FW_MAX_ARGV - 1) return -1;
    size_t off = 0;
    for (int i = 0; i < c->argc; i++) {
        off += strlen(c->argv[i]) + 1;
    }
    size_t len = strlen(arg) + 1;
    if (off + len > sizeof(c->argv_storage)) return -1;
    memcpy(c->argv_storage + off, arg, len);
    c->argv[c->argc] = c->argv_storage + off;
    c->argc++;
    c->argv[c->argc] = NULL;
    return 0;
}

static bool shell_word_safe(const char *s) {
    if (!s || !s[0]) return false;
    for (const char *p = s; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (isalnum(ch)) continue;
        if (strchr("_-./:=+,@%", (int)ch)) continue;
        return false;
    }
    return true;
}

/* Append a shell-escaped representation of one argv element (display/log only;
 * execution always uses argv directly). */
static void cmdline_append_arg(char *buf, size_t buflen, size_t *off, const char *arg) {
    if (!buf || !off || *off >= buflen) return;
    const char *a = arg ? arg : "";
    if (*off > 0) fw_appendf(buf, buflen, off, " ");
    if (shell_word_safe(a)) {
        fw_appendf(buf, buflen, off, "%s", a);
        return;
    }
    fw_appendf(buf, buflen, off, "'");
    for (const char *p = a; *p && *off < buflen; p++) {
        if (*p == '\'') {
            if (fw_appendf(buf, buflen, off, "'\\''") != 0) break;
        } else {
            if (fw_appendf(buf, buflen, off, "%c", *p) != 0) break;
        }
    }
    if (*off < buflen) fw_appendf(buf, buflen, off, "'");
}

static void build_cmdline(fw_merge_cmd_t *c) {
    if (!c) return;
    size_t off = 0;
    c->cmdline[0] = '\0';
    for (int i = 0; i < c->argc; i++) cmdline_append_arg(c->cmdline, sizeof(c->cmdline), &off, c->argv[i]);
}

int fw_build_merge_cmd(const fw_artifacts_t *a, const fw_merge_opts_t *opts, fw_merge_cmd_t *out) {
    if (!a || !out) return -1;
    memset(out, 0, sizeof(*out));

    bool has_split = (a->bootloader[0] && a->partitions[0] && a->app[0]);
    if (!has_split) return -1;
    if (fw_file_size(a->bootloader) == 0 || fw_file_size(a->partitions) == 0 || fw_file_size(a->app) == 0) return -1;

    fw_merge_opts_t o;
    fw_merge_opts_defaults(&o);
    if (opts) {
        if (opts->chip[0]) fw_set_str(o.chip, sizeof(o.chip), opts->chip);
        if (opts->flash_mode[0]) fw_set_str(o.flash_mode, sizeof(o.flash_mode), opts->flash_mode);
        if (opts->flash_freq[0]) fw_set_str(o.flash_freq, sizeof(o.flash_freq), opts->flash_freq);
        if (opts->flash_size[0]) fw_set_str(o.flash_size, sizeof(o.flash_size), opts->flash_size);
        if (opts->output[0]) fw_set_str(o.output, sizeof(o.output), opts->output);
    }
    if (!o.output[0]) return -1;

    if (fw_find_esptool(out->esptool_path, sizeof(out->esptool_path)) != 0) return -1;

    fw_esptool_info_t ei;
    fw_esptool_probe(&ei);
    const char *subcmd = ei.merge_subcmd[0] ? ei.merge_subcmd : "merge-bin";
    bool legacy = (strcmp(subcmd, "merge_bin") == 0);

    if (cmd_push(out, out->esptool_path) != 0) return -1;
    if (cmd_push(out, "--chip") != 0) return -1;
    if (cmd_push(out, o.chip) != 0) return -1;
    if (cmd_push(out, subcmd) != 0) return -1;
    if (cmd_push(out, "-o") != 0) return -1;
    if (cmd_push(out, o.output) != 0) return -1;
    if (cmd_push(out, legacy ? "--flash_mode" : "--flash-mode") != 0) return -1;
    if (cmd_push(out, o.flash_mode) != 0) return -1;
    if (cmd_push(out, legacy ? "--flash_freq" : "--flash-freq") != 0) return -1;
    if (cmd_push(out, o.flash_freq) != 0) return -1;
    if (cmd_push(out, legacy ? "--flash_size" : "--flash-size") != 0) return -1;
    if (cmd_push(out, o.flash_size) != 0) return -1;

    char off[24];
    snprintf(off, sizeof(off), "0x%x", (unsigned)a->off_bootloader);
    if (cmd_push(out, off) != 0) return -1;
    if (cmd_push(out, a->bootloader) != 0) return -1;
    snprintf(off, sizeof(off), "0x%x", (unsigned)a->off_partitions);
    if (cmd_push(out, off) != 0) return -1;
    if (cmd_push(out, a->partitions) != 0) return -1;
    snprintf(off, sizeof(off), "0x%x", (unsigned)a->off_app);
    if (cmd_push(out, off) != 0) return -1;
    if (cmd_push(out, a->app) != 0) return -1;

    if (a->fs[0] && fw_file_size(a->fs) > 0) {
        snprintf(off, sizeof(off), "0x%x", (unsigned)a->off_fs);
        if (cmd_push(out, off) != 0) return -1;
        if (cmd_push(out, a->fs) != 0) return -1;
    }

    build_cmdline(out);
    return 0;
}

int fw_build_merge_cmd_for_preset(fw_preset_t preset, const char *board,
                                  const fw_merge_opts_t *opts, fw_merge_cmd_t *out) {
    if (!out) return -1;
    fw_preset_info_t info;
    if (fw_resolve_preset(preset, board, &info) != 0) return -1;
    /* for presets that are merged-only, merge cmd from split may not apply; try detect in source dir */
    fw_artifacts_t a = info.artifacts;
    if (!a.has_split && a.source_path[0] && fw_path_is_dir(a.source_path)) {
        fw_detect(a.source_path, &a);
    }
    return fw_build_merge_cmd(&a, opts, out);
}

/* ===========================================================================
 * Merge plan: presets, validation, command generation, summary, execution
 * ========================================================================= */

/* --- small numeric/string helpers ---------------------------------------- */

static bool parse_size_bytes(const char *s, uint64_t *out) {
    if (!s || !s[0] || !out) return false;
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 0);
    if (errno != 0 || end == s) return false;
    while (*end == ' ' || *end == '\t') end++;
    uint64_t mult = 1;
    if (*end) {
        switch (tolower((unsigned char)*end)) {
            case 'k': mult = 1024ull; break;
            case 'm': mult = 1024ull * 1024ull; break;
            case 'g': mult = 1024ull * 1024ull * 1024ull; break;
            default: return false;
        }
        const char *r = end + 1;
        if (*r == 'i' || *r == 'I') r++;
        if (*r == 'b' || *r == 'B') r++;
        while (*r == ' ' || *r == '\t') r++;
        if (*r != '\0') return false;
    }
    *out = (uint64_t)v * mult;
    return true;
}

static bool read_head_bytes(const char *path, unsigned char *buf, size_t n) {
    if (!path || !buf || n == 0) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    return got == n;
}

static char *read_text_head(const char *path, size_t max_bytes) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = (char *)malloc(max_bytes + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, max_bytes, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

/* --- process capture (argv only, never a shell) -------------------------- */

static int spawn_capture(char *const argv[], char *out, size_t outsz, int *exit_code) {
    if (!argv || !argv[0] || !out || outsz == 0) return -1;
    out[0] = '\0';
    if (exit_code) *exit_code = -1;

    int pfd[2];
    if (pipe(pfd) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pfd[0]);
        if (dup2(pfd[1], STDOUT_FILENO) < 0 || dup2(pfd[1], STDERR_FILENO) < 0) _exit(127);
        close(pfd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(pfd[1]);

    size_t len = 0;
    char chunk[1024];
    ssize_t n;
    while ((n = read(pfd[0], chunk, sizeof(chunk))) > 0) {
        size_t need = (size_t)n;
        if (need >= outsz - 1) {
            memcpy(out, chunk + (need - (outsz - 1)), outsz - 1);
            len = outsz - 1;
        } else {
            if (len + need > outsz - 1) {
                size_t drop = len + need - (outsz - 1);
                memmove(out, out + drop, len - drop);
                len -= drop;
            }
            memcpy(out + len, chunk, need);
            len += need;
        }
        out[len] = '\0';
    }
    close(pfd[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        if (exit_code) *exit_code = -1;
        return -1;
    }
    if (exit_code) {
        if (WIFEXITED(status)) *exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) *exit_code = 128 + WTERMSIG(status);
        else *exit_code = -1;
    }
    return 0;
}

/* --- capability / role naming -------------------------------------------- */

const char *fw_capability_str(fw_capability_t cap) {
    switch (cap) {
        case FW_CAP_SUPPORTED_NOW:      return "supported-now";
        case FW_CAP_CONDITIONAL:        return "conditional(version/tool)";
        case FW_CAP_NEEDS_RUNTIME_HOST: return "needs-runtime-host";
        case FW_CAP_BLOCKED_BY_EMU:     return "blocked-by-emu";
    }
    return "unknown";
}

static const char *k_seg_names[FW_SEG_COUNT] = {"bootloader", "partitions", "app", "littlefs"};

const char *fw_segment_role_name(fw_segment_role_t role) {
    if (role < 0 || role >= FW_SEG_COUNT) return "unknown";
    return k_seg_names[role];
}

int fw_segment_role_from_name(const char *name, fw_segment_role_t *out) {
    if (!name || !out) return -1;
    if (strcmp(name, "bootloader") == 0 || strcmp(name, "boot") == 0) { *out = FW_SEG_BOOTLOADER; return 0; }
    if (strcmp(name, "partitions") == 0 || strcmp(name, "partition") == 0 || strcmp(name, "part") == 0) { *out = FW_SEG_PARTITIONS; return 0; }
    if (strcmp(name, "app") == 0 || strcmp(name, "firmware") == 0 || strcmp(name, "fw") == 0) { *out = FW_SEG_APP; return 0; }
    if (strcmp(name, "fs") == 0 || strcmp(name, "littlefs") == 0 || strcmp(name, "spiffs") == 0 || strcmp(name, "data") == 0) { *out = FW_SEG_FS; return 0; }
    return -1;
}

fw_capability_t fw_sd_capability(void) { return FW_CAP_SUPPORTED_NOW; }
const char *fw_sd_capability_note(void) {
    return "SD card: esp-emu 1.1.0 --sd (ESP32-S3 SDMMC host) with a FAT card image; "
           "the merged flash image carries no SD data.";
}

/* --- plan setup ---------------------------------------------------------- */

static void seg_init(fw_merge_segment_t *s, fw_segment_role_t role, uint32_t off,
                     bool required, uint32_t limit) {
    memset(s, 0, sizeof(*s));
    s->role = role;
    s->offset = off;
    s->required = required;
    s->limit = limit;
}

void fw_merge_plan_init(fw_merge_plan_t *plan) {
    if (!plan) return;
    memset(plan, 0, sizeof(*plan));
    fw_set_str(plan->chip, sizeof(plan->chip), "esp32s3");
    fw_set_str(plan->flash_mode, sizeof(plan->flash_mode), "qio");
    fw_set_str(plan->flash_freq, sizeof(plan->flash_freq), "80m");
    fw_set_str(plan->flash_size, sizeof(plan->flash_size), "16MB");
    plan->overwrite = true;

    seg_init(&plan->segments[FW_SEG_BOOTLOADER], FW_SEG_BOOTLOADER, FW_OFF_BOOTLOADER, true, 0);
    seg_init(&plan->segments[FW_SEG_PARTITIONS], FW_SEG_PARTITIONS, FW_OFF_PARTITIONS, true, 0);
    seg_init(&plan->segments[FW_SEG_APP], FW_SEG_APP, FW_OFF_APP, true, 0);
    seg_init(&plan->segments[FW_SEG_FS], FW_SEG_FS, FW_OFF_FS, false, FW_LIMIT_FS_16MB);
}

bool fw_board_preset_known(const char *board_id) {
    if (!board_id || !board_id[0]) return false;
    return strcmp(board_id, "es3c28p") == 0 ||
           strcmp(board_id, "esp32-s3-devkitc-1-n16r8") == 0 ||
           strcmp(board_id, "lilygo-t-deck") == 0 ||
           strcmp(board_id, "lilygo-t-hmi") == 0 ||
           strcmp(board_id, "m5stack-cardputer") == 0;
}

static bool fs_candidate(const char *dir, char *out, size_t outsz) {
    if (!dir || !dir[0] || !out || outsz == 0) return false;
    const char *names[] = {"littlefs.bin", "spiffs.bin", "littlefs.img", "spiffs.img", NULL};
    for (int i = 0; names[i]; i++) {
        char p[FW_MAX_PATH];
        snprintf(p, sizeof(p), "%s/%s", dir, names[i]);
        if (fw_path_is_file(p) && fw_file_size(p) > 0) {
            fw_set_str(out, outsz, p);
            return true;
        }
    }
    return false;
}

int fw_merge_plan_defaults_for_board(const char *board_id, fw_merge_plan_t *out) {
    if (!out) return -1;
    const char *id = (board_id && board_id[0]) ? board_id : "es3c28p";
    fw_merge_plan_init(out);
    fw_set_str(out->board, sizeof(out->board), id);

    char src[FW_MAX_PATH] = "";
    bool kryonos = false;
    bool has_elf = false;

    if (strcmp(id, "es3c28p") == 0) {
        const char *local = "firmware/.pio/build/es3c28p_emu";
        if (fw_path_is_dir(local)) {
            fw_set_str(src, sizeof(src), local);
            fw_set_str(out->notes, sizeof(out->notes),
                       "es3c28p local emu build (firmware/.pio/build/es3c28p_emu); "
                       "16MB qio/80m; FS optional @0xc90000 (region 0x360000).");
        } else {
            fw_set_str(src, sizeof(src), fw_known_asset_es3c28p_dir());
            fw_set_str(out->notes, sizeof(out->notes),
                       "es3c28p local build missing; using asset split dir "
                       "(simulator/emu/firmware/es3c28p). Build: (cd firmware && pio run -e es3c28p_emu).");
        }
        fw_set_str(out->output, sizeof(out->output), "simulator/build/merged-emu.bin");
    } else if (fw_board_preset_known(id)) {
        kryonos = true;
        snprintf(src, sizeof(src), "%s/%s", fw_known_kryonos_v3_root(), id);
        snprintf(out->output, sizeof(out->output), "simulator/build/merged-%s.bin", id);
        if (strcmp(id, "m5stack-cardputer") == 0) {
            fw_set_str(out->flash_size, sizeof(out->flash_size), "8MB");
            out->segments[FW_SEG_FS].offset = 0;
            out->segments[FW_SEG_FS].limit = 0;
            fw_set_str(out->notes, sizeof(out->notes),
                       "KryonOS v3 release split assets (8MB flash, no ELF); FS layout/offset "
                       "unknown for this build - set explicitly before including a data image.");
        } else {
            fw_set_str(out->notes, sizeof(out->notes),
                       "KryonOS v3 release split assets (16MB qio/80m, offsets 0x0/0x8000/0x10000); "
                       "no ELF in release; FS not shipped.");
        }
    } else {
        return -1;
    }

    fw_set_str(out->source_dir, sizeof(out->source_dir), src);
    snprintf(out->segments[FW_SEG_BOOTLOADER].path, FW_MAX_PATH, "%s/bootloader.bin", src);
    snprintf(out->segments[FW_SEG_PARTITIONS].path, FW_MAX_PATH, "%s/partitions.bin", src);
    snprintf(out->segments[FW_SEG_APP].path, FW_MAX_PATH, "%s/firmware.bin", src);

    char fs[FW_MAX_PATH];
    if (fs_candidate(src, fs, sizeof(fs))) {
        fw_set_str(out->segments[FW_SEG_FS].path, sizeof(out->segments[FW_SEG_FS].path), fs);
        out->include_fs = true;
    } else {
        out->include_fs = false;
    }

    /* ELF availability hint (release assets normally have none). */
    if (src[0] && fw_path_is_dir(src)) {
        DIR *d = opendir(src);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (ends_with_ci(e->d_name, ".elf")) { has_elf = true; break; }
            }
            closedir(d);
        }
    }
    if (!has_elf) {
        char note[FW_MAX_MSG];
        fw_set_str(note, sizeof(note), out->notes);
        size_t off = strlen(note);
        fw_appendf(note, sizeof(note), &off,
                   " No ELF alongside inputs: use --no-elf (reduced symbols/backtraces).");
        fw_set_str(out->notes, sizeof(out->notes), note);
    }

    (void)kryonos;
    return 0;
}

static fw_merge_segment_t *plan_seg_mut(fw_merge_plan_t *p, fw_segment_role_t role) {
    if (!p || role < 0 || role >= FW_SEG_COUNT) return NULL;
    return &p->segments[role];
}

int fw_merge_plan_set_segment(fw_merge_plan_t *plan, fw_segment_role_t role, const char *path) {
    fw_merge_segment_t *s = plan_seg_mut(plan, role);
    if (!s) return -1;
    fw_set_str(s->path, sizeof(s->path), path ? path : "");
    return 0;
}

int fw_merge_plan_set_offset(fw_merge_plan_t *plan, fw_segment_role_t role, uint32_t offset) {
    fw_merge_segment_t *s = plan_seg_mut(plan, role);
    if (!s) return -1;
    s->offset = offset;
    return 0;
}

int fw_merge_plan_set_fs(fw_merge_plan_t *plan, const char *path, bool enable) {
    if (!plan) return -1;
    plan->include_fs = enable && path && path[0];
    if (path && path[0]) fw_set_str(plan->segments[FW_SEG_FS].path,
                                    sizeof(plan->segments[FW_SEG_FS].path), path);
    else if (!enable) plan->segments[FW_SEG_FS].path[0] = '\0';
    return 0;
}

int fw_merge_plan_set_output(fw_merge_plan_t *plan, const char *path) {
    if (!plan || !path || !path[0]) return -1;
    fw_set_str(plan->output, sizeof(plan->output), path);
    return 0;
}

int fw_merge_plan_set_flash(fw_merge_plan_t *plan, const char *mode, const char *freq,
                            const char *size) {
    if (!plan) return -1;
    if (mode && mode[0]) fw_set_str(plan->flash_mode, sizeof(plan->flash_mode), mode);
    if (freq && freq[0]) fw_set_str(plan->flash_freq, sizeof(plan->flash_freq), freq);
    if (size && size[0]) fw_set_str(plan->flash_size, sizeof(plan->flash_size), size);
    return 0;
}

/* --- esptool probe ------------------------------------------------------- */

static bool executable_at(const char *path) {
    return path && path[0] && access(path, X_OK) == 0;
}

static bool find_in_path(const char *name, char *out, size_t outsz) {
    if (!name || !name[0] || !out || outsz == 0) return false;
    if (strchr(name, '/')) {
        if (executable_at(name)) { fw_set_str(out, outsz, name); return true; }
        return false;
    }
    const char *path = getenv("PATH");
    if (!path) return false;
    const char *p = path;
    while (*p) {
        const char *sep = strchr(p, ':');
        size_t n = sep ? (size_t)(sep - p) : strlen(p);
        if (n > 0 && n < FW_MAX_PATH) {
            char cand[FW_MAX_PATH];
            snprintf(cand, sizeof(cand), "%.*s/%s", (int)n, p, name);
            if (executable_at(cand)) { fw_set_str(out, outsz, cand); return true; }
        }
        if (!sep) break;
        p = sep + 1;
    }
    return false;
}

static void parse_version_text(const char *txt, char *out, size_t outsz, int *major) {
    if (major) *major = -1;
    if (out && outsz) out[0] = '\0';
    if (!txt) return;
    const char *p = txt;
    while (*p && !isdigit((unsigned char)*p)) p++;
    if (!*p) return;
    int a = 0, b = 0, c = 0;
    int n = sscanf(p, "%d.%d.%d", &a, &b, &c);
    if (n <= 0) return;
    if (major) *major = a;
    if (!out || outsz == 0) return;
    if (n >= 3) snprintf(out, outsz, "%d.%d.%d", a, b, c);
    else if (n == 2) snprintf(out, outsz, "%d.%d", a, b);
    else snprintf(out, outsz, "%d", a);
}

int fw_esptool_probe(fw_esptool_info_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    fw_set_str(out->merge_subcmd, sizeof(out->merge_subcmd), "merge-bin");
    out->capability = FW_CAP_CONDITIONAL;

    char found[FW_MAX_PATH] = "";
    const char *env_tool = getenv("ESPTOOL");
    if (env_tool && env_tool[0] && executable_at(env_tool)) {
        fw_set_str(found, sizeof(found), env_tool);
    }
    const char *home = getenv("HOME");
    if (!found[0] && home && home[0]) {
        char p[FW_MAX_PATH];
        snprintf(p, sizeof(p), "%s/.platformio/penv/bin/esptool", home);
        if (executable_at(p)) fw_set_str(found, sizeof(found), p);
        if (!found[0]) {
            snprintf(p, sizeof(p), "%s/.local/bin/esptool", home);
            if (executable_at(p)) fw_set_str(found, sizeof(found), p);
        }
        if (!found[0]) {
            snprintf(p, sizeof(p), "%s/.local/bin/esptool.py", home);
            if (executable_at(p)) fw_set_str(found, sizeof(found), p);
        }
    }
    const char *fixed[] = {"/usr/local/bin/esptool", "/usr/bin/esptool",
                           "/usr/local/bin/esptool.py", "/usr/bin/esptool.py", NULL};
    for (int i = 0; !found[0] && fixed[i]; i++) {
        if (executable_at(fixed[i])) fw_set_str(found, sizeof(found), fixed[i]);
    }
    if (!found[0] && find_in_path("esptool", found, sizeof(found))) { /* resolved */ }
    if (!found[0] && find_in_path("esptool.py", found, sizeof(found))) { /* resolved */ }

    if (!found[0]) {
        fw_set_str(out->path, sizeof(out->path), "esptool");
        fw_set_str(out->note, sizeof(out->note),
                   "esptool not found (PATH, ~/.platformio/penv/bin, ~/.local/bin); merge is "
                   "conditional(version/tool). Install esptool/esptool.py or PlatformIO.");
        return 0;
    }

    fw_set_str(out->path, sizeof(out->path), found);
    out->found = true;

    char *argv[3];
    argv[0] = found;
    argv[1] = (char *)"version";
    argv[2] = NULL;
    char buf[512];
    int rc = -1;
    if (spawn_capture(argv, buf, sizeof(buf), &rc) == 0 && rc == 0) {
        int major = -1;
        parse_version_text(buf, out->version, sizeof(out->version), &major);
        if (major >= 5) fw_set_str(out->merge_subcmd, sizeof(out->merge_subcmd), "merge-bin");
        else if (major >= 0) fw_set_str(out->merge_subcmd, sizeof(out->merge_subcmd), "merge_bin");
        out->capability = FW_CAP_SUPPORTED_NOW;
        snprintf(out->note, sizeof(out->note), "esptool %s at %s (%s)",
                 out->version[0] ? out->version : "unknown", out->path, out->merge_subcmd);
    } else {
        out->capability = FW_CAP_CONDITIONAL;
        snprintf(out->note, sizeof(out->note),
                 "esptool found at %s but 'version' probe failed; treating merge capability as "
                 "conditional(version/tool) with default merge-bin.", out->path);
    }
    return 0;
}

/* --- plan validation ----------------------------------------------------- */

static void validate_output_target(fw_validation_t *v, const fw_merge_plan_t *plan) {
    if (!plan->output[0]) {
        v_err(v, "output path missing");
        return;
    }
    if (!plan->overwrite && fw_path_exists(plan->output)) {
        char msg[FW_MAX_PATH + 64];
        snprintf(msg, sizeof(msg), "output already exists (overwrite disabled): %s", plan->output);
        v_err(v, msg);
    }
    char dir[FW_MAX_PATH];
    fw_set_str(dir, sizeof(dir), plan->output);
    char *slash = strrchr(dir, '/');
    if (slash) {
        if (slash == dir) slash[1] = '\0';
        else *slash = '\0';
    } else {
        fw_set_str(dir, sizeof(dir), ".");
    }
    char probe[FW_MAX_PATH];
    fw_set_str(probe, sizeof(probe), dir);
    struct stat st;
    while (stat(probe, &st) != 0) {
        char *s = strrchr(probe, '/');
        if (!s) { fw_set_str(probe, sizeof(probe), "."); break; }
        if (s == probe) { probe[1] = '\0'; break; }
        *s = '\0';
    }
    if (stat(probe, &st) != 0 || !S_ISDIR(st.st_mode)) {
        char msg[FW_MAX_PATH + 96];
        snprintf(msg, sizeof(msg), "output directory unavailable/not creatable: %s", dir);
        v_err(v, msg);
        return;
    }
    if (access(probe, W_OK | X_OK) != 0) {
        char msg[FW_MAX_PATH + 96];
        snprintf(msg, sizeof(msg), "output directory not writable: %s", probe);
        v_err(v, msg);
    }
}

static bool segment_magic_check(const fw_merge_segment_t *s, char *why, size_t whysz) {
    if (why && whysz) why[0] = '\0';
    if (s->role == FW_SEG_FS) return true; /* no fixed magic for FS images */
    unsigned char hdr[2] = {0, 0};
    size_t need = (s->role == FW_SEG_PARTITIONS) ? 2 : 1;
    if (!read_head_bytes(s->path, hdr, need)) {
        if (why && whysz) snprintf(why, whysz, "cannot read header");
        return false;
    }
    if (s->role == FW_SEG_PARTITIONS) {
        if (hdr[0] != FW_PART_MAGIC0 || hdr[1] != FW_PART_MAGIC1) {
            if (why && whysz) snprintf(why, whysz, "partition magic 0x%02X%02X != 0xAA50", hdr[0], hdr[1]);
            return false;
        }
        return true;
    }
    if (hdr[0] != FW_IMAGE_MAGIC) {
        if (why && whysz) snprintf(why, whysz, "image magic 0x%02X != 0xE9", hdr[0]);
        return false;
    }
    return true;
}

static bool dir_has_elf(const char *dir, char *out, size_t outsz) {
    if (!dir || !dir[0] || !fw_path_is_dir(dir)) return false;
    DIR *d = opendir(dir);
    if (!d) return false;
    bool found = false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!ends_with_ci(e->d_name, ".elf")) continue;
        if (out && outsz) {
            size_t dl = strlen(dir), nl = strlen(e->d_name);
            if (dl + 1 + nl < outsz) {
                memcpy(out, dir, dl);
                out[dl] = '/';
                memcpy(out + dl + 1, e->d_name, nl + 1);
            } else {
                out[0] = '\0';
            }
        }
        found = true;
        break;
    }
    closedir(d);
    return found;
}

int fw_merge_plan_validate(fw_merge_plan_t *plan, fw_validation_t *out) {
    if (!plan || !out) return -1;
    fw_validation_init(out);

    if (!plan->chip[0]) v_err(out, "chip missing (e.g. esp32s3)");
    uint64_t flash_bytes = 0;
    if (!parse_size_bytes(plan->flash_size, &flash_bytes) || flash_bytes == 0) {
        v_warn(out, "unrecognised flash size (expected e.g. 16MB/8MB); region bound check skipped");
        flash_bytes = 0;
    }
    validate_output_target(out, plan);

    uint32_t prev_off = 0, prev_end = 0;
    bool have_prev = false;
    bool any_fs_active = false;

    for (int i = 0; i < FW_SEG_COUNT; i++) {
        fw_merge_segment_t *s = &plan->segments[i];
        const char *role = fw_segment_role_name(s->role);
        bool is_fs = (s->role == FW_SEG_FS);
        bool active = !is_fs || plan->include_fs;
        s->present = false;
        s->size = 0;
        s->signature_ok = false;
        s->problem[0] = '\0';

        if (is_fs && active) any_fs_active = true;

        if (!s->path[0]) {
            if (is_fs && active) {
                v_err(out, "littlefs: include_fs enabled but no image path set");
            } else if (s->required && active) {
                char msg[FW_MAX_PATH + 64];
                snprintf(msg, sizeof(msg), "%s: path missing", role);
                v_err(out, msg);
            }
            continue;
        }
        if (is_fs && !active) {
            v_warn(out, "LittleFS/data path set but include_fs is disabled; segment will be ignored");
            continue;
        }
        if (!fw_path_is_file(s->path)) {
            char msg[FW_MAX_PATH + 64];
            snprintf(msg, sizeof(msg), "%s: file not found: %s", role, s->path);
            if (s->required) v_err(out, msg); else v_warn(out, msg);
            continue;
        }
        if (access(s->path, R_OK) != 0) {
            char msg[FW_MAX_PATH + 64];
            snprintf(msg, sizeof(msg), "%s: file not readable: %s", role, s->path);
            if (s->required) v_err(out, msg); else v_warn(out, msg);
        }
        uint64_t sz = fw_file_size(s->path);
        s->size = sz;
        s->present = (sz > 0);
        if (sz == 0) {
            char msg[FW_MAX_PATH + 64];
            snprintf(msg, sizeof(msg), "%s: file is empty: %s", role, s->path);
            if (s->required) v_err(out, msg); else v_warn(out, msg);
            continue;
        }
        if (s->limit && sz > s->limit) {
            char msg[FW_MAX_PATH + 128];
            snprintf(msg, sizeof(msg), "%s: size %llu exceeds region limit %u (%s)",
                     role, (unsigned long long)sz, (unsigned)s->limit, s->path);
            v_err(out, msg);
        }
        if (is_fs && s->offset == 0) {
            v_err(out, "littlefs: offset is 0/unknown; set an explicit FS offset for this layout");
        }

        char why[128];
        s->signature_ok = segment_magic_check(s, why, sizeof(why));
        if (!s->signature_ok) {
            snprintf(s->problem, sizeof(s->problem), "%s", why);
            char msg[FW_MAX_PATH + 160];
            snprintf(msg, sizeof(msg), "%s: signature check failed (%s): %s", role, why, s->path);
            v_warn(out, msg);
        }

        if (have_prev) {
            if (s->offset <= prev_off) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "%s: offset 0x%x is not after previous offset 0x%x (segments must be ordered)",
                         role, (unsigned)s->offset, (unsigned)prev_off);
                v_err(out, msg);
            } else if ((uint64_t)s->offset < (uint64_t)prev_end) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "%s: overlaps previous segment (offset 0x%x < previous end 0x%x)",
                         role, (unsigned)s->offset, (unsigned)prev_end);
                v_err(out, msg);
            }
        }
        if (flash_bytes && (uint64_t)s->offset + sz > flash_bytes) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "%s: end 0x%llx exceeds flash size %llu",
                     role, (unsigned long long)((uint64_t)s->offset + sz),
                     (unsigned long long)flash_bytes);
            v_err(out, msg);
        }
        prev_off = s->offset;
        prev_end = (uint32_t)((uint64_t)s->offset + sz);
        have_prev = true;
    }

    if (!any_fs_active) {
        v_warn(out, "no LittleFS/data image included; internal FS mount may fail (often non-blocking). "
                    "The SD card (--sd) is separate from this image.");
    }

    if (plan->source_dir[0]) {
        char elf[FW_MAX_PATH];
        if (!dir_has_elf(plan->source_dir, elf, sizeof(elf))) {
            v_warn(out, "no ELF alongside inputs (release/custom build); use --no-elf "
                        "(reduced symbols/backtraces)");
        }
    }

    fw_esptool_info_t ei;
    if (fw_esptool_probe(&ei) == 0) {
        if (!ei.found) {
            v_warn(out, "esptool not found: merge capability is conditional(version/tool); "
                        "install esptool or use PlatformIO penv");
        } else if (ei.capability != FW_CAP_SUPPORTED_NOW) {
            char msg[FW_MAX_MSG];
            snprintf(msg, sizeof(msg), "%s", ei.note);
            v_warn(out, msg);
        }
    }
    return 0;
}

/* --- command building / summary ------------------------------------------ */

int fw_merge_plan_build_cmd(const fw_merge_plan_t *plan, fw_merge_cmd_t *out) {
    if (!plan || !out) return -1;
    memset(out, 0, sizeof(*out));
    if (!plan->output[0] || !plan->chip[0]) return -1;

    fw_esptool_info_t ei;
    fw_esptool_probe(&ei);

    const char *tool = "esptool";
    const char *subcmd = "merge-bin";
    if (plan->esptool[0]) {
        tool = plan->esptool;
        subcmd = ei.merge_subcmd[0] ? ei.merge_subcmd : "merge-bin";
    } else if (ei.found) {
        tool = ei.path;
        subcmd = ei.merge_subcmd[0] ? ei.merge_subcmd : "merge-bin";
    }
    fw_set_str(out->esptool_path, sizeof(out->esptool_path), tool);

    /* esptool v5 renamed merge_bin options to merge-bin/hyphenated flags. */
    bool legacy = (strcmp(subcmd, "merge_bin") == 0);
    const char *opt_mode = legacy ? "--flash_mode" : "--flash-mode";
    const char *opt_freq = legacy ? "--flash_freq" : "--flash-freq";
    const char *opt_size = legacy ? "--flash_size" : "--flash-size";

    if (cmd_push(out, tool) != 0) return -1;
    if (cmd_push(out, "--chip") != 0) return -1;
    if (cmd_push(out, plan->chip) != 0) return -1;
    if (cmd_push(out, subcmd) != 0) return -1;
    if (cmd_push(out, "-o") != 0) return -1;
    if (cmd_push(out, plan->output) != 0) return -1;
    if (plan->flash_mode[0]) {
        if (cmd_push(out, opt_mode) != 0 || cmd_push(out, plan->flash_mode) != 0) return -1;
    }
    if (plan->flash_freq[0]) {
        if (cmd_push(out, opt_freq) != 0 || cmd_push(out, plan->flash_freq) != 0) return -1;
    }
    if (plan->flash_size[0]) {
        if (cmd_push(out, opt_size) != 0 || cmd_push(out, plan->flash_size) != 0) return -1;
    }

    char offbuf[24];
    for (int i = 0; i < FW_SEG_COUNT; i++) {
        const fw_merge_segment_t *s = &plan->segments[i];
        if (s->role == FW_SEG_FS && !plan->include_fs) continue;
        if (!s->path[0]) continue;
        snprintf(offbuf, sizeof(offbuf), "0x%x", (unsigned)s->offset);
        if (cmd_push(out, offbuf) != 0) return -1;
        if (cmd_push(out, s->path) != 0) return -1;
    }

    build_cmdline(out);
    return 0;
}

int fw_merge_plan_summary(const fw_merge_plan_t *plan, char *out, size_t outsz) {
    if (!plan || !out || outsz == 0) return -1;
    out[0] = '\0';
    size_t off = 0;

    fw_appendf(out, outsz, &off,
               "Merge plan: board=%s chip=%s flash=%s/%s/%s\n",
               plan->board[0] ? plan->board : "(custom)",
               plan->chip[0] ? plan->chip : "?",
               plan->flash_mode[0] ? plan->flash_mode : "?",
               plan->flash_freq[0] ? plan->flash_freq : "?",
               plan->flash_size[0] ? plan->flash_size : "?");
    fw_appendf(out, outsz, &off, "Output: %s\n", plan->output[0] ? plan->output : "(unset)");
    if (plan->source_dir[0]) fw_appendf(out, outsz, &off, "Source: %s\n", plan->source_dir);

    fw_esptool_info_t ei;
    fw_esptool_probe(&ei);
    fw_appendf(out, outsz, &off, "esptool: %s%s%s\n",
               ei.found ? ei.path : "not found",
               ei.found && ei.version[0] ? " v" : "",
               ei.found ? ei.version : "");
    fw_appendf(out, outsz, &off, "capability: %s\n", fw_capability_str(ei.capability));

    fw_appendf(out, outsz, &off, "Segments:\n");
    bool any = false;
    for (int i = 0; i < FW_SEG_COUNT; i++) {
        const fw_merge_segment_t *s = &plan->segments[i];
        if (!s->path[0]) continue;
        if (s->role == FW_SEG_FS && !plan->include_fs) {
            fw_appendf(out, outsz, &off, "  %-11s (ignored, include_fs=false) %s\n",
                       fw_segment_role_name(s->role), s->path);
            any = true;
            continue;
        }
        any = true;
        if (s->limit) {
            fw_appendf(out, outsz, &off, "  %-11s offset 0x%06x limit %u size %llu %s\n",
                       fw_segment_role_name(s->role), (unsigned)s->offset, (unsigned)s->limit,
                       (unsigned long long)s->size, s->path);
        } else {
            fw_appendf(out, outsz, &off, "  %-11s offset 0x%06x size %llu %s\n",
                       fw_segment_role_name(s->role), (unsigned)s->offset,
                       (unsigned long long)s->size, s->path);
        }
    }
    if (!any) fw_appendf(out, outsz, &off, "  (none)\n");

    if (plan->notes[0]) fw_appendf(out, outsz, &off, "Notes: %s\n", plan->notes);

    fw_validation_t v;
    fw_merge_plan_t tmp = *plan;
    fw_merge_plan_validate(&tmp, &v);
    fw_appendf(out, outsz, &off, "Validation: %s (%d errors, %d warnings)\n",
               v.ok ? "ok" : "FAILED", v.error_count, v.warning_count);
    if (v.errors[0]) fw_appendf(out, outsz, &off, "%s\n", v.errors);
    if (v.warnings[0]) fw_appendf(out, outsz, &off, "%s\n", v.warnings);
    return 0;
}

/* --- execution ----------------------------------------------------------- */

int fw_merge_cmd_run(const fw_merge_cmd_t *cmd, bool dry_run, fw_merge_run_t *out) {
    if (!cmd || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->dry_run = dry_run;
    out->exit_code = -1;
    fw_set_str(out->cmdline, sizeof(out->cmdline), cmd->cmdline);
    if (cmd->argc <= 0 || !cmd->argv[0]) return -1;

    if (dry_run) {
        out->executed = false;
        size_t off = 0;
        fw_appendf(out->output, sizeof(out->output), &off, "DRY-RUN: %s", cmd->cmdline);
        return 0;
    }

    int rc = -1;
    if (spawn_capture(cmd->argv, out->output, sizeof(out->output), &rc) != 0) {
        snprintf(out->output, sizeof(out->output), "failed to execute: %s", cmd->argv[0]);
        out->exit_code = -1;
        return -1;
    }
    out->executed = true;
    out->exit_code = rc;
    return rc == 0 ? 0 : 1;
}

int fw_merge_plan_run(const fw_merge_plan_t *plan, bool dry_run, fw_merge_run_t *out) {
    if (!plan || !out) return -1;
    fw_merge_cmd_t cmd;
    if (fw_merge_plan_build_cmd(plan, &cmd) != 0) return -1;
    return fw_merge_cmd_run(&cmd, dry_run, out);
}

/* --- artifact probe (merged/split/manifest/magic) ------------------------ */

int fw_probe_artifacts(const char *path, fw_artifact_probe_t *out) {
    if (!path || !path[0] || !out) return -1;
    memset(out, 0, sizeof(*out));

    fw_artifacts_t a;
    if (fw_detect(path, &a) != 0) return -1;

    out->has_merged = a.has_merged;
    out->has_split = a.has_split;
    out->has_fs = a.has_fs;
    out->has_elf = a.has_elf;
    fw_set_str(out->merged, sizeof(out->merged), a.merged);
    fw_set_str(out->bootloader, sizeof(out->bootloader), a.bootloader);
    fw_set_str(out->partitions, sizeof(out->partitions), a.partitions);
    fw_set_str(out->app, sizeof(out->app), a.app);
    fw_set_str(out->elf, sizeof(out->elf), a.elf);
    fw_set_str(out->fs, sizeof(out->fs), a.fs);
    out->size_merged = a.size_merged;
    out->size_bootloader = a.size_bootloader;
    out->size_partitions = a.size_partitions;
    out->size_app = a.size_app;
    out->size_fs = a.size_fs;

    /* Name heuristics. */
    if (a.merged[0] && contains_ci(path_basename(a.merged), "merged")) out->name_merged_hint = true;
    if (a.has_merged && !a.has_split) out->name_merged_hint = true;
    if (a.has_split) out->name_split_hint = true;
    if (!out->name_merged_hint && !out->name_split_hint) {
        const char *base = path_basename(path);
        if (contains_ci(base, "merged")) out->name_merged_hint = true;
        if (contains_ci(base, "bootloader") || contains_ci(base, "partitions")) out->name_split_hint = true;
    }

    unsigned char hdr[2];
    bool checked_any = false;
    bool all_ok = true;
    if (out->has_merged) {
        checked_any = true;
        out->merged_magic_ok = read_head_bytes(out->merged, hdr, 1) && hdr[0] == FW_IMAGE_MAGIC;
        if (!out->merged_magic_ok) all_ok = false;
    }
    if (out->has_split) {
        checked_any = true;
        out->bootloader_magic_ok = read_head_bytes(out->bootloader, hdr, 1) && hdr[0] == FW_IMAGE_MAGIC;
        out->app_magic_ok = read_head_bytes(out->app, hdr, 1) && hdr[0] == FW_IMAGE_MAGIC;
        out->partitions_magic_ok = read_head_bytes(out->partitions, hdr, 2) &&
                                   hdr[0] == FW_PART_MAGIC0 && hdr[1] == FW_PART_MAGIC1;
        if (!out->bootloader_magic_ok || !out->app_magic_ok || !out->partitions_magic_ok) all_ok = false;
    }
    out->signature_ok = checked_any ? all_ok : true;

    /* Manifest discovery: <dir>/manifest.json, then parents (up to two). */
    char dir[FW_MAX_PATH];
    fw_set_str(dir, sizeof(dir), path);
    if (!fw_path_is_dir(path)) {
        char *slash = strrchr(dir, '/');
        if (slash) *slash = '\0';
        else fw_set_str(dir, sizeof(dir), ".");
    }
    char board_id[FW_MAX_SHORT] = "";
    snprintf(board_id, sizeof(board_id), "%.*s", (int)(sizeof(board_id) - 1), path_basename(dir));
    if (strcmp(board_id, ".") == 0 || strcmp(board_id, "firmware") == 0) board_id[0] = '\0';

    char up1[FW_MAX_PATH];
    fw_set_str(up1, sizeof(up1), dir);
    char *s1 = strrchr(up1, '/');
    if (s1) {
        if (s1 == up1) up1[1] = '\0';
        else *s1 = '\0';
    } else {
        fw_set_str(up1, sizeof(up1), ".");
    }
    char up2[FW_MAX_PATH];
    fw_set_str(up2, sizeof(up2), up1);
    char *s2 = strrchr(up2, '/');
    if (s2) {
        if (s2 == up2) up2[1] = '\0';
        else *s2 = '\0';
    } else {
        fw_set_str(up2, sizeof(up2), ".");
    }

    const char *dirs[3] = {dir, up1, up2};
    for (int i = 0; i < 3 && !out->manifest_found; i++) {
        char mf[FW_MAX_PATH];
        snprintf(mf, sizeof(mf), "%s/manifest.json", dirs[i]);
        if (!fw_path_is_file(mf) || fw_file_size(mf) == 0) continue;
        out->manifest_found = true;
        fw_set_str(out->manifest_path, sizeof(out->manifest_path), mf);
        char *txt = read_text_head(mf, 65536);
        if (txt) {
            if (board_id[0] && contains_ci(txt, board_id)) out->manifest_board_match = true;
            free(txt);
        }
    }

    size_t off = 0;
    if (out->name_merged_hint) fw_appendf(out->note, sizeof(out->note), &off, "merged-like input; ");
    if (out->name_split_hint) fw_appendf(out->note, sizeof(out->note), &off, "split-like input; ");
    if (out->manifest_found) {
        fw_appendf(out->note, sizeof(out->note), &off, "manifest=%s%s",
                   out->manifest_path, out->manifest_board_match ? " (board match)" : "");
    } else {
        fw_appendf(out->note, sizeof(out->note), &off, "no manifest.json; name heuristics only");
    }
    if (!out->signature_ok) {
        fw_appendf(out->note, sizeof(out->note), &off, "; signature mismatch (check magic/inputs)");
    }
    if (!out->note[0]) fw_set_str(out->note, sizeof(out->note), "no merged/split artefacts detected");
    return 0;
}
