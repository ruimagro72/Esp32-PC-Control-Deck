#include "storage_littlefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void slfs_copy_str(char *dst, size_t dstsz, const char *src) {
    if (!dst || dstsz == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dstsz, "%s", src);
}

static void slfs_append(char *buf, size_t bufsz, const char *s) {
    if (!buf || bufsz == 0 || !s) return;
    size_t len = strlen(buf);
    if (len >= bufsz - 1) return;
    snprintf(buf + len, bufsz - len, "%s", s);
}

static bool slfs_path_exists(const char *path) {
    if (!path || !path[0]) return false;
    struct stat st;
    return stat(path, &st) == 0;
}

const char *slfs_capability_str(slfs_capability_t cap) {
    switch (cap) {
        case SLFS_CAP_SUPPORTED_NOW: return "supported-now";
        case SLFS_CAP_NEEDS_RUNTIME_HOST: return "needs-runtime-host";
        case SLFS_CAP_BLOCKED_BY_EMU: return "blocked-by-emu";
        case SLFS_CAP_CONDITIONAL: return "conditional(version/tool)";
        default: return "unknown";
    }
}

const char *slfs_sd_mode_str(slfs_sd_mode_t mode) {
    switch (mode) {
        case SLFS_SD_MODE_SDMMC: return "SDMMC";
        case SLFS_SD_MODE_SDSPI: return "SDSPI";
        case SLFS_SD_MODE_NONE: return "none";
        default: return "unknown";
    }
}

int slfs_sd_mode_from_str(const char *s, slfs_sd_mode_t *out) {
    if (!s || !out) return -1;
    if (strcmp(s, "SDMMC") == 0 || strcmp(s, "sdmmc") == 0) { *out = SLFS_SD_MODE_SDMMC; return 0; }
    if (strcmp(s, "SDSPI") == 0 || strcmp(s, "sdspi") == 0) { *out = SLFS_SD_MODE_SDSPI; return 0; }
    if (strcmp(s, "none") == 0 || strcmp(s, "NONE") == 0) { *out = SLFS_SD_MODE_NONE; return 0; }
    return -1;
}

void slfs_littlefs_init(slfs_littlefs_t *lfs) {
    if (!lfs) return;
    memset(lfs, 0, sizeof(*lfs));
    lfs->enabled = false;
    lfs->image_optional = false;
    lfs->capability = SLFS_CAP_CONDITIONAL;
    slfs_copy_str(lfs->partition_label, sizeof(lfs->partition_label), "littlefs");
    slfs_copy_str(lfs->mount_path, sizeof(lfs->mount_path), "/littlefs");
    lfs->offset = 0;
    lfs->size = 0;
}

void slfs_sd_init(slfs_sd_t *sd) {
    if (!sd) return;
    memset(sd, 0, sizeof(*sd));
    sd->enabled = false;
    sd->mode = SLFS_SD_MODE_SDMMC;
    sd->read_only = false;
    sd->slot = -1;
    sd->cs_pin = -1;
    sd->capability = SLFS_CAP_SUPPORTED_NOW;
    slfs_copy_str(sd->capability_note, sizeof(sd->capability_note),
                  "SDMMC via esp-emu 1.1.0 --sd (ESP32-S3); needs a card image");
}

void slfs_config_init(slfs_config_t *cfg) {
    if (!cfg) return;
    slfs_littlefs_init(&cfg->littlefs);
    slfs_sd_init(&cfg->sd);
}

void slfs_config_defaults(slfs_config_t *cfg) {
    if (!cfg) return;
    slfs_config_init(cfg);
    /* LittleFS: common alternative to SD (usable when image/offset/size match). */
    cfg->littlefs.enabled = false;
    cfg->littlefs.capability = SLFS_CAP_CONDITIONAL;
    cfg->littlefs.image_optional = true;
    /* SD: modeled as SDMMC (esp-emu 1.1.0 --sd); disabled until an image is set. */
    cfg->sd.enabled = false;
    cfg->sd.mode = SLFS_SD_MODE_SDMMC;
    cfg->sd.capability = SLFS_CAP_SUPPORTED_NOW;
}

int slfs_littlefs_set_image(slfs_littlefs_t *lfs, const char *path) {
    if (!lfs) return -1;
    slfs_copy_str(lfs->image_path, sizeof(lfs->image_path), path ? path : "");
    return 0;
}

