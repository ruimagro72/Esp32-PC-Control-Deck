#ifndef LAUNCHER_THEME_H
#define LAUNCHER_THEME_H

/* Dark theme of the launcher ("design studio" look): one GtkCssProvider built from an embedded CSS string,
 * applied to the default screen. Call once after gtk_init(). Widgets opt in through style classes
 * (primary, rail-item, badge-*, card, ...). */
void theme_apply(void);

#endif
