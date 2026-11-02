#include "flat_native.h"
#include "flat_native_util.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *mod;
    const char *func;
    const char *name;
    const char *sig;
} NativeMeta;

#define META(m, f) \
    { (m), (f), m "." f, NULL }
#define META_SIG(m, f, s) \
    { (m), (f), m "." f, (s) }

static const NativeMeta NATIVES[FLAT_NATIVE_COUNT] = {
    META_SIG("io", "outln", "io.outln(expr)"),
    META_SIG("io", "out", "io.out(expr)"),
    META_SIG("io", "outvar", "io.outvar(name)"),
    META_SIG("io", "in", "io.in(prompt?)"),
    META_SIG("io", "inln", "io.inln(prompt?)"),
    META_SIG("io", "in_num", "io.in_num(prompt?)"),
    META_SIG("io", "in_bool", "io.in_bool(prompt?)"),
    META_SIG("io", "in_list", "io.in_list(prompt?)"),
    META_SIG("io", "in_object", "io.in_object(prompt?)"),
    META_SIG("io", "in_bools", "io.in_bools(prompt?)"),
    META_SIG("str", "to_num", "str.to_num(s)"),
    META_SIG("str", "to_bool", "str.to_bool(s)"),
    META_SIG("str", "len", "str.len(s)"),
    META_SIG("str", "repeat", "str.repeat(s, n)"),
    META_SIG("str", "concat", "str.concat(s1, s2, ...)"),
    META_SIG("str", "slice", "str.slice(s, start, end)"),
    META_SIG("str", "index_of", "str.index_of(haystack, needle)"),
    META_SIG("str", "contains", "str.contains(haystack, needle)"),
    META_SIG("str", "starts_with", "str.starts_with(s, prefix)"),
    META_SIG("str", "ends_with", "str.ends_with(s, suffix)"),
    META_SIG("str", "trim", "str.trim(s)"),
    META_SIG("str", "split", "str.split(s, sep)"),
    META_SIG("str", "join", "str.join(sep, list)"),
    META_SIG("str", "lower", "str.lower(s)"),
    META_SIG("str", "upper", "str.upper(s)"),
    META_SIG("str", "replace", "str.replace(s, old, new)"),
    META_SIG("str", "replace_all", "str.replace_all(s, old, new)"),
    META_SIG("json", "parse", "json.parse(s)"),
    META_SIG("json", "parse_list", "json.parse_list(s)"),
    META_SIG("json", "parse_object", "json.parse_object(s)"),
    META_SIG("json", "parse_bools", "json.parse_bools(s)"),
    META_SIG("json", "stringify", "json.stringify(v)"),
    META_SIG("math.trig", "sin", "math.trig.sin(x)"),
    META_SIG("math.trig", "cos", "math.trig.cos(x)"),
    META_SIG("math.trig", "tan", "math.trig.tan(x)"),
    META_SIG("math.trig", "num_pi", "math.trig.num_pi()"),
    META_SIG("math.trig", "num_tau", "math.trig.num_tau()"),
    META_SIG("math.trig", "deg_to_rad", "math.trig.deg_to_rad(deg)"),
    META_SIG("math.trig", "rad_to_deg", "math.trig.rad_to_deg(rad)"),
    META_SIG("math.trig", "sin_deg", "math.trig.sin_deg(deg)"),
    META_SIG("math.trig", "cos_deg", "math.trig.cos_deg(deg)"),
    META_SIG("math.trig", "tan_deg", "math.trig.tan_deg(deg)"),
    META_SIG("math.stats", "mean", "math.stats.mean(xs)"),
    META_SIG("math.stats", "median", "math.stats.median(xs)"),
    META_SIG("math.stats", "var", "math.stats.var(xs, n)"),
    META_SIG("math.stats", "stdev", "math.stats.stdev(xs, n)"),
    META_SIG("math", "pow", "math.pow(x, y)"),
    META_SIG("math", "sqrt", "math.sqrt(x)"),
    META_SIG("math", "exp", "math.exp(x)"),
    META_SIG("math", "log", "math.log(x)"),
    META_SIG("math", "log10", "math.log10(x)"),
    META_SIG("math", "floor", "math.floor(x)"),
    META_SIG("math", "ceil", "math.ceil(x)"),
    META_SIG("math", "round", "math.round(x)"),
    META_SIG("math", "mod", "math.mod(x, y)"),
    META_SIG("math", "floor_mod", "math.floor_mod(a, b)"),
    META_SIG("math", "sum", "math.sum(xs)"),
    META_SIG("math", "min", "math.min(xs)"),
    META_SIG("math", "max", "math.max(xs)"),
    META_SIG("math", "sum2", "math.sum2(a, b)"),
    META_SIG("math", "min2", "math.min2(a, b)"),
    META_SIG("math", "max2", "math.max2(a, b)"),
    META_SIG("math", "num_pi", "math.num_pi()"),
    META_SIG("math", "num_tau", "math.num_tau()"),
    META_SIG("math", "num_e", "math.num_e()"),
    META_SIG("list", "empty", "list.empty()"),
    META_SIG("list", "pack", "list.pack(v1, v2, ...)"),
    META_SIG("list", "push", "list.push(list, val)"),
    META_SIG("list", "get", "list.get(list, index)"),
    META_SIG("list", "len", "list.len(list)"),
    META_SIG("list", "prepend", "list.prepend(list, val)"),
    META_SIG("list", "insert", "list.insert(list, index, val)"),
    META_SIG("list", "set", "list.set(list, index, val)"),
    META_SIG("list", "index_of", "list.index_of(list, val)"),
    META_SIG("list", "contains", "list.contains(list, val)"),
    META_SIG("list", "remove_at", "list.remove_at(list, index)"),
    META_SIG("list", "slice", "list.slice(list, start, end)"),
    META_SIG("list", "splice", "list.splice(list, start, count)"),
    META_SIG("list", "range", "list.range(start, stop, step?)"),
    META_SIG("list", "repeat", "list.repeat(n, value)"),
    META_SIG("obj", "empty", "obj.empty()"),
    META_SIG("obj", "get", "obj.get(obj, key)"),
    META_SIG("obj", "set", "obj.set(obj, key, val)"),
    META_SIG("obj", "has", "obj.has(obj, key)"),
    META_SIG("obj", "len", "obj.len(obj)"),
    META_SIG("obj", "remove", "obj.remove(obj, key)"),
    META_SIG("obj", "keys", "obj.keys(obj)"),
    META_SIG("obj", "values", "obj.values(obj)"),
    META_SIG("mem", "vars_max", "mem.vars_max()"),
    META_SIG("mem", "vars_used", "mem.vars_used()"),
    META_SIG("mem", "vars_avail", "mem.vars_avail()"),
    META_SIG("mem", "list_max", "mem.list_max()"),
    META_SIG("mem", "list_used", "mem.list_used()"),
    META_SIG("mem", "list_avail", "mem.list_avail()"),
    META_SIG("mem", "object_max", "mem.object_max()"),
    META_SIG("mem", "object_used", "mem.object_used()"),
    META_SIG("mem", "object_avail", "mem.object_avail()"),
    META_SIG("mem", "strref_max", "mem.strref_max()"),
    META_SIG("mem", "strref_used", "mem.strref_used()"),
    META_SIG("mem", "strref_avail", "mem.strref_avail()"),
    META_SIG("mem", "str_used", "mem.str_used()"),
    META_SIG("mem", "bool_used", "mem.bool_used()"),
    META_SIG("mem", "num_used", "mem.num_used()"),
    META_SIG("mem", "null_used", "mem.null_used()"),
    META_SIG("io", "docs", "io.docs(query)"),
    META_SIG("io", "ctxin", "io.ctxin(source)"),
    META_SIG("io", "ctxout", "io.ctxout(destination)"),
    META_SIG("io", "ctxerr", "io.ctxerr(destination)"),
    META_SIG("io", "err", "io.err(expr)"),
    META_SIG("io", "errln", "io.errln(expr)"),
    META_SIG("mem", "ref_empty", "mem.ref_empty()"),
    META_SIG("mem", "ref_from", "mem.ref_from(s)"),
    META_SIG("file", "readCtx", "file.readCtx(path)"),
    META_SIG("file", "createCtx", "file.createCtx(path)"),
    META_SIG("file", "writeCtx", "file.writeCtx(path)"),
    META_SIG("file", "appendCtx", "file.appendCtx(path)"),
    META_SIG("file", "readWriteCtx", "file.readWriteCtx(path)"),
    META_SIG("file", "readln", "file.readln(fp)"),
    META_SIG("file", "read", "file.read(fp, n?)"),
    META_SIG("file", "write", "file.write(fp, data)"),
    META_SIG("file", "writeBytes", "file.writeBytes(fp, data)"),
    META_SIG("file", "flush", "file.flush(fp)"),
    META_SIG("file", "pos", "file.pos(fp)"),
    META_SIG("file", "size", "file.size(fp)"),
    META_SIG("file", "setpos", "file.setpos(fp, n)"),
    META_SIG("file", "iseof", "file.iseof(fp)"),
    META_SIG("file", "close", "file.close(fp)"),
};

