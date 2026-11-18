#ifndef SIM_LAUNCHER_HELP_UI_H
#define SIM_LAUNCHER_HELP_UI_H

#include <gtk/gtk.h>

/* Help windows of the launcher (Help menu). Kept out of sim_launcher.c. */

/* Help contents: renders docs/simulator/LAUNCHER-HELP.md (repo root, or next to the binary). */
void help_show_contents(GtkWindow *parent);

/* Keyboard shortcuts of the launcher menus. */
void help_show_shortcuts(GtkWindow *parent);

/* About dialog: launcher, esp-emu version, project and licence. */
void help_show_about(GtkWindow *parent);

#endif /* SIM_LAUNCHER_HELP_UI_H */
