/* Help windows of the launcher: contents (simple Markdown rendered with GtkTextTag), shortcuts, about. */
#include <gtk/gtk.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include "help_ui.h"
#include "i18n.h"

#define HELP_DOC_REL "docs/simulator/LAUNCHER-HELP.md"        /* from the repo root (start.sh runs there) */
#define HELP_DOC_FROM_BIN "../../docs/simulator/LAUNCHER-HELP.md" /* from simulator/launcher/ (the binary's folder) */

static GtkWidget *contents_win;
static GtkWidget *shortcuts_win;

/* Finds the help file: working directory first, then relative to the executable. Returns NULL or a path to g_free(). */
static gchar *help_doc_find(void) {
    if (g_file_test(HELP_DOC_REL, G_FILE_TEST_IS_REGULAR)) return g_strdup(HELP_DOC_REL);
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) return NULL;
    exe[n] = '\0';
    gchar *dir = g_path_get_dirname(exe);
    gchar *path = g_build_filename(dir, HELP_DOC_FROM_BIN, NULL);
    g_free(dir);
    if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) return path;
    g_free(path);
    return NULL;
}

/* Escape closes the help window (as its Close button does); Ctrl+Q quits the launcher through the main window's close path. */
static gboolean help_key_press(GtkWidget *win, GdkEventKey *ev, gpointer data) {
    (void)data;
    if (ev->keyval == GDK_KEY_Escape) {
        gtk_widget_destroy(win);
        return TRUE;
    }
    if (gdk_keyval_to_lower(ev->keyval) == GDK_KEY_q && (ev->state & gtk_accelerator_get_default_mod_mask()) == GDK_CONTROL_MASK) {
        GtkWindow *main_win = gtk_window_get_transient_for(GTK_WINDOW(win));
        if (main_win) gtk_window_close(main_win);  /* same close path as the window button: delete-event asks about unsaved profile changes */
        return TRUE;
    }
    return FALSE;
}

/* Creates a top-level help window with a vertical box inside (returned in *box). */
static GtkWidget *help_window(GtkWindow *parent, const char *title, int w, int h, GtkWidget **box) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), title);
    gtk_window_set_default_size(GTK_WINDOW(win), w, h);
    if (parent) gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(win), TRUE);
    g_signal_connect(win, "key-press-event", G_CALLBACK(help_key_press), NULL);
    gtk_container_set_border_width(GTK_CONTAINER(win), 12);
    *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(GTK_CONTAINER(win), *box);
    return win;
}

/* Adds a Close button at the bottom right of the box. */
static void help_close_button(GtkWidget *win, GtkWidget *box) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *btn = gtk_button_new_with_label(_("Close"));
    g_signal_connect_swapped(btn, "clicked", G_CALLBACK(gtk_widget_destroy), win);
    gtk_box_pack_end(GTK_BOX(row), btn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(box), row, FALSE, FALSE, 0);
}

/* Inserts text with an optional tag (NULL = no tag). */
static void help_put(GtkTextBuffer *buf, GtkTextIter *it, const char *s, size_t n, const char *tag) {
    if (n == 0) return;
    if (tag) gtk_text_buffer_insert_with_tags_by_name(buf, it, s, (gint)n, tag, NULL);
    else gtk_text_buffer_insert(buf, it, s, (gint)n);
}

/* [text](target) at s: sets *text_len (the text) and *next (after the closing parenthesis). FALSE if not a link. */
static gboolean help_link_at(const char *s, size_t *text_len, const char **next) {
    const char *close = strchr(s, ']');
    if (!close || close[1] != '(') return FALSE;
    const char *paren = strchr(close + 2, ')');
    if (!paren) return FALSE;
    *text_len = (size_t)(close - s - 1);
    *next = paren + 1;
    return TRUE;
}

/* Inline Markdown: `code` (monospace), **bold**, [text](target) (underlined text, no navigation). Other text keeps base_tag. */
static void help_put_inline(GtkTextBuffer *buf, GtkTextIter *it, const char *s, const char *base_tag) {
    while (*s) {
        const char *end = NULL;
        size_t n = 0;
        if (*s == '`' && (end = strchr(s + 1, '`'))) {
            help_put(buf, it, s + 1, (size_t)(end - s - 1), "code");
            s = end + 1;
        } else if (s[0] == '*' && s[1] == '*' && (end = strstr(s + 2, "**")) && end > s + 2) {
            help_put(buf, it, s + 2, (size_t)(end - s - 2), "bold");
            s = end + 2;
        } else if (*s == '[' && help_link_at(s, &n, &end)) {
            help_put(buf, it, s + 1, n, "link");
            s = end;
        } else {
            size_t plain = 1 + strcspn(s + 1, "`*[");
            help_put(buf, it, s, plain, base_tag);
            s += plain;
        }
    }
}

/* One Markdown line: #/##/### headings, - or * bullets (nested by indent), ``` fenced code blocks, | table rows. */
static void help_render_line(GtkTextBuffer *buf, GtkTextIter *it, const char *ln, gboolean *in_code) {
    if (strncmp(ln, "```", 3) == 0) {
        *in_code = !*in_code;
        return;
    }
    if (*in_code) {
        help_put(buf, it, ln, strlen(ln), "code");
        return;
    }
    if (ln[0] == '|') {  /* table row in monospace; separator rows (|---|---|) are skipped */
        if (strspn(ln + 1, "-:| ") != strlen(ln + 1)) help_put_inline(buf, it, ln, "code");
        return;
    }
    int h = 0;
    while (ln[h] == '#' && h < 3) h++;
    if (h > 0 && ln[h] == ' ') {
        static const char *const heading_tags[3] = { "h1", "h2", "h3" };
        help_put_inline(buf, it, ln + h + 1, heading_tags[h - 1]);
        return;
    }
    const char *b = ln;
    while (*b == ' ') b++;
    if ((b[0] == '-' || b[0] == '*') && b[1] == ' ') {
        help_put(buf, it, ln, (size_t)(b - ln), NULL);
        help_put(buf, it, "• ", strlen("• "), NULL);
        help_put_inline(buf, it, b + 2, NULL);
        return;
    }
    help_put_inline(buf, it, ln, NULL);
}

