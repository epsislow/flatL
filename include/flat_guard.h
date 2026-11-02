#ifndef FLAT_GUARD_H
#define FLAT_GUARD_H

#include <string.h>

#define FLAT_OR_HANDLER_PREFIX "#or."
#define FLAT_OR_HANDLER_PREFIX_LEN 4u

static inline int flat_label_is_or_handler(const char *lab) {
    if (!lab) {
        return 0;
    }
    return strncmp(lab, FLAT_OR_HANDLER_PREFIX, FLAT_OR_HANDLER_PREFIX_LEN) == 0;
}

static inline const char *flat_or_handler_body(const char *lab) {
    if (!lab || strncmp(lab, FLAT_OR_HANDLER_PREFIX, FLAT_OR_HANDLER_PREFIX_LEN) != 0) {
        return NULL;
    }
    return lab + FLAT_OR_HANDLER_PREFIX_LEN;
}

#endif
