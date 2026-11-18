#ifndef SIM_LAUNCHER_I18N_H
#define SIM_LAUNCHER_I18N_H

#include <stddef.h>
#include <libintl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Source strings are English. _() translates at use; N_() only marks strings in static tables
 * (translate them where they are displayed with _()). Text domain: sim_launcher. */
#define _(s) gettext(s)
#define N_(s) (s)

/* Reads the saved ui_lang value from launcher.conf (before any config load or widget exists).
 * Writes "auto" when the key is missing. */
void i18n_read_saved_setting(const char *conf_path, char *out, size_t outsz);

/* lang_setting: "auto" (keep the environment), or a language code such as "en" or "pt".
 * Call once, first thing in main(), before GTK creates any widget. */
void i18n_init(const char *lang_setting);

#ifdef __cplusplus
}
#endif

#endif /* SIM_LAUNCHER_I18N_H */