static void help_render_markdown(GtkTextBuffer *buf, const char *md) {
    gboolean in_code = FALSE;
    const char *p = md;
    for (;;) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char *ln = g_strndup(p, len);
        if (len > 0 && ln[len - 1] == '\r') ln[len - 1] = '\0';
        GtkTextIter it;
        gtk_text_buffer_get_end_iter(buf, &it);
        if (gtk_text_buffer_get_char_count(buf) > 0) help_put(buf, &it, "\n", 1, NULL);
        help_render_line(buf, &it, ln, &in_code);
        g_free(ln);
        if (!nl) break;
        p = nl + 1;
    }
}

static void help_create_tags(GtkTextBuffer *buf) {
    gtk_text_buffer_create_tag(buf, "h1", "weight", PANGO_WEIGHT_BOLD, "scale", 1.6, "pixels-above-lines", 10, NULL);
    gtk_text_buffer_create_tag(buf, "h2", "weight", PANGO_WEIGHT_BOLD, "scale", 1.3, "pixels-above-lines", 8, NULL);
    gtk_text_buffer_create_tag(buf, "h3", "weight", PANGO_WEIGHT_BOLD, "scale", 1.1, "pixels-above-lines", 4, NULL);
    gtk_text_buffer_create_tag(buf, "code", "family", "monospace", NULL);
    gtk_text_buffer_create_tag(buf, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
    gtk_text_buffer_create_tag(buf, "link", "underline", PANGO_UNDERLINE_SINGLE, NULL);
}

void help_show_contents(GtkWindow *parent) {
    if (contents_win) {
        gtk_window_present(GTK_WINDOW(contents_win));
        return;
    }
    GtkWidget *box = NULL;
    GtkWidget *win = help_window(parent, _("Help contents"), 760, 600, &box);
    contents_win = win;
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_widget_destroyed), &contents_win);

    GtkWidget *tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(tv), 8);
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
    help_create_tags(buf);

    gchar *path = help_doc_find();
    gchar *text = NULL;
    gsize len = 0;
    if (path && g_file_get_contents(path, &text, &len, NULL) && g_utf8_validate(text, (gssize)len, NULL)) {
        help_render_markdown(buf, text);
    } else {
        gchar *msg = g_strdup_printf(_("Help file not found. Expected: %s (relative to the repository root)."), HELP_DOC_REL);
        gtk_text_buffer_set_text(buf, msg, -1);
        g_free(msg);
    }
    g_free(text);
    g_free(path);

    GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sc), tv);
    gtk_box_pack_start(GTK_BOX(box), sc, TRUE, TRUE, 0);
    help_close_button(win, box);
    gtk_widget_show_all(win);
}

/* Key label and meaning; the labels are the menu accelerators of sim_launcher.c. */
static const struct { const char *keys; const char *what; } k_shortcuts[] = {
    { "F1",          N_("Help contents") },
    { "Ctrl+R",      N_("Start the emulator") },
    { "Ctrl+.",      N_("Stop the emulator") },
    { "Ctrl+Shift+R", N_("Restart the emulator") },
    { "Ctrl+Q",      N_("Quit the launcher") },
};

void help_show_shortcuts(GtkWindow *parent) {
    if (shortcuts_win) {
        gtk_window_present(GTK_WINDOW(shortcuts_win));
        return;
    }
    GtkWidget *box = NULL;
    GtkWidget *win = help_window(parent, _("Keyboard shortcuts"), 420, 260, &box);
    shortcuts_win = win;
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_widget_destroyed), &shortcuts_win);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
    for (guint i = 0; i < G_N_ELEMENTS(k_shortcuts); i++) {
        GtkWidget *k = gtk_label_new(NULL);
        GtkWidget *w = gtk_label_new(_(k_shortcuts[i].what));
        gchar *markup = g_markup_printf_escaped("<tt>%s</tt>", k_shortcuts[i].keys);
        gtk_label_set_markup(GTK_LABEL(k), markup);
        g_free(markup);
        gtk_label_set_xalign(GTK_LABEL(k), 0.0f);
        gtk_label_set_xalign(GTK_LABEL(w), 0.0f);
        gtk_grid_attach(GTK_GRID(grid), k, 0, (gint)i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), w, 1, (gint)i, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(box), grid, TRUE, TRUE, 0);
    help_close_button(win, box);
    gtk_widget_show_all(win);
}

void help_show_about(GtkWindow *parent) {
    const char *title = _("ESP32 Simulator (Unified)\nGTK3 front-end for esp-emu: profile/config, embedded canvas (sim_host/SDL) and SD card.");
    gchar *details = g_strjoin("\n",
        _("Project: ESP32 PC Control Deck"),
        _("Emulator: esp-emu 1.1.0 (external binary)"),
        _("License: GPL-3.0 (see LICENSE in the repository root)"),
        NULL);
    GtkWidget *d = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", title);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", details);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    g_free(details);
}
