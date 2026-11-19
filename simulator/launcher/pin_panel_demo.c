/*
 * pin_panel_demo: shows the pin panel with the ES3C28P pins and fake states.
 * Not part of build.sh. Build by hand:
 *   gcc -o pin_panel_demo pin_panel.c pin_panel_demo.c $(pkg-config --cflags --libs gtk+-3.0)
 * Pin functions taken from firmware/src/Hal/Boards/es3c28p/BoardConfig.h,
 * firmware/src/Hal/Audio/AudioEngine.cpp (ES3C28P branch) and
 * simulator/emu/board-es3c28p.json.
 */

#include "pin_panel.h"

#include <stdio.h>

typedef struct {
    int pin;
    const char *name;
    pin_state_t fake;   /* fake state for the demo */
} es3c28p_pin_t;

static const es3c28p_pin_t k_pins[] = {
    {  1, "Audio amplifier enable (active low)", PIN_OUT_LOW },
    {  4, "I2S MCLK (ES8311)",                  PIN_OUT_HIGH },
    {  5, "I2S BCK (ES8311)",                   PIN_OUT_HIGH },
    {  7, "I2S WS (ES8311)",                    PIN_OUT_HIGH },
    {  8, "I2S DOUT (speaker)",                 PIN_OUT_LOW },
    {  9, "Battery ADC",                        PIN_IN_HIGH },
    { 10, "LCD CS",                             PIN_OUT_HIGH },
    { 11, "LCD MOSI",                           PIN_OUT_LOW },
    { 12, "LCD SCLK",                           PIN_OUT_LOW },
    { 13, "LCD MISO",                           PIN_IN_HIGH },
    { 15, "Touch and codec I2C SCL",            PIN_OUT_HIGH },
    { 16, "Touch and codec I2C SDA",            PIN_OUT_HIGH },
    { 17, "Touch INT",                          PIN_IN_HIGH },
    { 18, "Touch RST",                          PIN_OUT_HIGH },
    { 38, "SD CLK",                             PIN_OUT_LOW },
    { 39, "SD D0",                              PIN_IN_HIGH },
    { 40, "SD CMD",                             PIN_OUT_HIGH },
    { 41, "SD D1",                              PIN_IN_HIGH },
    { 42, "RGB status LED (WS2812)",            PIN_OUT_LOW },
    { 45, "LCD backlight",                      PIN_OUT_HIGH },
    { 46, "LCD DC",                             PIN_OUT_LOW },
    { 47, "SD D3",                              PIN_IN_HIGH },
    { 48, "SD D2",                              PIN_IN_HIGH },
};

#define N_PINS ((int)(sizeof k_pins / sizeof k_pins[0]))

static void on_pin_click(int pin, int level, void *user)
{
    (void)user;
    printf("input pin %d -> %s\n", pin, level ? "high" : "low");
    fflush(stdout);
}

static void apply_css(void)
{
    GtkCssProvider *prov = gtk_css_provider_new();
    GdkScreen *screen = gdk_screen_get_default();
    gtk_css_provider_load_from_data(prov,
        "window { background-color: #0b0f14; color: #cfd6e0; }\n", -1, NULL);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(prov),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(prov);

    prov = gtk_css_provider_new();
    gtk_css_provider_load_from_data(prov, pin_panel_css(), -1, NULL);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(prov),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(prov);
}

int main(int argc, char **argv)
{
    GtkWidget *win, *panel;
    int pins[N_PINS];
    int i;

    gtk_init(&argc, &argv);
    apply_css();

    for (i = 0; i < N_PINS; i++) pins[i] = k_pins[i].pin;
    panel = pin_panel_new(pins, N_PINS);
    for (i = 0; i < N_PINS; i++) {
        pin_panel_set_label(panel, k_pins[i].pin, k_pins[i].name);
        pin_panel_set_state(panel, k_pins[i].pin, k_pins[i].fake);
    }
    pin_panel_set_click_cb(panel, on_pin_click, NULL);

    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "Pin panel demo (ES3C28P)");
    gtk_container_set_border_width(GTK_CONTAINER(win), 12);
    gtk_container_add(GTK_CONTAINER(win), panel);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(win);

    gtk_main();
    return 0;
}
