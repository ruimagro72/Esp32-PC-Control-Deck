#include "i18n.h"

#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define I18N_DOMAIN "sim_launcher"

static int is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* <dir of the executable>/locale, else simulator/launcher/locale relative to the working directory. */
static void locale_dir(char *out, size_t outsz) {
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0 && (size_t)n < sizeof(exe)) {
        exe[n] = '\0';
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = '\0';
            size_t len = strlen(exe);
            if (len + sizeof("/locale") <= outsz) {
                memcpy(out, exe, len);
                memcpy(out + len, "/locale", sizeof("/locale"));
                if (is_dir(out)) return;
            }
        }
    }
    snprintf(out, outsz, "simulator/launcher/locale");
}

void i18n_read_saved_setting(const char *conf_path, char *out, size_t outsz) {
    if (!out || outsz == 0) return;
    snprintf(out, outsz, "auto");
    if (!conf_path) return;
    FILE *f = fopen(conf_path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "ui_lang=", 8) != 0) continue;
        char *val = line + 8;
        char *nl = strpbrk(val, "\r\n");
        if (nl) *nl = '\0';
        if (val[0]) snprintf(out, outsz, "%s", val);
        break;
    }
    fclose(f);
}

void i18n_init(const char *lang_setting) {
    if (lang_setting && lang_setting[0] && strcmp(lang_setting, "auto") != 0) {
        /* GNU gettext honours LANGUAGE even when the system has no such locale installed. */
        setenv("LANGUAGE", lang_setting, 1);
    }
    setlocale(LC_ALL, "");
    char dir[PATH_MAX];
    locale_dir(dir, sizeof(dir));
    bindtextdomain(I18N_DOMAIN, dir);
    bind_textdomain_codeset(I18N_DOMAIN, "UTF-8");
    textdomain(I18N_DOMAIN);
}
