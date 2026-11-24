#include "flat_native_mods.h"
#include "flat_native.h"

#include <string.h>

typedef struct {
    const char *id;
    int built;
} FlatModEntry;

static const FlatModEntry BUILT_MODS[] = {
    {"io", 1},
    {"json", 1},
    {"str", 1},
    {FLAT_NATIVE_MODULE_DATE, 1},
    {FLAT_NATIVE_MODULE_CLOCK, 1},
    {FLAT_NATIVE_MODULE_MEM, 1},
    {FLAT_NATIVE_MODULE_FILE, 1},
#if FLAT_WITH_HTTP
    {FLAT_NATIVE_MODULE_HTTP, 1},
#endif
#if FLAT_WITH_DOM
    {FLAT_NATIVE_MODULE_DOM, 1},
#endif
#if FLAT_WITH_ENV
    {FLAT_NATIVE_MODULE_ENV, 1},
#endif
#if FLAT_WITH_PATH
    {FLAT_NATIVE_MODULE_PATH, 1},
#endif
#if FLAT_WITH_FS
    {FLAT_NATIVE_MODULE_FS, 1},
#endif
#if FLAT_WITH_ENC
    {FLAT_NATIVE_MODULE_ENC, 1},
#endif
#if FLAT_WITH_RE
    {FLAT_NATIVE_MODULE_RE, 1},
#endif
#if FLAT_WITH_HASH
    {FLAT_NATIVE_MODULE_HASH, 1},
#endif
    {FLAT_NATIVE_MODULE_MATH, 1},
    {FLAT_NATIVE_MODULE_MATH_TRIG, 1},
    {FLAT_NATIVE_MODULE_MATH_STATS, 1},
    {FLAT_NATIVE_MODULE_MATH_MDIM, 1},
    {FLAT_NATIVE_MODULE_LIST, 1},
    {FLAT_NATIVE_MODULE_OBJ, 1},
    {FLAT_NATIVE_MODULE_VCX, 1},
    {FLAT_NATIVE_MODULE_EXECUTOR, 1},
    {FLAT_NATIVE_MODULE_CSV, 1},
};

int flat_native_mod_is_built(const char *mod) {
    size_t i;
    if (!mod || !mod[0]) {
        return 0;
    }
    for (i = 0; i < sizeof BUILT_MODS / sizeof BUILT_MODS[0]; i++) {
        if (BUILT_MODS[i].built && strcmp(BUILT_MODS[i].id, mod) == 0) {
            return 1;
        }
    }
    return 0;
}

int flat_native_mods_print(FILE *out) {
    size_t i;
    int n = 0;
    if (!out) {
        return 0;
    }
    for (i = 0; i < sizeof BUILT_MODS / sizeof BUILT_MODS[0]; i++) {
        if (!BUILT_MODS[i].built) {
            continue;
        }
        fprintf(out, "%s\n", BUILT_MODS[i].id);
        n++;
    }
    return n;
}

int flat_native_mods_format(char *buf, size_t buf_sz) {
    size_t i;
    size_t used = 0;
    int first = 1;
    if (!buf || buf_sz == 0) {
        return -1;
    }
    buf[0] = '\0';
    for (i = 0; i < sizeof BUILT_MODS / sizeof BUILT_MODS[0]; i++) {
        size_t len;
        if (!BUILT_MODS[i].built) {
            continue;
        }
        len = strlen(BUILT_MODS[i].id);
        if (!first) {
            if (used + 1 >= buf_sz) {
                return -1;
            }
            buf[used++] = ' ';
            buf[used] = '\0';
        }
        if (used + len >= buf_sz) {
            return -1;
        }
        memcpy(buf + used, BUILT_MODS[i].id, len + 1);
        used += len;
        first = 0;
    }
    return (int)used;
}