int flat_native_is_core_module(const char *mod) {
    if (!mod) {
        return 0;
    }
    return strcmp(mod, "io") == 0 || strcmp(mod, "json") == 0 || strcmp(mod, "str") == 0 ||
           strcmp(mod, FLAT_NATIVE_MODULE_MEM) == 0 || strcmp(mod, FLAT_NATIVE_MODULE_FILE) == 0;
}

int flat_native_is_module_id(const char *mod) {
    static const char *const MODULE_IDS[] = {
        "io",
        "json",
        "str",
        FLAT_NATIVE_MODULE_MEM,
        FLAT_NATIVE_MODULE_FILE,
        FLAT_NATIVE_MODULE_MATH_TRIG,
        FLAT_NATIVE_MODULE_MATH_STATS,
        FLAT_NATIVE_MODULE_MATH,
        FLAT_NATIVE_MODULE_LIST,
        FLAT_NATIVE_MODULE_OBJ,
        NULL,
    };
    int i;

    if (!mod || !mod[0]) {
        return 0;
    }
    for (i = 0; MODULE_IDS[i]; i++) {
        if (strcmp(MODULE_IDS[i], mod) == 0) {
            return 1;
        }
    }
    return 0;
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

const char *flat_native_doc_sig(uint16_t id) {
    static char buf[160];

    if (id >= FLAT_NATIVE_COUNT) {
        return "?";
    }
    if (NATIVES[id].sig) {
        return NATIVES[id].sig;
    }
    snprintf(buf, sizeof buf, "%s()", NATIVES[id].name);
    return buf;
}

int flat_native_module_for_id(uint16_t id, char *mod_out, size_t mod_out_sz) {
    if (id >= FLAT_NATIVE_COUNT || !mod_out || mod_out_sz == 0) {
        return -1;
    }
    snprintf(mod_out, mod_out_sz, "%s", NATIVES[id].mod);
    return 0;
}

int flat_native_is_optional_id(uint16_t id) {
    if (id == (uint16_t)FLAT_NATIVE_IO_DOCS) {
        return 0;
    }
    if (id >= (uint16_t)FLAT_NATIVE_IO_CTXIN && id <= (uint16_t)FLAT_NATIVE_IO_ERRLN) {
        return 0;
    }
    return id >= (uint16_t)FLAT_NATIVE_CORE_COUNT && id < (uint16_t)FLAT_NATIVE_COUNT;
}

static int resolve_module_from_decls(const char *mod_in, const FlatOptionalNativeDecl *decls,
                                     int decl_count, char *mod_out, size_t mod_out_sz) {
    int i;
    if (!mod_in || !mod_out || mod_out_sz == 0) {
        return -1;
    }
    for (i = 0; i < decl_count; i++) {
        if (decls[i].alias[0] && strcmp(decls[i].alias, mod_in) == 0) {
            snprintf(mod_out, mod_out_sz, "%s", decls[i].module);
            return 0;
        }
    }
    snprintf(mod_out, mod_out_sz, "%s", mod_in);
    return 0;
}

int flat_native_split_call_manifest(const char *line, const FlatOptionalNativeDecl *decls,
                                    int decl_count, uint16_t *out_id, char *inner,
                                    size_t inner_max) {
    char mod[64];
    char func[32];
    char resolved[FGIR_NATIVE_MODULE_LEN];

    if (!line || !out_id || !inner) {
        return -1;
    }
    if (flat_native_parse_dotted_call(line, mod, sizeof mod, func, sizeof func, inner, inner_max) !=
        0) {
        return -1;
    }
    if (resolve_module_from_decls(mod, decls, decl_count, resolved, sizeof resolved) != 0) {
        return -1;
    }
    if (flat_native_lookup(resolved, func, out_id) != 0) {
        return -1;
    }
    if (*out_id >= (uint16_t)FLAT_NATIVE_CORE_COUNT) {
        int i;
        int ok = 0;
        for (i = 0; i < decl_count; i++) {
            if (strcmp(decls[i].module, resolved) == 0) {
                ok = 1;
                break;
            }
        }
        if (!ok) {
            return -1;
        }
    }
    return 0;
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
