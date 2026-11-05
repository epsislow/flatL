#include "flat_native_use.h"
#include "flat_native.h"
#include "flat_native_util.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *const KNOWN_OPTIONAL_MODULES[] = {
    FLAT_NATIVE_MODULE_MATH_TRIG,
    FLAT_NATIVE_MODULE_MATH_STATS,
    FLAT_NATIVE_MODULE_MATH,
    FLAT_NATIVE_MODULE_LIST,
    FLAT_NATIVE_MODULE_OBJ,
    FLAT_NATIVE_MODULE_VCX,
    NULL,
};

static int known_optional_module(const char *mod) {
    int i;
    for (i = 0; KNOWN_OPTIONAL_MODULES[i]; i++) {
        if (strcmp(mod, KNOWN_OPTIONAL_MODULES[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    {
        char *e = s + strlen(s) - 1;
        while (e > s && isspace((unsigned char)*e)) {
            *e-- = '\0';
        }
    }
    return s;
}

static int starts_with_use_native(const char *s) {
    return strncmp(s, "use", 3) == 0 && isspace((unsigned char)s[3]);
}

static int validate_alias(const char *alias, char *err, size_t err_size) {
    size_t i;

    if (!alias || !alias[0]) {
        return 0;
    }
    if (flat_native_is_module_id(alias)) {
        snprintf(err, err_size, "native alias \"%s\" is a reserved module name", alias);
        return -1;
    }
    if (!isalpha((unsigned char)alias[0])) {
        snprintf(err, err_size, "native alias must start with a letter");
        return -1;
    }
    for (i = 1; alias[i]; i++) {
        unsigned char c = (unsigned char)alias[i];
        if (isalnum(c) || c == '_' || c == '-' || c == ':') {
            continue;
        }
        snprintf(err, err_size, "invalid character in native alias");
        return -1;
    }
    return 0;
}

static int parse_use_native_line(const char *line, FlatOptionalNativeDecl *out, char *err,
                                 size_t err_size) {
    char buf[FGIR_MAX_LINE];
    char *p;
    char *with_tok;
    char mod[FGIR_NATIVE_MODULE_LEN];
    int is_core;

    snprintf(buf, sizeof buf, "%s", line);
    p = trim(buf);
    if (strncmp(p, "use", 3) != 0) {
        return -1;
    }
    p += 3;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (strncmp(p, "native", 6) != 0 || (p[6] != '\0' && !isspace((unsigned char)p[6]))) {
        return -1;
    }
    p += 6;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    {
        char *mod_start = p;
        while (*p && !isspace((unsigned char)*p)) {
            p++;
        }
        if (p == mod_start) {
            snprintf(err, err_size, "use native: missing module name");
            return -1;
        }
        snprintf(mod, sizeof mod, "%.*s", (int)(p - mod_start), mod_start);
    }
    is_core = flat_native_is_core_module(mod);
    if (is_core) {
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            snprintf(err, err_size, "use native: %s is a core module (no declaration needed)", mod);
            return -1;
        }
    } else if (!known_optional_module(mod)) {
        snprintf(err, err_size, "use native: unknown module \"%s\"", mod);
        return -1;
    }
    out->alias[0] = '\0';
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '\0') {
        snprintf(out->module, sizeof out->module, "%s", mod);
        return 0;
    }
    if (strncmp(p, "with", 4) != 0 || (p[4] != '\0' && !isspace((unsigned char)p[4]))) {
        snprintf(err, err_size, "use native: expected end or \"with alias\"");
        return -1;
    }
    p += 4;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    with_tok = p;
    while (*p && !isspace((unsigned char)*p)) {
        p++;
    }
    if (with_tok == p) {
        snprintf(err, err_size, "use native: missing alias after with");
        return -1;
    }
    snprintf(out->alias, sizeof out->alias, "%.*s", (int)(p - with_tok), with_tok);
    if (validate_alias(out->alias, err, err_size) != 0) {
        return -1;
    }
    snprintf(out->module, sizeof out->module, "%s", mod);
    return 0;
}

static const char *func_name_for_id(uint16_t id) {
    const char *name = flat_native_name(id);
    const char *dot = strrchr(name, '.');
    return dot ? dot + 1 : name;
}

static int check_alias_func_collisions(const FlatOptionalNativeDecl *decls, int decl_count,
                                       char *err, size_t err_size) {
    int a, b;
    size_t i, j;

    for (a = 0; a < decl_count; a++) {
        if (!decls[a].alias[0]) {
            continue;
        }
        for (b = a + 1; b < decl_count; b++) {
            if (strcmp(decls[a].alias, decls[b].alias) != 0) {
                continue;
            }
            for (i = 0; i < (size_t)FLAT_NATIVE_COUNT; i++) {
                char ma[FGIR_NATIVE_MODULE_LEN];
                const char *fa;

                if (flat_native_module_for_id((uint16_t)i, ma, sizeof ma) != 0) {
                    continue;
                }
                if (strcmp(ma, decls[a].module) != 0) {
                    continue;
                }
                fa = func_name_for_id((uint16_t)i);
                for (j = 0; j < (size_t)FLAT_NATIVE_COUNT; j++) {
                    char mb2[FGIR_NATIVE_MODULE_LEN];
                    if (flat_native_module_for_id((uint16_t)j, mb2, sizeof mb2) != 0) {
                        continue;
                    }
                    if (strcmp(mb2, decls[b].module) != 0) {
                        continue;
                    }
                    if (strcmp(func_name_for_id((uint16_t)j), fa) == 0) {
                        snprintf(err, err_size, "%s.%s already exists (%s, %s)", decls[a].alias, fa,
                                 decls[a].module, decls[b].module);
                        return -1;
                    }
                }
            }
        }
    }
    return 0;
}

static int alias_for_module(const FlatOptionalNativeDecl *decls, int decl_count, const char *mod,
                            char *alias_out, size_t alias_sz) {
    int i;

    alias_out[0] = '\0';
    for (i = 0; i < decl_count; i++) {
        if (strcmp(decls[i].module, mod) == 0 && decls[i].alias[0]) {
            snprintf(alias_out, alias_sz, "%s", decls[i].alias);
            return 1;
        }
    }
    return 0;
}

static int optional_declared(const FlatOptionalNativeDecl *decls, int decl_count, const char *mod) {
    int i;
    for (i = 0; i < decl_count; i++) {
        if (strcmp(decls[i].module, mod) == 0) {
            return 1;
        }
    }
    return 0;
}

int flat_native_use_scan_file(const char *path, FlatOptionalNativeDecl *decls, int decl_max,
                              int *decl_count_out, char *err, size_t err_size) {
    FILE *f;
    char line[FGIR_MAX_LINE + 8];
    int line_no = 0;
    int in_block = 0;
    int in_header = 1;
    int seen_count = 0;

    if (!decl_count_out) {
        return -1;
    }
    *decl_count_out = 0;
    f = fopen(path, "r");
    if (!f) {
        snprintf(err, err_size, "cannot open %s", path);
        return -1;
    }
    while (fgets(line, sizeof line, f)) {
        char *p;
        line_no++;
        p = trim(line);
        if (in_block) {
            if (strncmp(p, "-->", 3) == 0) {
                in_block = 0;
            }
            continue;
        }
        if (strncmp(p, "--<", 3) == 0) {
            in_block = 1;
            continue;
        }
        if (p[0] == '\0' || (p[0] == '-' && p[1] == '-')) {
            continue;
        }
        if (in_header && starts_with_use_native(p) && strstr(p, "native") != NULL) {
            FlatOptionalNativeDecl decl;
            int j;

            memset(&decl, 0, sizeof decl);
            if (parse_use_native_line(p, &decl, err, err_size) != 0) {
                char sub[256];
                snprintf(sub, sizeof sub, "%s", err);
                snprintf(err, err_size, "%s:%d: %s", path, line_no, sub);
                fclose(f);
                return -1;
            }
            for (j = 0; j < seen_count; j++) {
                if (strcmp(decls[j].module, decl.module) == 0) {
                    snprintf(err, err_size, "%s:%d: duplicate use native for \"%s\"", path,
                             line_no, decl.module);
                    fclose(f);
                    return -1;
                }
            }
            if (seen_count >= decl_max) {
                snprintf(err, err_size, "%s:%d: too many use native directives", path, line_no);
                fclose(f);
                return -1;
            }
            decls[seen_count++] = decl;
            continue;
        }
        if (*p == '#') {
            in_header = 0;
            break;
        }
        if (in_header) {
            continue;
        }
    }
    fclose(f);
    if (check_alias_func_collisions(decls, seen_count, err, err_size) != 0) {
        char sub[256];
        snprintf(sub, sizeof sub, "%s", err);
        snprintf(err, err_size, "%s: %s", path, sub);
        return -1;
    }
    *decl_count_out = seen_count;
    return 0;
}

int flat_native_use_add_unit(FgirProgram *prog, const FlatOptionalNativeDecl *decls, int decl_count,
                             int *unit_index_out, char *err, size_t err_size) {
    FgirNativeUnit *u;
    int ui;

    if (!prog || !unit_index_out) {
        return -1;
    }
    if (prog->native_unit_count >= FGIR_MAX_NATIVE_UNITS) {
        snprintf(err, err_size, "too many linked native units");
        return -1;
    }
    if (decl_count > FGIR_MAX_OPTIONAL_NATIVES) {
        snprintf(err, err_size, "too many use native directives in one file");
        return -1;
    }
    ui = prog->native_unit_count++;
    u = &prog->native_units[ui];
    u->decl_count = decl_count;
    if (decl_count > 0 && decls) {
        memcpy(u->decls, decls, (size_t)decl_count * sizeof(FlatOptionalNativeDecl));
    }
    *unit_index_out = ui;
    return 0;
}

int flat_native_use_build_fnat_union(FgirProgram *prog) {
    int ui, di, j;
    int n = 0;

    if (!prog) {
        return -1;
    }
    prog->optional_native_count = 0;
    for (ui = 0; ui < prog->native_unit_count; ui++) {
        for (di = 0; di < prog->native_units[ui].decl_count; di++) {
            const char *mod = prog->native_units[ui].decls[di].module;
            if (flat_native_is_core_module(mod)) {
                continue;
            }
            for (j = 0; j < n; j++) {
                if (strcmp(prog->optional_natives[j].module, mod) == 0) {
                    break;
                }
            }
            if (j < n) {
                continue;
            }
            if (n >= FGIR_MAX_OPTIONAL_NATIVES) {
                return -1;
            }
            snprintf(prog->optional_natives[n].module, sizeof prog->optional_natives[n].module,
                     "%s", mod);
            prog->optional_natives[n].alias[0] = '\0';
            n++;
        }
    }
    prog->optional_native_count = n;
    return 0;
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

int flat_native_resolve_module_name(const char *mod_in, const FlatOptionalNativeDecl *decls,
                                    int decl_count, char *mod_out, size_t mod_out_sz) {
    return resolve_module_from_decls(mod_in, decls, decl_count, mod_out, mod_out_sz);
}

static int canonical_forbidden(const char *prefix, const FlatOptionalNativeDecl *decls,
                               int decl_count, char *err, size_t err_size) {
    char alias[FGIR_NATIVE_ALIAS_LEN];
    char resolved[FGIR_NATIVE_MODULE_LEN];

    if (flat_native_is_core_module(prefix)) {
        return 0;
    }
    if (!known_optional_module(prefix)) {
        return 0;
    }
    if (!optional_declared(decls, decl_count, prefix)) {
        return 0;
    }
    if (alias_for_module(decls, decl_count, prefix, alias, sizeof alias)) {
        snprintf(err, err_size, "use alias %s instead of %s", alias, prefix);
        return -1;
    }
    resolve_module_from_decls(prefix, decls, decl_count, resolved, sizeof resolved);
    (void)resolved;
    return 0;
}

int flat_native_split_call_decls(const char *line, const FlatOptionalNativeDecl *decls,
                                 int decl_count, uint16_t *out_id, char *inner, size_t inner_max,
                                 char *err, size_t err_size) {
    char mod[64];
    char func[32];
    char resolved[FGIR_NATIVE_MODULE_LEN];

    if (flat_native_parse_dotted_call(line, mod, sizeof mod, func, sizeof func, inner, inner_max) !=
        0) {
        return -1;
    }
    if (canonical_forbidden(mod, decls, decl_count, err, err_size) != 0) {
        return -1;
    }
    if (flat_native_resolve_module_name(mod, decls, decl_count, resolved, sizeof resolved) != 0) {
        return -1;
    }
    if (flat_native_lookup(resolved, func, out_id) != 0) {
        return -1;
    }
    if (*out_id >= (uint16_t)FLAT_NATIVE_CORE_COUNT && !flat_native_is_core_module(resolved) &&
        !optional_declared(decls, decl_count, resolved)) {
        if (err && err_size > 0) {
            snprintf(err, err_size, "undeclared optional native %s.%s (add use native %s)",
                     resolved, func, resolved);
        }
        return -1;
    }
    return 0;
}

int flat_native_split_call_prog(const char *line, const FgirProgram *prog, int native_unit_index,
                                uint16_t *out_id, char *inner, size_t inner_max, char *err,
                                size_t err_size) {
    const FlatOptionalNativeDecl *decls = NULL;
    int decl_count = 0;

    if (!prog) {
        return -1;
    }
    if (native_unit_index >= 0 && native_unit_index < prog->native_unit_count) {
        decls = prog->native_units[native_unit_index].decls;
        decl_count = prog->native_units[native_unit_index].decl_count;
    }
    return flat_native_split_call_decls(line, decls, decl_count, out_id, inner, inner_max, err,
                                        err_size);
}

static int validate_optional_native_call(const FlatOptionalNativeDecl *decls, int decl_count,
                                         const char *call_line, char *err, size_t err_size) {
    char mod[64];
    char func[32];
    char inner[64];
    char resolved[FGIR_NATIVE_MODULE_LEN];
    uint16_t id;

    if (flat_native_parse_dotted_call(call_line, mod, sizeof mod, func, sizeof func, inner,
                                      sizeof inner) != 0) {
        return 0;
    }
    if (canonical_forbidden(mod, decls, decl_count, err, err_size) != 0) {
        return -1;
    }
    flat_native_resolve_module_name(mod, decls, decl_count, resolved, sizeof resolved);
    if (flat_native_is_core_module(resolved)) {
        return 0;
    }
    if (!optional_declared(decls, decl_count, resolved)) {
        snprintf(err, err_size, "undeclared optional native %s.%s (add use native %s)", resolved,
                 func, resolved);
        return -1;
    }
    if (flat_native_lookup(resolved, func, &id) != 0) {
        snprintf(err, err_size, "unknown native %s.%s", resolved, func);
        return -1;
    }
    return 0;
}

int flat_native_use_validate_program(const FgirProgram *prog, char *err, size_t err_size) {
    int bi, ii;

    for (bi = 0; bi < prog->block_count; bi++) {
        const FgirBlock *b = &prog->blocks[bi];
        const FlatOptionalNativeDecl *decls = NULL;
        int decl_count = 0;
        int ui = b->native_unit_index;

        if (ui >= 0 && ui < prog->native_unit_count) {
            decls = prog->native_units[ui].decls;
            decl_count = prog->native_units[ui].decl_count;
        }
        for (ii = 0; ii < b->instr_count; ii++) {
            const FgirInstr *ins = &b->instr[ii];
            if (ins->kind == FGIR_IO) {
                if (validate_optional_native_call(decls, decl_count, ins->text, err, err_size) !=
                    0) {
                    return -1;
                }
            } else if (ins->kind == FGIR_ASSIGN) {
                char buf[FGIR_MAX_LINE];
                char *eq;
                char *rhs;

                snprintf(buf, sizeof buf, "%s", ins->text);
                eq = strchr(buf, '=');
                if (!eq) {
                    continue;
                }
                *eq = '\0';
                rhs = eq + 1;
                while (*rhs && (*rhs == ' ' || *rhs == '\t')) {
                    rhs++;
                }
                if (validate_optional_native_call(decls, decl_count, rhs, err, err_size) != 0) {
                    return -1;
                }
            }
        }
    }
    return 0;
}