int slfs_littlefs_set_partition(slfs_littlefs_t *lfs, const char *label,
                                uint32_t offset, uint32_t size) {
    if (!lfs) return -1;
    if (label && label[0]) {
        slfs_copy_str(lfs->partition_label, sizeof(lfs->partition_label), label);
    }
    lfs->offset = offset;
    lfs->size = size;
    return 0;
}

int slfs_littlefs_set_mount(slfs_littlefs_t *lfs, const char *mount_path) {
    if (!lfs) return -1;
    slfs_copy_str(lfs->mount_path, sizeof(lfs->mount_path), mount_path ? mount_path : "");
    return 0;
}

int slfs_sd_set_image(slfs_sd_t *sd, const char *path) {
    if (!sd) return -1;
    slfs_copy_str(sd->image_path, sizeof(sd->image_path), path ? path : "");
    return 0;
}

int slfs_sd_set_mode(slfs_sd_t *sd, slfs_sd_mode_t mode) {
    if (!sd) return -1;
    sd->mode = mode;
    return 0;
}

int slfs_sd_set_hints(slfs_sd_t *sd, bool read_only, int slot, int cs_pin) {
    if (!sd) return -1;
    sd->read_only = read_only;
    sd->slot = slot;
    sd->cs_pin = cs_pin;
    return 0;
}

int slfs_apply_capabilities(slfs_config_t *cfg, int emu_major, int emu_minor,
                            int emu_patch, bool is_esp32s3) {
    if (!cfg) return -1;
    /* --sd is available from esp-emu 1.1.0 (verified on the ESP32-S3 SDMMC host). */
    bool ver_ge_1_1 = (emu_major > 1) || (emu_major == 1 && emu_minor >= 1);
    (void)emu_patch;

    if (ver_ge_1_1 && is_esp32s3) {
        cfg->sd.capability = SLFS_CAP_SUPPORTED_NOW;
        slfs_copy_str(cfg->sd.capability_note, sizeof(cfg->sd.capability_note),
                      "esp-emu 1.1.0 mounts the card image with --sd (ESP32-S3 SDMMC)");
    } else {
        cfg->sd.capability = SLFS_CAP_CONDITIONAL;
        slfs_copy_str(cfg->sd.capability_note, sizeof(cfg->sd.capability_note),
                      is_esp32s3 ? "conditional(version/tool): --sd needs esp-emu 1.1.0 or newer"
                                 : "conditional(version/tool/chip): SD support on this chip is not verified");
    }

    /* LittleFS via merge/data image is generally conditional on correct image/offset/size. */
    cfg->littlefs.capability = SLFS_CAP_CONDITIONAL;
    return 0;
}

void slfs_report_init(slfs_report_t *r) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
    r->ok = true;
    r->error_count = 0;
    r->warning_count = 0;
}

static void slfs_report_error(slfs_report_t *r, const char *msg) {
    if (!r || !msg) return;
    r->ok = false;
    r->error_count++;
    if (r->errors[0]) slfs_append(r->errors, sizeof(r->errors), "; ");
    slfs_append(r->errors, sizeof(r->errors), msg);
}

static void slfs_report_warn(slfs_report_t *r, const char *msg) {
    if (!r || !msg) return;
    r->warning_count++;
    if (r->warnings[0]) slfs_append(r->warnings, sizeof(r->warnings), "; ");
    slfs_append(r->warnings, sizeof(r->warnings), msg);
}

static void slfs_report_note(slfs_report_t *r, const char *msg) {
    if (!r || !msg) return;
    if (r->notes[0]) slfs_append(r->notes, sizeof(r->notes), "; ");
    slfs_append(r->notes, sizeof(r->notes), msg);
}

