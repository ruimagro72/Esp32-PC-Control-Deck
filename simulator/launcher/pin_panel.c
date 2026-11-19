#include "pin_panel.h"

#include <stdio.h>
#include <stdlib.h>

#define PIN_COLS 8

/* Palette (same hex values as theme.c): bg #0b0f14, bar #0e141b, panel #131a22,
 * border #232d3a, fg #cfd6e0, dim #7d8a99, accent #f5a623. */
static const char *k_css =
"button.pin-btn { min-width: 40px; min-height: 26px; padding: 2px 4px; border-radius: 6px;\n"
"  font-family: \"DejaVu Sans Mono\", monospace; font-size: 11px; box-shadow: none; outline: none; }\n"
"button.pin-btn.pin-unused, .pin-swatch.pin-unused { background-color: #0e141b; border: 1px solid #232d3a; color: #4b5663; }\n"
"button.pin-btn.pin-out-high, .pin-swatch.pin-out-high { background-color: #f5a623; border: 1px solid #f5a623; color: #1a1204; }\n"
"button.pin-btn.pin-out-low, .pin-swatch.pin-out-low { background-color: #0e141b; border: 1px solid #f5a623; color: #f5a623; }\n"
"button.pin-btn.pin-in-high, .pin-swatch.pin-in-high { background-color: #cfd6e0; border: 1px solid #cfd6e0; color: #0b0f14; }\n"
"button.pin-btn.pin-in-low, .pin-swatch.pin-in-low { background-color: #0e141b; border: 1px solid #7d8a99; color: #7d8a99; }\n"
"button.pin-btn.pin-in-low:hover, button.pin-btn.pin-in-high:hover { border-color: #f5a623; }\n"
"button.pin-btn.pin-out-low:hover, button.pin-btn.pin-out-high:hover,\n"
"button.pin-btn.pin-unused:hover { background-color: #0e141b; }\n"
".pin-swatch { min-width: 14px; min-height: 14px; border-radius: 3px; }\n"
".pin-caption { color: #7d8a99; font-size: 11px; }\n";

static const char *k_state_class[] = {
    "pin-unused", "pin-out-low", "pin-out-high", "pin-in-low", "pin-in-high"
};

typedef struct {
    int pin;
    pin_state_t state;
    GtkWidget *btn;
} pin_cell_t;

typedef struct {
    GtkWidget *grid;
    pin_cell_t *cells;
    int count;
    pin_click_cb_t cb;
    void *user;
} pin_panel_priv_t;

static pin_panel_priv_t *priv_of(GtkWidget *panel)
{
    return panel ? g_object_get_data(G_OBJECT(panel), "pin-panel") : NULL;
}

static pin_cell_t *find_cell(pin_panel_priv_t *p, int pin)
{
    int i;
    if (!p) return NULL;
    for (i = 0; i < p->count; i++)
        if (p->cells[i].pin == pin) return &p->cells[i];
    return NULL;
}

static void apply_class(pin_cell_t *c)
{
    GtkStyleContext *sc = gtk_widget_get_style_context(c->btn);
    int i;
    for (i = 0; i < 5; i++) gtk_style_context_remove_class(sc, k_state_class[i]);
    gtk_style_context_add_class(sc, k_state_class[c->state]);
}

static void on_clicked(GtkButton *btn, gpointer data)
{
    pin_panel_priv_t *p = data;
    pin_cell_t *c = g_object_get_data(G_OBJECT(btn), "pin-cell");
    if (!c) return;
    if (c->state == PIN_IN_LOW) c->state = PIN_IN_HIGH;
    else if (c->state == PIN_IN_HIGH) c->state = PIN_IN_LOW;
    else return;
    apply_class(c);
    if (p->cb) p->cb(c->pin, c->state == PIN_IN_HIGH ? 1 : 0, p->user);
}

static void free_priv(gpointer data)
{
    pin_panel_priv_t *p = data;
    g_free(p->cells);
    g_free(p);
}

static void add_cell(pin_panel_priv_t *p, int index, int pin)
{
    pin_cell_t *c = &p->cells[index];
    char buf[16];
    GtkWidget *btn;
    c->pin = pin;
    c->state = PIN_UNUSED;
    snprintf(buf, sizeof buf, "%d", pin);
    btn = gtk_button_new_with_label(buf);
    c->btn = btn;
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "pin-btn");
    g_object_set_data(G_OBJECT(btn), "pin-cell", c);
    snprintf(buf, sizeof buf, "GPIO %d", pin);
    gtk_widget_set_tooltip_text(btn, buf);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_clicked), p);
    gtk_grid_attach(GTK_GRID(p->grid), btn, index % PIN_COLS, index / PIN_COLS, 1, 1);
    apply_class(c);
}

static GtkWidget *legend_item(const char *cls, const char *text)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *sw = gtk_label_new(NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(sw), "pin-swatch");
    gtk_style_context_add_class(gtk_widget_get_style_context(sw), cls);
    gtk_widget_set_size_request(sw, 14, 14);
    gtk_box_pack_start(GTK_BOX(row), sw, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new(text), FALSE, FALSE, 0);
    return row;
}

static GtkWidget *build_legend(void)
{
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_attach(GTK_GRID(grid), legend_item("pin-out-high", "Output high"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), legend_item("pin-out-low", "Output low"), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), legend_item("pin-in-high", "Input high"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), legend_item("pin-in-low", "Input low"), 1, 1, 1, 1);
    return grid;
}

GtkWidget *pin_panel_new(const int *pins, int count)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *caption = gtk_label_new("Click an input pin to drive it. "
                                       "Pins the firmware configured as outputs show what it drives.");
    pin_panel_priv_t *p = g_new0(pin_panel_priv_t, 1);
    int i;

    gtk_style_context_add_class(gtk_widget_get_style_context(caption), "pin-caption");
    gtk_label_set_line_wrap(GTK_LABEL(caption), TRUE);
    gtk_label_set_xalign(GTK_LABEL(caption), 0.0f);

    p->grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(p->grid), 6);
    gtk_grid_set_row_spacing(GTK_GRID(p->grid), 6);
    p->count = count > 0 ? count : 0;
    p->cells = g_new0(pin_cell_t, p->count > 0 ? p->count : 1);
    for (i = 0; i < p->count; i++) add_cell(p, i, pins[i]);

    gtk_box_pack_start(GTK_BOX(box), caption, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), p->grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), build_legend(), FALSE, FALSE, 0);
    g_object_set_data_full(G_OBJECT(box), "pin-panel", p, free_priv);
    return box;
}

void pin_panel_set_state(GtkWidget *panel, int pin, pin_state_t state)
{
    pin_cell_t *c = find_cell(priv_of(panel), pin);
    if (!c || state < PIN_UNUSED || state > PIN_IN_HIGH) return;
    c->state = state;
    apply_class(c);
}

void pin_panel_set_click_cb(GtkWidget *panel, pin_click_cb_t cb, void *user)
{
    pin_panel_priv_t *p = priv_of(panel);
    if (!p) return;
    p->cb = cb;
    p->user = user;
}

void pin_panel_set_label(GtkWidget *panel, int pin, const char *text)
{
    pin_cell_t *c = find_cell(priv_of(panel), pin);
    char buf[128];
    if (!c) return;
    if (text && *text) snprintf(buf, sizeof buf, "GPIO %d: %s", pin, text);
    else snprintf(buf, sizeof buf, "GPIO %d", pin);
    gtk_widget_set_tooltip_text(c->btn, buf);
}

const char *pin_panel_css(void)
{
    return k_css;
}
