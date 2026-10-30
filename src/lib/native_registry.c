#include "flat_native.h"
#include "flat_native_util.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *mod;
    const char *func;
    const char *name;
} NativeMeta;

#define META(m, f) { (m), (f), m "." f }

static const NativeMeta NATIVES[FLAT_NATIVE_COUNT] = {
    META("io", "outln"),
    META("io", "out"),
    META("io", "outvar"),
    META("io", "in"),
    META("io", "inln"),
    META("io", "in_num"),
    META("io", "in_bool"),
    META("io", "in_list"),
    META("io", "in_object"),
    META("io", "in_bools"),
    META("str", "to_num"),
    META("str", "to_bool"),
    META("json", "parse"),
    META("json", "parse_list"),
    META("json", "parse_object"),
    META("json", "parse_bools"),
    META("json", "stringify"),
    META("math.trig", "sin"),
    META("math.trig", "cos"),
    META("math.trig", "tan"),
    META("math.trig", "sqrt"),
};

int flat_native_is_core_module(const char *mod) {
    if (!mod) {
        return 0;
    }
    return strcmp(mod, "io") == 0 || strcmp(mod, "json") == 0 || strcmp(mod, "str") == 0;
}

int flat_native_lookup(const char *mod, const char *func, uint16_t *out_id) {
    size_t i;
    if (!mod || !func || !out_id) {
        return -1;
    }
    for (i = 0; i < FLAT_NATIVE_COUNT; i++) {
        if (strcmp(NATIVES[i].mod, mod) == 0 && strcmp(NATIVES[i].func, func) == 0) {
            *out_id = (uint16_t)i;
            return 0;
        }
    }
    return -1;
}

const char *flat_native_name(uint16_t id) {
    if (id >= FLAT_NATIVE_COUNT) {
        return "?";
    }
    return NATIVES[id].name;
}

int flat_native_module_for_id(uint16_t id, char *mod_out, size_t mod_out_sz) {
    if (id >= FLAT_NATIVE_COUNT || !mod_out || mod_out_sz == 0) {
        return -1;
    }
    snprintf(mod_out, mod_out_sz, "%s", NATIVES[id].mod);
    return 0;
}

int flat_native_is_optional_id(uint16_t id) {
    return id >= (uint16_t)FLAT_NATIVE_CORE_COUNT && id < (uint16_t)FLAT_NATIVE_COUNT;
}

int flat_native_split_call(const char *line, uint16_t *out_id, char *inner, size_t inner_max) {
    char mod[16];
    char func[32];

    if (!line || !out_id || !inner) {
        return -1;
    }
    if (flat_native_parse_dotted_call(line, mod, sizeof mod, func, sizeof func, inner, inner_max) !=
        0) {
        return -1;
    }
    return flat_native_lookup(mod, func, out_id);
}

int flat_native_is_call_line(const char *line) {
    char mod[64];
    char func[32];
    char inner[64];
    const char *lp;
    const char *p;

    if (!line) {
        return 0;
    }
    lp = strchr(line, '(');
    if (lp) {
        for (p = line; p < lp; p++) {
            if (*p == '=') {
                return 0;
            }
        }
    }
    if (flat_native_parse_dotted_call(line, mod, sizeof mod, func, sizeof func, inner,
                                      sizeof inner) != 0) {
        return 0;
    }
    {
        uint16_t id;
        if (flat_native_lookup(mod, func, &id) == 0) {
            return 1;
        }
    }
    if (!flat_native_is_core_module(mod)) {
        return 1;
    }
    return 0;
}