int slfs_validate(const slfs_config_t *cfg, bool check_paths, slfs_report_t *out) {
    if (!cfg || !out) return -1;
    slfs_report_init(out);

    /* LittleFS checks */
    if (cfg->littlefs.enabled) {
        if (cfg->littlefs.image_path[0]) {
            if (check_paths && !slfs_path_exists(cfg->littlefs.image_path)) {
                if (cfg->littlefs.image_optional) {
                    slfs_report_warn(out, "LittleFS image path does not exist (optional)");
                } else {
                    slfs_report_error(out, "LittleFS image path does not exist");
                }
            }
        } else {
            if (cfg->littlefs.image_optional) {
                slfs_report_warn(out, "LittleFS enabled but image path not set");
            } else {
                slfs_report_error(out, "LittleFS enabled but image path not set");
            }
        }
        if (cfg->littlefs.size != 0 && cfg->littlefs.offset > UINT32_MAX - cfg->littlefs.size) {
            slfs_report_error(out, "LittleFS offset+size overflow");
        }
        slfs_report_note(out, "LittleFS is conditional(version/tool): image/offset/size must match partition table");
    }

    /* SD checks */
    if (cfg->sd.enabled) {
        if (cfg->sd.mode == SLFS_SD_MODE_NONE) {
            slfs_report_error(out, "SD enabled but mode is none");
        }
        if (cfg->sd.image_path[0]) {
            if (check_paths && !slfs_path_exists(cfg->sd.image_path)) {
                slfs_report_error(out, "SD image path does not exist");
            }
        } else {
            slfs_report_warn(out, "SD enabled but image path not set");
        }
        if (cfg->sd.slot < -1) {
            slfs_report_error(out, "SD slot must be -1 or >= 0");
        }
        if (cfg->sd.cs_pin < -1) {
            slfs_report_error(out, "SD CS pin must be -1 or >= 0");
        }
        if (cfg->sd.capability == SLFS_CAP_BLOCKED_BY_EMU) {
            slfs_report_warn(out, "SD is blocked by esp-emu in this configuration; args unavailable");
        } else if (cfg->sd.capability == SLFS_CAP_CONDITIONAL) {
            slfs_report_note(out, "SD is conditional(version/tool/chip): verify esp-emu --sd support and firmware path");
        }
    }

    if (cfg->sd.capability_note[0]) {
        slfs_report_note(out, cfg->sd.capability_note);
    }

    return 0;
}

int slfs_build_merge_args(const slfs_config_t *cfg, char *buf, size_t buflen,
                          char *note_out, size_t note_len) {
    if (note_out && note_len) note_out[0] = '\0';
    if (!cfg || !buf || buflen == 0) return -1;
    buf[0] = '\0';

    if (!cfg->littlefs.enabled) {
        slfs_copy_str(note_out, note_len, "LittleFS disabled: no merge args");
        return 0;
    }
    if (!cfg->littlefs.image_path[0]) {
        slfs_copy_str(note_out, note_len, "LittleFS enabled but image path missing: cannot emit --littlefs arg");
        return 0;
    }

    /* start.sh merge accepts --littlefs <img> in this repo's flow. */
    int n = snprintf(buf, buflen, "--littlefs %s", cfg->littlefs.image_path);
    if (n < 0 || (size_t)n >= buflen) return -1;
    slfs_copy_str(note_out, note_len,
                  "LittleFS merge arg emitted (conditional: image/offset/size must match partition)");
    return n;
}

int slfs_build_sd_args(const slfs_config_t *cfg, char *buf, size_t buflen,
                       char *note_out, size_t note_len) {
    if (note_out && note_len) note_out[0] = '\0';
    if (!cfg || !buf || buflen == 0) return -1;
    buf[0] = '\0';

    if (!cfg->sd.enabled) {
        slfs_copy_str(note_out, note_len, "SD disabled: no args");
        return 0;
    }

    /* No --sd when the configuration is blocked by esp-emu. */
    if (cfg->sd.capability == SLFS_CAP_BLOCKED_BY_EMU) {
        slfs_copy_str(note_out, note_len,
                      "SD args unavailable: blocked-by-emu (no --sd for this configuration)");
        return 0;
    }

    if (!cfg->sd.image_path[0]) {
        slfs_copy_str(note_out, note_len, "SD enabled but image path missing: no args emitted");
        return 0;
    }

    /* --sd fragment (esp-emu 1.1.0 and newer). */
    int n = 0;
    if (cfg->sd.read_only) {
        n = snprintf(buf, buflen, "--sd %s:ro", cfg->sd.image_path);
    } else {
        n = snprintf(buf, buflen, "--sd %s", cfg->sd.image_path);
    }
    if (n < 0 || (size_t)n >= buflen) return -1;

    if (cfg->sd.capability == SLFS_CAP_CONDITIONAL) {
        slfs_copy_str(note_out, note_len,
                      "SD args emitted as conditional(version/tool/chip): verify esp-emu --help/--sd support");
    } else {
        slfs_copy_str(note_out, note_len, "SD args emitted");
    }
    return n;
}

