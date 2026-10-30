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
    if (flat_native_is_core_module(mod)) {
        snprintf(err, err_size, "use native: %s is a core module (no declaration needed)", mod);
        return -1;
    }
    if (!known_optional_module(mod)) {
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

int flat_native_use_merge(FgirProgram *acc, const FlatOptionalNativeDecl *one, char *err,
                          size_t err_size) {
    int i;
    for (i = 0; i < acc->optional_native_count; i++) {
        if (strcmp(acc->optional_natives[i].module, one->module) == 0) {
            if (strcmp(acc->optional_natives[i].alias, one->alias) != 0) {
                snprintf(err, err_size, "module already imported as alias \"%s\"",
                         acc->optional_natives[i].alias[0] ? acc->optional_natives[i].alias
                                                           : "(canonical)");
                return -1;
            }
            return 0;
        }
    }
    if (acc->optional_native_count >= FGIR_MAX_OPTIONAL_NATIVES) {
        snprintf(err, err_size, "too many optional native modules");
        return -1;
    }
    acc->optional_natives[acc->optional_native_count] = *one;
    acc->optional_native_count++;
    return 0;
}

int flat_native_use_scan_file(const char *path, FgirProgram *acc, char *err, size_t err_size) {
    FILE *f;
    char line[FGIR_MAX_LINE + 8];
    int line_no = 0;
    int in_block = 0;
    int in_header = 1;
    FlatOptionalNativeDecl seen[FGIR_MAX_OPTIONAL_NATIVES];
    int seen_count = 0;

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
                if (strcmp(seen[j].module, decl.module) == 0) {
                    snprintf(err, err_size, "%s:%d: duplicate use native for \"%s\"", path,
                             line_no, decl.module);
                    fclose(f);
                    return -1;
                }
            }
            if (seen_count >= FGIR_MAX_OPTIONAL_NATIVES) {
                snprintf(err, err_size, "%s:%d: too many use native directives", path, line_no);
                fclose(f);
                return -1;
            }
            seen[seen_count++] = decl;
            if (flat_native_use_merge(acc, &decl, err, err_size) != 0) {
                char sub[256];
                snprintf(sub, sizeof sub, "%s", err);
                snprintf(err, err_size, "%s:%d: %s", path, line_no, sub);
                fclose(f);
                return -1;
            }
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

int flat_native_resolve_module_name(const char *mod_in, const FgirProgram *prog, char *mod_out,
                                    size_t mod_out_sz) {
    if (!prog) {
        return -1;
    }
    return resolve_module_from_decls(mod_in, prog->optional_natives, prog->optional_native_count,
                                     mod_out, mod_out_sz);
}

static int optional_declared(const FgirProgram *prog, const char *mod) {
    int i;
    for (i = 0; i < prog->optional_native_count; i++) {
        if (strcmp(prog->optional_natives[i].module, mod) == 0) {
            return 1;
        }
    }
    return 0;
}

int flat_native_split_call_prog(const char *line, const FgirProgram *prog, uint16_t *out_id,
                                char *inner, size_t inner_max) {
    char mod[64];
    char func[32];
    char resolved[FGIR_NATIVE_MODULE_LEN];

    if (flat_native_parse_dotted_call(line, mod, sizeof mod, func, sizeof func, inner, inner_max) !=
        0) {
        return -1;
    }
    if (flat_native_resolve_module_name(mod, prog, resolved, sizeof resolved) != 0) {
        return -1;
    }
    if (flat_native_lookup(resolved, func, out_id) != 0) {
        return -1;
    }
    if (*out_id >= (uint16_t)FLAT_NATIVE_CORE_COUNT && !optional_declared(prog, resolved)) {
        return -1;
    }
    return 0;
}

static int validate_optional_native_call(const FgirProgram *prog, const char *call_line, char *err,
                                         size_t err_size) {
    char mod[64];
    char func[32];
    char inner[64];
    char resolved[FGIR_NATIVE_MODULE_LEN];
    uint16_t id;

    if (flat_native_parse_dotted_call(call_line, mod, sizeof mod, func, sizeof func, inner,
                                      sizeof inner) != 0) {
        return 0;
    }
    flat_native_resolve_module_name(mod, prog, resolved, sizeof resolved);
    if (flat_native_is_core_module(resolved)) {
        return 0;
    }
    if (!optional_declared(prog, resolved)) {
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
        for (ii = 0; ii < b->instr_count; ii++) {
            const FgirInstr *ins = &b->instr[ii];
            if (ins->kind == FGIR_IO) {
                if (validate_optional_native_call(prog, ins->text, err, err_size) != 0) {
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
                if (validate_optional_native_call(prog, rhs, err, err_size) != 0) {
                    return -1;
                }
            }
        }
    }
    return 0;
}
