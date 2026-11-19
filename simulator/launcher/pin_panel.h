#ifndef PIN_PANEL_H
#define PIN_PANEL_H

/*
 * Pin panel: a grid of GPIO buttons for the launcher (esp-emu "GPIO" panel).
 * Standalone GTK3 widget. Not wired into sim_launcher yet.
 *
 * Colours: output high = filled orange, output low = orange outline,
 * input high = filled neutral, input low = neutral outline, unused = dim.
 * Clicking an input pin toggles it and calls the click callback; outputs and
 * unused pins ignore clicks.
 */

#include <gtk/gtk.h>

typedef enum {
    PIN_UNUSED = 0,
    PIN_OUT_LOW,
    PIN_OUT_HIGH,
    PIN_IN_LOW,
    PIN_IN_HIGH
} pin_state_t;

/* level is 0 (low) or 1 (high), the new level of the input pin */
typedef void (*pin_click_cb_t)(int pin, int level, void *user);

/* Panel for the given pin numbers (count may be 0), 8 pins per row. */
GtkWidget *pin_panel_new(const int *pins, int count);

/* Sets the state of one pin. Pins not in the panel are ignored. */
void pin_panel_set_state(GtkWidget *panel, int pin, pin_state_t state);

/* Callback for clicks on input pins. NULL cb removes it. */
void pin_panel_set_click_cb(GtkWidget *panel, pin_click_cb_t cb, void *user);

/* Tooltip text for one pin, e.g. "LCD CS". NULL resets it to "GPIO n". */
void pin_panel_set_label(GtkWidget *panel, int pin, const char *text);

/* CSS for the panel. Add it to the launcher style provider (do not edit theme.c). */
const char *pin_panel_css(void);

#endif