int slfs_build_args(const slfs_config_t *cfg, char *buf, size_t buflen,
                    char *note_out, size_t note_len) {
    if (note_out && note_len) note_out[0] = '\0';
    if (!cfg || !buf || buflen == 0) return -1;
    buf[0] = '\0';

    char merge_buf[SLFS_MAX_ARGS];
    char sd_buf[SLFS_MAX_ARGS];
    char merge_note[SLFS_MAX_MSG];
    char sd_note[SLFS_MAX_MSG];
    merge_buf[0] = sd_buf[0] = '\0';
    merge_note[0] = sd_note[0] = '\0';

    int m = slfs_build_merge_args(cfg, merge_buf, sizeof(merge_buf), merge_note, sizeof(merge_note));
    int s = slfs_build_sd_args(cfg, sd_buf, sizeof(sd_buf), sd_note, sizeof(sd_note));
    if (m < 0 || s < 0) return -1;

    int written = 0;
    if (merge_buf[0]) {
        written = snprintf(buf, buflen, "%s", merge_buf);
        if (written < 0 || (size_t)written >= buflen) return -1;
    }
    if (sd_buf[0]) {
        if (buf[0]) {
            size_t len = strlen(buf);
            int n = snprintf(buf + len, buflen - len, " %s", sd_buf);
            if (n < 0 || (size_t)n >= (buflen - len)) return -1;
            written = (int)strlen(buf);
        } else {
            written = snprintf(buf, buflen, "%s", sd_buf);
            if (written < 0 || (size_t)written >= buflen) return -1;
        }
    }

    if (note_out && note_len) {
        if (merge_note[0]) slfs_append(note_out, note_len, merge_note);
        if (sd_note[0]) {
            if (note_out[0]) slfs_append(note_out, note_len, " | ");
            slfs_append(note_out, note_len, sd_note);
        }
        if (!note_out[0]) slfs_copy_str(note_out, note_len, "No storage args applicable");
    }
    return written;
}

int slfs_format_summary(const slfs_config_t *cfg, char *buf, size_t buflen) {
    if (!cfg || !buf || buflen == 0) return -1;
    buf[0] = '\0';

    char tmp[SLFS_MAX_PATH + 128];
    slfs_append(buf, buflen, "Storage (LittleFS/SD)\n");

    snprintf(tmp, sizeof(tmp), "  LittleFS: %s [%s]\n",
             cfg->littlefs.enabled ? "enabled" : "disabled",
             slfs_capability_str(cfg->littlefs.capability));
    slfs_append(buf, buflen, tmp);
    snprintf(tmp, sizeof(tmp), "    image: %s\n",
             cfg->littlefs.image_path[0] ? cfg->littlefs.image_path : "(none)");
    slfs_append(buf, buflen, tmp);
    snprintf(tmp, sizeof(tmp), "    partition: label=%s offset=0x%08x size=%u\n",
             cfg->littlefs.partition_label[0] ? cfg->littlefs.partition_label : "(none)",
             (unsigned)cfg->littlefs.offset, (unsigned)cfg->littlefs.size);
    slfs_append(buf, buflen, tmp);
    snprintf(tmp, sizeof(tmp), "    mount: %s\n",
             cfg->littlefs.mount_path[0] ? cfg->littlefs.mount_path : "(none)");
    slfs_append(buf, buflen, tmp);

    snprintf(tmp, sizeof(tmp), "  SD: %s [%s] mode=%s\n",
             cfg->sd.enabled ? "enabled" : "disabled",
             slfs_capability_str(cfg->sd.capability),
             slfs_sd_mode_str(cfg->sd.mode));
    slfs_append(buf, buflen, tmp);
    snprintf(tmp, sizeof(tmp), "    image: %s%s\n",
             cfg->sd.image_path[0] ? cfg->sd.image_path : "(none)",
             cfg->sd.read_only ? " (ro)" : "");
    slfs_append(buf, buflen, tmp);
    snprintf(tmp, sizeof(tmp), "    hints: slot=%d cs_pin=%d\n", cfg->sd.slot, cfg->sd.cs_pin);
    slfs_append(buf, buflen, tmp);
    if (cfg->sd.capability_note[0]) {
        slfs_append(buf, buflen, "    note: ");
        slfs_append(buf, buflen, cfg->sd.capability_note);
        slfs_append(buf, buflen, "\n");
    }

    return (int)strlen(buf);
}
