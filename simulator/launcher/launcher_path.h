#ifndef SIM_LAUNCHER_PATH_H
#define SIM_LAUNCHER_PATH_H

#include <stdbool.h>

/* Shared path helper for launcher modules (profile.c, validate.c). */
static inline bool launcher_is_abs_path(const char *p) {
    return p && p[0] == '/';
}

#endif
