#include "flat_func.h"
#include "flat_func_tag.h"
#include "flat_native.h"
#include "flat_native_tag.h"
#include "flat_native_util.h"
#include "flat_schema.h"
#include "flat_slot_type.h"
#include "flat_value.h"
#include "flat_str.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int join_strings(char *out, size_t out_sz, const char *a, const char *mid, const char *b) {
    size_t la = strlen(a);
    size_t lm = strlen(mid);
    size_t lb = strlen(b);

    if (la + lm + lb + 1 > out_sz) {
        return -1;
    }
    memcpy(out, a, la);
    memcpy(out + la, mid, lm);
    memcpy(out + la + lm, b, lb + 1);
    return 0;
}

static int assign_param_in_line(char *line, size_t line_sz, int i, const char *rhs) {
    char lhs[32];
    int n;

    n = snprintf(lhs, sizeof lhs, "param_in_%d", i);
    if (n < 0 || (size_t)n >= sizeof lhs) {
        return -1;
    }
    return flat_str_assign_line(line, line_sz, lhs, rhs);
}

static int concat2(char *out, size_t out_sz, const char *a, const char *b) {
    return join_strings(out, out_sz, a, "", b);
}

static int is_ident_char(int c) {
    return isalnum((unsigned char)c) || c == '_';
}

static int is_simple_var_ref(const char *s) {
    size_t i;

    if (!s || !s[0]) {
        return 0;
    }
    if (!isalpha((unsigned char)s[0]) && s[0] != '_') {
        return 0;
    }
    for (i = 0; s[i]; i++) {
        if (!is_ident_char((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

static int starts_with_kw(const char *s, const char *kw) {
    size_t n = strlen(kw);
    return strncmp(s, kw, n) == 0 &&
           (s[n] == '\0' || isspace((unsigned char)s[n]) || s[n] == '(' || s[n] == ':');
}

static int parse_call_inner(const char *line, char *name_out, size_t name_sz, char *args_out,
                            size_t args_sz, char *ret_out, size_t ret_sz, int *arrow_form);

static int line_starts_with(const char *s, const char *pfx) {
    size_t n = strlen(pfx);
    return strncmp(s, pfx, n) == 0 && (s[n] == '\0' || isspace((unsigned char)s[n]));
}

int flat_func_meta_init(FgirProgram *prog) {
    if (!prog) {
        return -1;
    }
    snprintf(prog->default_executor, sizeof prog->default_executor, "%s",
             FLAT_FUNC_DEFAULT_EXECUTOR);
    prog->func_var_slots_manual = -1;
    prog->func_call_vm = 0;
    return 0;
}

static int parse_mod_line(const char *line, FgirProgram *prog, char *err, size_t err_size) {
    const char *p = strchr(line, '(');
    const char *q;
    size_t n;

    if (!p) {
        snprintf(err, err_size, "mod('id') expected (...)");
        return -1;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '\'') {
        snprintf(err, err_size, "mod('id') expected quoted id");
        return -1;
    }
    p++;
    q = strchr(p, '\'');
    if (!q || q == p) {
        snprintf(err, err_size, "mod: bad quoted id");
        return -1;
    }
    n = (size_t)(q - p);
    if (n >= sizeof prog->mod_id) {
        snprintf(err, err_size, "mod id too long");
        return -1;
    }
    memcpy(prog->mod_id, p, n);
    prog->mod_id[n] = '\0';
    prog->mod_id_set = 1;
    return 0;
}

static int add_export(FgirProgram *prog, const char *sym, char *err, size_t err_size) {
    int i;

    if (!sym[0]) {
        return 0;
    }
    for (i = 0; i < prog->export_count; i++) {
        if (strcmp(prog->exports[i], sym) == 0) {
            return 0;
        }
    }
    if (prog->export_count >= FGIR_MAX_FUNCTIONS) {
        snprintf(err, err_size, "too many export symbols");
        return -1;
    }
    snprintf(prog->exports[prog->export_count], FGIR_MAX_FUNC_NAME, "%s", sym);
    prog->export_count++;
    return 0;
}

static int parse_export_line(const char *line, FgirProgram *prog, char *err, size_t err_size) {
    const char *p = line + 6;
    char buf[FGIR_MAX_LINE];
    char *tok;
    char *ctx = NULL;

    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    snprintf(buf, sizeof buf, "%s", p);
    tok = strtok_r(buf, ",", &ctx);
    while (tok) {
        char *t = tok;
        while (*t && isspace((unsigned char)*t)) {
            t++;
        }
        {
            char *e = t + strlen(t);
            while (e > t && isspace((unsigned char)*(e - 1))) {
                e--;
            }
            *e = '\0';
        }
        if (*t && add_export(prog, t, err, err_size) != 0) {
            return -1;
        }
        tok = strtok_r(NULL, ",", &ctx);
    }
    return 0;
}

int flat_func_parse_header_line(const char *line, FgirProgram *prog, const char *path, int line_no,
                                char *err, size_t err_size) {
    char buf[FGIR_MAX_LINE];
    const char *p;

    (void)path;
    (void)line_no;
    snprintf(buf, sizeof buf, "%s", line);
    p = buf;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (line_starts_with(p, "func:executor")) {
        const char *lbl = p + 13;
        while (*lbl && isspace((unsigned char)*lbl)) {
            lbl++;
        }
        if (*lbl != '#') {
            snprintf(err, err_size, "func:executor requires block label");
            return -1;
        }
        snprintf(prog->default_executor, sizeof prog->default_executor, "%s", lbl);
        return 0;
    }
    if (line_starts_with(p, "func:varSlots")) {
        const char *n = p + 13;
        char *end = NULL;
        long v;

        while (*n && isspace((unsigned char)*n)) {
            n++;
        }
        v = strtol(n, &end, 10);
        if (end == n || (end && *end && !isspace((unsigned char)*end))) {
            snprintf(err, err_size, "func:varSlots requires a number");
            return -1;
        }
        if (v < 0) {
            snprintf(err, err_size, "func:varSlots must be >= 0");
            return -1;
        }
        prog->func_var_slots_manual = (v == 0) ? -1 : (int)v;
        return 0;
    }
    if (line_starts_with(p, "func:callVM")) {
        const char *n = p + 11;
        char *end = NULL;
        long v;

        while (*n && isspace((unsigned char)*n)) {
            n++;
        }
        v = strtol(n, &end, 10);
        if (end == n || (end && *end && !isspace((unsigned char)*end))) {
            snprintf(err, err_size, "func:callVM requires 0 or 1");
            return -1;
        }
        if (v != 0 && v != 1) {
            snprintf(err, err_size, "func:callVM must be 0 (desugar) or 1 (VM); see USER_FUNCTIONS.md");
            return -1;
        }
        prog->func_call_vm = (v != 0) ? 1 : 0;
        return 0;
    }
    if (strncmp(p, "mod(", 4) == 0) {
        return parse_mod_line(p, prog, err, err_size);
    }
    if (line_starts_with(p, "export")) {
        return parse_export_line(p, prog, err, err_size);
    }
    return 1; /* not a func header */
}

int flat_func_split_param_list(char *inner, char out[][64], int max_out, int *out_count) {
    int in_s = 0;
    int in_d = 0;
    int depth = 0;
    int count = 0;
    size_t start = 0;
    size_t i;
    size_t len = strlen(inner);

    *out_count = 0;
    for (i = 0; i <= len; i++) {
        char c = inner[i];
        if (c == '\'' && !in_d) {
            in_s = !in_s;
        } else if (c == '"' && !in_s) {
            in_d = !in_d;
        } else if (!in_s && !in_d) {
            if (c == '(') {
                depth++;
            } else if (c == ')' && depth > 0) {
                depth--;
            } else if ((c == ',' && depth == 0) || c == '\0') {
                size_t seg_len = i - start;
                char tmp[256];
                if (count >= max_out) {
                    return -1;
                }
                if (seg_len >= sizeof tmp) {
                    return -1;
                }
                memcpy(tmp, inner + start, seg_len);
                tmp[seg_len] = '\0';
                {
                    char *t = tmp;
                    while (*t && isspace((unsigned char)*t)) {
                        t++;
                    }
                    {
                        char *e = t + strlen(t);
                        while (e > t && isspace((unsigned char)*(e - 1))) {
                            e--;
                        }
                        *e = '\0';
                    }
                    if (*t) {
                        size_t tl = strlen(t);

                        if (tl >= 64) {
                            return -1;
                        }
                        memcpy(out[count], t, tl + 1);
                        count++;
                    }
                }
                start = i + 1;
            }
        }
    }
    *out_count = count;
    return 0;
}

static int parse_formal_token(const char *tok, const FgirProgram *prog, char *name_out,
                              uint8_t *has_type_out, char *spec_out, size_t spec_sz, char *err,
                              size_t err_sz) {
    FlatSlotType ty;
    int has = 0;
    FlatSchemaCatalog cat;
    int cat_ok = 0;

    *has_type_out = 0;
    if (spec_out && spec_sz > 0) {
        spec_out[0] = '\0';
    }
    if (!tok || !name_out) {
        return -1;
    }
    memset(&cat, 0, sizeof cat);
    if (prog && prog->schema_count > 0 &&
        flat_schema_build_catalog(prog->schemas, prog->schema_count, &cat, err, err_sz) == 0) {
        cat_ok = 1;
    }
    if (flat_bind_parse_lhs(tok, name_out, 64, &ty, &has) != 0) {
        snprintf(err, err_sz, "def: bad formal %s", tok);
        return -1;
    }
    if (has) {
        if (flat_slot_type_parse_spec(strchr(tok, ':') + 1, &ty, cat_ok ? &cat : NULL, err,
                                        err_sz) != 0) {
            if (err && err_sz) {
                snprintf(err, err_sz, "def: bad formal type %s", tok);
            }
            return -1;
        }
        *has_type_out = 1;
        if (spec_out && spec_sz > 0) {
            const char *spec = strchr(tok, ':') + 1;
            while (*spec && isspace((unsigned char)*spec)) {
                spec++;
            }
            snprintf(spec_out, spec_sz, "%s", spec);
        }
    }
    return 0;
}

static int split_def_param_tag(char *paren_inner, char **params_out, char **tag_out, int *has_tag) {
    int depth = 0;
    char quote = 0;
    char *p;

    *params_out = paren_inner;
    *tag_out = NULL;
    *has_tag = 0;
    for (p = paren_inner; *p; p++) {
        if (!quote && (*p == '\'' || *p == '"')) {
            quote = *p;
        } else if (quote && *p == quote) {
            quote = 0;
        } else if (!quote && (*p == '(' || *p == '[')) {
            depth++;
        } else if (!quote && (*p == ')' || *p == ']')) {
            if (depth > 0) {
                depth--;
            }
        } else if (!quote && depth == 0 && *p == ';') {
            *p = '\0';
            *tag_out = flat_native_trim(p + 1);
            *has_tag = 1;
            break;
        }
    }
    *params_out = flat_native_trim(paren_inner);
    return 0;
}

int flat_func_is_def_clause_line(const char *line) {
    char buf[FGIR_MAX_LINE];
    const char *p;
    const char *q;

    if (!line) {
        return 0;
    }
    snprintf(buf, sizeof buf, "%s", line);
    p = flat_native_trim(buf);
    if (p[0] != '(') {
        return 0;
    }
    if (!strstr(p, "->")) {
        return 0;
    }
    q = p + 1;
    while (*q && isspace((unsigned char)*q)) {
        q++;
    }
    return (*q == ';');
}

static int returns_match_proto(const FgirFunction *fn, const FgirFunction *proto, char *err,
                               size_t err_sz) {
    int ri;

    if (fn->return_count != proto->return_count) {
        snprintf(err, err_sz, "def.clause.return.count");
        return -1;
    }
    for (ri = 0; ri < fn->return_count; ri++) {
        if (strcmp(fn->return_names[ri], proto->return_names[ri]) != 0) {
            snprintf(err, err_sz, "def.clause.return.name");
            return -1;
        }
        if (fn->return_has_type_arr[ri] != proto->return_has_type_arr[ri]) {
            snprintf(err, err_sz, "def.clause.return.type");
            return -1;
        }
        if (strcmp(fn->return_type_spec_arr[ri], proto->return_type_spec_arr[ri]) != 0) {
            snprintf(err, err_sz, "def.clause.return.type");
            return -1;
        }
    }
    return 0;
}

static void copy_def_proto_header(FgirFunction *fn, const FgirFunction *proto) {
    int pi;

    snprintf(fn->name, sizeof fn->name, "%s", proto->name);
    fn->param_count = proto->param_count;
    for (pi = 0; pi < proto->param_count; pi++) {
        snprintf(fn->params[pi], sizeof fn->params[pi], "%s", proto->params[pi]);
        fn->param_has_type[pi] = proto->param_has_type[pi];
        snprintf(fn->param_type_spec[pi], sizeof fn->param_type_spec[pi], "%s",
                 proto->param_type_spec[pi]);
    }
    snprintf(fn->executor_label, sizeof fn->executor_label, "%s", proto->executor_label);
    fn->is_global_executor = proto->is_global_executor;
    fn->call_via_vm = proto->call_via_vm;
}

int flat_func_parse_def_clause_line(const char *line, FgirFunction *fn, const FgirFunction *proto,
                                    FgirProgram *prog, char *err, size_t err_size) {
    char buf[FGIR_MAX_LINE];
    char *p;
    const char *lp;
    const char *rp;
    const char *arrow;
    char inner_work[FGIR_MAX_LINE];
    char *params_part;
    char *tag_part;
    int def_has_tag = 0;

    if (!line || !fn || !proto || !prog) {
        return -1;
    }
    memset(fn, 0, sizeof *fn);
    copy_def_proto_header(fn, proto);

    snprintf(buf, sizeof buf, "%s", line);
    p = flat_native_trim(buf);
    lp = strchr(p, '(');
    if (!lp) {
        snprintf(err, err_size, "def.clause: bad syntax");
        return -1;
    }
    rp = strchr(lp, ')');
    if (!rp || rp <= lp) {
        snprintf(err, err_size, "def.clause: bad syntax");
        return -1;
    }
    snprintf(inner_work, sizeof inner_work, "%.*s", (int)(rp - lp - 1), lp + 1);
    split_def_param_tag(inner_work, &params_part, &tag_part, &def_has_tag);
    if (params_part && params_part[0] != '\0') {
        snprintf(err, err_size, "def.clause: parameters not allowed");
        return -1;
    }
    if (!def_has_tag) {
        snprintf(err, err_size, "def.clause: missing tag section");
        return -1;
    }
    fn->def_tag_has_section = 1;
    fn->def_tag_expr[0] = '\0';
    fn->def_tag_canonical[0] = '\0';
    if (tag_part && tag_part[0] != '\0') {
        int trc;
        char terr[64];

        snprintf(fn->def_tag_expr, sizeof fn->def_tag_expr, "%s", tag_part);
        terr[0] = '\0';
        trc = flat_func_tag_canonical_from_literal(tag_part, fn->def_tag_canonical,
                                                   sizeof fn->def_tag_canonical, terr, sizeof terr);
        if (trc < 0) {
            snprintf(err, err_size, "%s", terr[0] ? terr : "type.tag.arg");
            return -1;
        }
        if (trc > 0) {
            snprintf(err, err_size, "def tag must be const literal");
            return -1;
        }
    }
    arrow = strstr(rp + 1, "->");
    if (!arrow) {
        snprintf(err, err_size, "def.clause: requires -> return");
        return -1;
    }
    arrow += 2;
    while (*arrow && isspace((unsigned char)*arrow)) {
        arrow++;
    }
    {
        char ret_inner[FGIR_MAX_LINE];
        char ret_toks[FGIR_MAX_FUNC_RETURNS][64];
        int rc = 0;
        int ri;

        snprintf(ret_inner, sizeof ret_inner, "%s", arrow);
        {
            char *e = ret_inner + strlen(ret_inner);
            while (e > ret_inner && isspace((unsigned char)*(e - 1))) {
                e--;
            }
            *e = '\0';
        }
        if (ret_inner[0] == '\0') {
            snprintf(err, err_size, "def.clause: bad return name");
            return -1;
        }
        if (flat_func_split_param_list(ret_inner, ret_toks, FGIR_MAX_FUNC_RETURNS, &rc) != 0) {
            snprintf(err, err_size, "def.clause: too many returns");
            return -1;
        }
        if (rc <= 0 || rc > FGIR_MAX_FUNC_RETURNS) {
            snprintf(err, err_size, "def.clause: bad return name");
            return -1;
        }
        fn->return_count = rc;
        for (ri = 0; ri < rc; ri++) {
            if (parse_formal_token(ret_toks[ri], prog, fn->return_names[ri],
                                   &fn->return_has_type_arr[ri], fn->return_type_spec_arr[ri],
                                   sizeof fn->return_type_spec_arr[ri], err, err_size) != 0) {
                return -1;
            }
        }
        snprintf(fn->return_name, sizeof fn->return_name, "%s", fn->return_names[0]);
        fn->return_has_type = fn->return_has_type_arr[0];
        snprintf(fn->return_type_spec, sizeof fn->return_type_spec, "%s",
                 fn->return_type_spec_arr[0]);
    }
    if (returns_match_proto(fn, proto, err, err_size) != 0) {
        return -1;
    }
    snprintf(fn->impl_label, sizeof fn->impl_label, "%s%s_%d", FLAT_FUNC_IMPL_PREFIX, fn->name,
             prog->function_count);
    return 0;
}

int flat_func_parse_def_header(const char *line, FgirFunction *fn, FgirProgram *prog, char *err,
                               size_t err_size) {
    const char *p = line + 3;
    const char *lp;
    const char *rp;
    const char *arrow;
    char inner[FGIR_MAX_LINE];
    char inner_work[FGIR_MAX_LINE];
    char *params_part;
    char *tag_part;
    int def_has_tag = 0;
    char raw_params[FGIR_MAX_FUNC_PARAMS][64];
    char name_buf[FGIR_MAX_FUNC_NAME];
    size_t n;
    int pi;

    memset(fn, 0, sizeof *fn);
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    lp = strchr(p, '(');
    if (!lp) {
        snprintf(err, err_size, "def requires parameter list");
        return -1;
    }
    n = (size_t)(lp - p);
    if (n == 0 || n >= sizeof name_buf) {
        snprintf(err, err_size, "bad function name");
        return -1;
    }
    memcpy(name_buf, p, n);
    name_buf[n] = '\0';
    {
        size_t j;
        for (j = 0; j < n; j++) {
            if (!is_ident_char((unsigned char)name_buf[j])) {
                snprintf(err, err_size, "bad function name");
                return -1;
            }
        }
    }
    snprintf(fn->name, sizeof fn->name, "%s", name_buf);
    rp = strchr(lp, ')');
    if (!rp || rp <= lp) {
        snprintf(err, err_size, "def: bad parameter list");
        return -1;
    }
    snprintf(inner_work, sizeof inner_work, "%.*s", (int)(rp - lp - 1), lp + 1);
    split_def_param_tag(inner_work, &params_part, &tag_part, &def_has_tag);
    snprintf(inner, sizeof inner, "%s", params_part ? params_part : "");
    fn->def_tag_has_section = (uint8_t)(def_has_tag ? 1 : 0);
    fn->def_tag_expr[0] = '\0';
    fn->def_tag_canonical[0] = '\0';
    if (def_has_tag && tag_part && tag_part[0] != '\0') {
        int trc;
        char terr[64];

        snprintf(fn->def_tag_expr, sizeof fn->def_tag_expr, "%s", tag_part);
        terr[0] = '\0';
        trc = flat_func_tag_canonical_from_literal(tag_part, fn->def_tag_canonical,
                                                   sizeof fn->def_tag_canonical, terr, sizeof terr);
        if (trc < 0) {
            snprintf(err, err_size, "%s", terr[0] ? terr : "type.tag.arg");
            return -1;
        }
        if (trc > 0) {
            snprintf(err, err_size, "def tag must be const literal");
            return -1;
        }
    }
    if (flat_func_split_param_list(inner, raw_params, FGIR_MAX_FUNC_PARAMS,
                                   &fn->param_count) != 0) {
        snprintf(err, err_size, "def: too many parameters");
        return -1;
    }
    for (pi = 0; pi < fn->param_count; pi++) {
        if (parse_formal_token(raw_params[pi], prog, fn->params[pi],
                                &fn->param_has_type[pi], fn->param_type_spec[pi],
                                sizeof fn->param_type_spec[pi], err, err_size) != 0) {
            return -1;
        }
    }
    arrow = strstr(rp + 1, "->");
    if (!arrow) {
        snprintf(err, err_size, "def requires -> return name");
        return -1;
    }
    arrow += 2;
    while (*arrow && isspace((unsigned char)*arrow)) {
        arrow++;
    }
    {
        char ret_inner[FGIR_MAX_LINE];
        char ret_toks[FGIR_MAX_FUNC_RETURNS][64];
        int rc = 0;
        int ri;

        snprintf(ret_inner, sizeof ret_inner, "%s", arrow);
        {
            char *e = ret_inner + strlen(ret_inner);
            while (e > ret_inner && isspace((unsigned char)*(e - 1))) {
                e--;
            }
            *e = '\0';
        }
        if (ret_inner[0] == '\0') {
            snprintf(err, err_size, "def: bad return name");
            return -1;
        }
        if (flat_func_split_param_list(ret_inner, ret_toks, FGIR_MAX_FUNC_RETURNS, &rc) != 0) {
            snprintf(err, err_size, "def: too many return values");
            return -1;
        }
        if (rc <= 0 || rc > FGIR_MAX_FUNC_RETURNS) {
            snprintf(err, err_size, "def: bad return name");
            return -1;
        }
        fn->return_count = rc;
        for (ri = 0; ri < rc; ri++) {
            if (parse_formal_token(ret_toks[ri], prog, fn->return_names[ri],
                                   &fn->return_has_type_arr[ri], fn->return_type_spec_arr[ri],
                                   sizeof fn->return_type_spec_arr[ri], err, err_size) != 0) {
                return -1;
            }
            {
                int pi;
                for (pi = 0; pi < fn->param_count; pi++) {
                    if (strcmp(fn->return_names[ri], fn->params[pi]) == 0) {
                        snprintf(err, err_size, "def.return.name.param %s", fn->name);
                        return -1;
                    }
                }
            }
        }
        snprintf(fn->return_name, sizeof fn->return_name, "%s", fn->return_names[0]);
        fn->return_has_type = fn->return_has_type_arr[0];
        snprintf(fn->return_type_spec, sizeof fn->return_type_spec, "%s",
                 fn->return_type_spec_arr[0]);
    }
    snprintf(fn->executor_label, sizeof fn->executor_label, "%s", prog->default_executor);
    snprintf(fn->impl_label, sizeof fn->impl_label, "%s%s_%d", FLAT_FUNC_IMPL_PREFIX, fn->name,
             prog->function_count);
    fn->is_global_executor = (strstr(fn->executor_label, "global.scope") != NULL) ? 1 : 0;
    fn->call_via_vm = prog->func_call_vm;
    return 0;
}

int flat_func_finish_def(FgirFunction *fn, const FgirBlock *impl, int manual_slots, char *err,
                         size_t err_size) {
    (void)impl;

    if (fn->is_global_executor) {
        fn->func_vcx_slots = 0;
        return 0;
    }
    if (manual_slots > FLAT_MAX_VARS) {
        snprintf(err, err_size, "func.varSlots.exceeds_max (%d > %d)", manual_slots,
                 FLAT_MAX_VARS);
        return -1;
    }
    /*
     * Unique executor uses V.createAuto(funcVcxSlots) at runtime. 0 = default seed;
     * explicit N = initial capacity hint only (see doc_src/USER_FUNCTIONS.md).
     */
    fn->func_vcx_slots = (manual_slots >= 0) ? (uint32_t)manual_slots : 0u;
    return 0;
}

const FgirFunction *flat_func_at(const FgirProgram *prog, int index) {
    if (!prog || index < 0 || index >= prog->function_count) {
        return NULL;
    }
    return &prog->functions[index];
}

const FgirFunction *flat_func_find(const FgirProgram *prog, const char *name) {
    int i;
    if (!prog || !name) {
        return NULL;
    }
    for (i = 0; i < prog->function_count; i++) {
        if (strcmp(prog->functions[i].name, name) == 0) {
            return &prog->functions[i];
        }
    }
    return NULL;
}

int flat_func_index(const FgirProgram *prog, const char *name) {
    int i;
    if (!prog || !name) {
        return -1;
    }
    for (i = 0; i < prog->function_count; i++) {
        if (strcmp(prog->functions[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int parse_call_inner(const char *line, char *name_out, size_t name_sz, char *args_out,
                            size_t args_sz, char *ret_out, size_t ret_sz, int *arrow_form) {
    const char *lp;
    const char *rp;
    const char *arrow;
    size_t n;

    *arrow_form = 0;
    lp = strchr(line, '(');
    if (!lp) {
        return -1;
    }
    {
        const char *q;
        int dot_count = 0;
        for (q = line; q < lp; q++) {
            if (*q == '.') {
                dot_count++;
            }
        }
        if (dot_count > 1) {
            return -1;
        }
    }
    n = (size_t)(lp - line);
    while (n > 0 && isspace((unsigned char)line[n - 1])) {
        n--;
    }
    {
        size_t start = 0;
        while (start < n && isspace((unsigned char)line[start])) {
            start++;
        }
        if (n - start == 0 || n - start >= name_sz) {
            return -1;
        }
        memcpy(name_out, line + start, n - start);
        name_out[n - start] = '\0';
    }
    rp = strrchr(line, ')');
    if (!rp || rp <= lp) {
        return -1;
    }
    snprintf(args_out, args_sz, "%.*s", (int)(rp - lp - 1), lp + 1);
    arrow = strstr(rp + 1, "->");
    if (arrow) {
        const char *r = arrow + 2;
        *arrow_form = 1;
        while (*r && isspace((unsigned char)*r)) {
            r++;
        }
        snprintf(ret_out, ret_sz, "%s", r);
        {
            char *e = ret_out + strlen(ret_out);
            while (e > ret_out && isspace((unsigned char)*(e - 1))) {
                e--;
            }
            *e = '\0';
        }
        return 0;
    }
    if (ret_out && ret_sz > 0) {
        ret_out[0] = '\0';
    }
    return 0;
}

static int flat_func_fill_call_returns(const char *ret_list, FgirInstr *ins) {
    char buf[FGIR_MAX_LINE];
    char tokens[FGIR_MAX_FUNC_RETURNS][64];
    int n = 0;
    int i;

    if (!ret_list || !ins) {
        return -1;
    }
    snprintf(buf, sizeof buf, "%s", ret_list);
    if (flat_func_split_param_list(buf, tokens, FGIR_MAX_FUNC_RETURNS, &n) != 0) {
        return -1;
    }
    if (n == 0) {
        return -1;
    }
    ins->func_ret_count = n;
    for (i = 0; i < n; i++) {
        char name[64];
        FlatSlotType ty;
        int has = 0;

        if (flat_bind_parse_lhs(tokens[i], name, sizeof name, &ty, &has) != 0) {
            return -1;
        }
        snprintf(ins->func_rets[i], sizeof ins->func_rets[i], "%s", name);
    }
    snprintf(ins->func_ret, sizeof ins->func_ret, "%s", ret_list);
    return 0;
}

int flat_func_check_call_returns(const FgirFunction *fn, const FgirInstr *call, char *err,
                                 size_t err_size) {
    int i;

    if (!fn || !call) {
        return -1;
    }
    if (call->func_ret_count != fn->return_count) {
        snprintf(err, err_size, "call.function.return.count %s", fn->name);
        return -1;
    }
    for (i = 0; i < call->func_ret_count; i++) {
        char name[64];
        FlatSlotType ty;
        int has = 0;

        if (flat_bind_parse_lhs(call->func_rets[i], name, sizeof name, &ty, &has) != 0) {
            snprintf(err, err_size, "call.function.return %s", fn->name);
            return -1;
        }
    }
    return 0;
}

int flat_func_try_classify_call(const char *line, const FgirProgram *prog, FgirInstr *ins) {
    char name[FGIR_MAX_FUNC_NAME];
    char args[FGIR_MAX_LINE];
    char ret[64];
    int arrow = 0;

    if (!line || !prog || !ins) {
        return -1;
    }
    if (line[0] == '#' || starts_with_kw(line, "def") || starts_with_kw(line, "use")) {
        return 0;
    }
    if (parse_call_inner(line, name, sizeof name, args, sizeof args, ret, sizeof ret, &arrow) == 0 &&
        arrow) {
        if (!flat_func_name_used(prog, name)) {
            return 0;
        }
        ins->kind = FGIR_FUNC_CALL;
        snprintf(ins->func_name, sizeof ins->func_name, "%s", name);
        ins->func_resolved_index = -1;
        ins->func_call_dynamic_tag = 0;
        if (args[0] != '\0') {
            snprintf(ins->func_args, sizeof ins->func_args, "%s", args);
        }
        if (flat_func_fill_call_returns(ret, ins) != 0) {
            return 0;
        }
        snprintf(ins->text, sizeof ins->text, "%s", line);
        return 1;
    }
    {
        char buf[FGIR_MAX_LINE];
        char ret_name[64];
        char *bind;
        char *val;
        FlatAssignSplitForm form;

        snprintf(buf, sizeof buf, "%s", line);
        if (flat_assign_split_line(buf, &bind, &val, &form) == 0 && form == FLAT_ASSIGN_SPLIT_EQ) {
            if (parse_call_inner(val, name, sizeof name, args, sizeof args, ret_name, sizeof ret_name,
                               &arrow) == 0 && !arrow) {
                if (!flat_func_name_used(prog, name)) {
                    return 0;
                }
                ins->kind = FGIR_FUNC_CALL;
                snprintf(ins->func_name, sizeof ins->func_name, "%s", name);
                ins->func_resolved_index = -1;
                ins->func_call_dynamic_tag = 0;
                snprintf(ins->func_args, sizeof ins->func_args, "%s", args);
                if (flat_func_fill_call_returns(bind, ins) != 0) {
                    return 0;
                }
                snprintf(ins->text, sizeof ins->text, "%s", line);
                return 1;
            }
        }
    }
    return 0;
}

int flat_func_reclassify_program(FgirProgram *prog) {
    int bi, ii;

    if (!prog) {
        return -1;
    }
    for (bi = 0; bi < prog->block_count; bi++) {
        FgirBlock *b = &prog->blocks[bi];
        for (ii = 0; ii < b->instr_count; ii++) {
            FgirInstr *ins = &b->instr[ii];
            if (ins->kind == FGIR_ASSIGN || ins->kind == FGIR_IO) {
                (void)flat_func_try_classify_call(ins->text, prog, ins);
            }
        }
    }
    return 0;
}

static int export_has(const FgirProgram *prog, const char *name) {
    int i;
    if (!prog || !name) {
        return 0;
    }
    for (i = 0; i < prog->export_count; i++) {
        if (strcmp(prog->exports[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

int flat_func_prepare_fmod_unit(FgirProgram *mod, const char *import_as, char *err, size_t err_size) {
    int i;
    char old_impl[FGIR_MAX_LABEL];

    if (!mod || !import_as || !import_as[0]) {
        snprintf(err, err_size, "fmod: internal prepare error");
        return -1;
    }
    if (!mod->mod_id_set || mod->mod_id[0] == '\0') {
        snprintf(err, err_size, "fmod: mod('id') required");
        return -1;
    }
    if (mod->main_index >= 0) {
        snprintf(err, err_size, "fmod: #main not allowed");
        return -1;
    }
    if (mod->export_count == 0) {
        snprintf(err, err_size, "fmod: export requires at least one symbol");
        return -1;
    }
    {
        int ei;
        for (ei = 0; ei < mod->export_count; ei++) {
            if (!flat_func_find(mod, mod->exports[ei])) {
                snprintf(err, err_size, "fmod: export %s has no def", mod->exports[ei]);
                return -1;
            }
        }
    }
    for (i = 0; i < mod->function_count; i++) {
        FgirFunction *fn = &mod->functions[i];
        char orig[FGIR_MAX_FUNC_NAME];
        char slug[FGIR_MAX_FUNC_NAME];

        snprintf(orig, sizeof orig, "%s", fn->name);
        if (join_strings(slug, sizeof slug, mod->mod_id, "_", orig) != 0) {
            snprintf(err, err_size, "fmod: symbol name too long");
            return -1;
        }
        if (export_has(mod, orig)) {
            if (join_strings(fn->name, sizeof fn->name, import_as, ".", orig) != 0) {
                snprintf(err, err_size, "fmod: exported name too long");
                return -1;
            }
        } else {
            char suffix[FGIR_MAX_FUNC_NAME];

            if (join_strings(suffix, sizeof suffix, mod->mod_id, "_", orig) != 0 ||
                join_strings(fn->name, sizeof fn->name, "__", suffix, "") != 0) {
                snprintf(err, err_size, "fmod: internal name too long");
                return -1;
            }
        }
        snprintf(old_impl, sizeof old_impl, "%s", fn->impl_label);
        snprintf(fn->impl_label, sizeof fn->impl_label, "%s%s", FLAT_FUNC_IMPL_PREFIX, slug);

        {
            int bi;
            for (bi = 0; bi < mod->block_count; bi++) {
                if (strcmp(mod->blocks[bi].label, old_impl) == 0) {
                    snprintf(mod->blocks[bi].label, sizeof mod->blocks[bi].label, "%s", fn->impl_label);
                    break;
                }
            }
        }
    }
    flat_func_reclassify_program(mod);
    return 0;
}

static uint32_t hash_label_u32(const char *label) {
    uint32_t h = 5381u;
    const unsigned char *p;
    for (p = (const unsigned char *)label; *p; p++) {
        h = h * 33u + *p;
    }
    return h;
}

static int block_id_for_label(const char *label) {
    if (strcmp(label, "#main") == 0) {
        return 0;
    }
    return (int)hash_label_u32(label);
}

static void ins_init_assign(FgirInstr *ins, const char *line) {
    memset(ins, 0, sizeof *ins);
    ins->kind = FGIR_ASSIGN;
    if (flat_native_is_call_line(line) || (line[0] == 'V' && line[1] == '.' && strchr(line, '('))) {
        ins->kind = FGIR_IO;
    }
    snprintf(ins->text, sizeof ins->text, "%s", line);
}

static void ins_init_call(FgirInstr *ins, const char *label) {
    memset(ins, 0, sizeof *ins);
    ins->kind = FGIR_CALL;
    snprintf(ins->text, sizeof ins->text, "call %s", label);
    snprintf(ins->call_target, sizeof ins->call_target, "%s", label);
}

static int emit_call_arg_slots(FgirInstr *expanded, int *n, int arg_count,
                               char arg_parts[][FGIR_MAX_LINE], char *line, size_t line_sz,
                               char *err, size_t err_size) {
    int i;

    for (i = 0; i < arg_count; i++) {
        if (!is_simple_var_ref(arg_parts[i])) {
            if (flat_str_assign_fl_tmp(line, line_sz, i, arg_parts[i]) != 0) {
                snprintf(err, err_size, "lowered tmp assign too long");
                return -1;
            }
            ins_init_assign(&expanded[(*n)++], line);
        }
    }
    for (i = 0; i < arg_count; i++) {
        if (is_simple_var_ref(arg_parts[i])) {
            if (assign_param_in_line(line, line_sz, i, arg_parts[i]) != 0) {
                snprintf(err, err_size, "lowered param_in assign too long");
                return -1;
            }
        } else {
            char rhs[32];
            int rn = snprintf(rhs, sizeof rhs, "__fl_tmp_%d", i);
            if (rn < 0 || (size_t)rn >= sizeof rhs ||
                assign_param_in_line(line, line_sz, i, rhs) != 0) {
                snprintf(err, err_size, "lowered param_in assign too long");
                return -1;
            }
        }
        ins_init_assign(&expanded[(*n)++], line);
    }
    return 0;
}

static int build_param_name_list(const FgirFunction *fn, char *out, size_t out_sz) {
    int pos = 0;
    int i;

    pos += snprintf(out + pos, out_sz - (size_t)pos, "[");
    for (i = 0; i < fn->param_count; i++) {
        if (i > 0) {
            pos += snprintf(out + pos, out_sz - (size_t)pos, ", ");
        }
        pos += snprintf(out + pos, out_sz - (size_t)pos, "'%s'", fn->params[i]);
    }
    pos += snprintf(out + pos, out_sz - (size_t)pos, "]");
    return (pos >= (int)out_sz) ? -1 : 0;
}

static int build_param_in_names_list(int pcount, char *out, size_t out_sz) {
    int pos = 0;
    int i;

    pos += snprintf(out + pos, out_sz - (size_t)pos, "[");
    for (i = 0; i < pcount; i++) {
        if (i > 0) {
            pos += snprintf(out + pos, out_sz - (size_t)pos, ", ");
        }
        pos += snprintf(out + pos, out_sz - (size_t)pos, "'param_in_%d'", i);
    }
    pos += snprintf(out + pos, out_sz - (size_t)pos, "]");
    return (pos >= (int)out_sz) ? -1 : 0;
}

static void emit_callee_return_slot_setup(FgirInstr *expanded, int *n, const FgirInstr *call) {
    char line[FGIR_MAX_LINE];
    int i;

    for (i = 0; i < call->func_ret_count; i++) {
        snprintf(line, sizeof line, "__fl_calleReturn_%d = '%s'", i, call->func_rets[i]);
        ins_init_assign(&expanded[(*n)++], line);
    }
}

static int build_callee_return_copy_lists(const FgirInstr *call, char *src_out, size_t src_sz,
                                          char *dst_out, size_t dst_sz) {
    int pos = 0;
    int i;

    pos += snprintf(src_out + pos, src_sz - (size_t)pos, "[");
    for (i = 0; i < call->func_ret_count; i++) {
        pos +=
            snprintf(src_out + pos, src_sz - (size_t)pos, "%s'__fl_calleReturn_%d'", i ? "," : "", i);
    }
    pos += snprintf(src_out + pos, src_sz - (size_t)pos, "]");
    pos = 0;
    pos += snprintf(dst_out + pos, dst_sz - (size_t)pos, "[");
    for (i = 0; i < call->func_ret_count; i++) {
        pos += snprintf(dst_out + pos, dst_sz - (size_t)pos, "%s'calleReturn_%d'", i ? "," : "", i);
    }
    pos += snprintf(dst_out + pos, dst_sz - (size_t)pos, "]");
    return (pos >= (int)src_sz || pos >= (int)dst_sz) ? -1 : 0;
}

static const FgirFunction *flat_func_for_impl_label(const FgirProgram *prog, const char *label) {
    int i;

    if (!prog || !label || !label[0]) {
        return NULL;
    }
    for (i = 0; i < prog->function_count; i++) {
        if (strcmp(prog->functions[i].impl_label, label) == 0) {
            return &prog->functions[i];
        }
    }
    return NULL;
}

static int caller_body_needs_global_vcx(const FgirProgram *prog, const FgirBlock *caller_block) {
    const FgirFunction *caller;

    if (!prog || !caller_block || !caller_block->label[0]) {
        return 1;
    }
    caller = flat_func_for_impl_label(prog, caller_block->label);
    if (caller == NULL) {
        return 1;
    }
    return caller->is_global_executor ? 1 : 0;
}

static int call_site_uses_vm(const FgirProgram *prog, const FgirBlock *caller_block,
                             const FgirFunction *callee) {
    const FgirFunction *caller;

    if (callee && callee->call_via_vm) {
        return 1;
    }
    if (!prog || !caller_block) {
        return 0;
    }
    caller = flat_func_for_impl_label(prog, caller_block->label);
    return (caller != NULL && caller->call_via_vm) ? 1 : 0;
}

static const FgirFunction *call_callee(const FgirProgram *prog, const FgirInstr *call, char *err,
                                       size_t err_size) {
    if (call->func_resolved_index >= 0) {
        return flat_func_at(prog, call->func_resolved_index);
    }
    if (call->func_call_dynamic_tag) {
        char argbuf[FGIR_MAX_LINE];
        char argtok[FGIR_MAX_FUNC_PARAMS][64];
        int ac = 0;
        int i;

        snprintf(argbuf, sizeof argbuf, "%s", call->func_args);
        if (flat_func_split_param_list(argbuf, argtok, FGIR_MAX_FUNC_PARAMS, &ac) != 0) {
            snprintf(err, err_size, "too many arguments");
            return NULL;
        }
        for (i = 0; i < prog->function_count; i++) {
            if (strcmp(prog->functions[i].name, call->func_name) == 0 &&
                prog->functions[i].param_count == ac) {
                return &prog->functions[i];
            }
        }
        snprintf(err, err_size, "unknown function %s", call->func_name);
        return NULL;
    }
    return flat_func_find(prog, call->func_name);
}

static int lower_one_call_vm(FgirProgram *prog, FgirBlock *b, int at, const FgirInstr *call,
                               char *err, size_t err_size) {
    const FgirFunction *fn = call_callee(prog, call, err, err_size);
    FgirInstr expanded[24];
    int n = 0;
    char arg_parts[FGIR_MAX_FUNC_PARAMS][FGIR_MAX_LINE];
    char arg_tokens[FGIR_MAX_FUNC_PARAMS][64];
    int arg_count = 0;
    char args_buf[FGIR_MAX_LINE];
    char line[FGIR_MAX_LINE];
    int i;

    if (!fn) {
        if (err[0] == '\0') {
            snprintf(err, err_size, "unknown function %s", call->func_name);
        }
        return -1;
    }
    if (flat_func_check_call_returns(fn, call, err, err_size) != 0) {
        return -1;
    }
    snprintf(args_buf, sizeof args_buf, "%s", call->func_args);
    if ((args_buf[0] == '\0' || strchr(args_buf, ',') == NULL) && call->text[0] != '\0' &&
        fn->param_count > 1) {
        char name[FGIR_MAX_FUNC_NAME];
        char ret[64];
        int arrow = 0;
        if (parse_call_inner(call->text, name, sizeof name, args_buf, sizeof args_buf, ret,
                             sizeof ret, &arrow) != 0) {
            snprintf(err, err_size, "call.function.arg %s", fn->name);
            return -1;
        }
    }
    if (flat_func_split_param_list(args_buf, arg_tokens, FGIR_MAX_FUNC_PARAMS, &arg_count) != 0) {
        snprintf(err, err_size, "too many arguments to %s", fn->name);
        return -1;
    }
    for (i = 0; i < arg_count; i++) {
        snprintf(arg_parts[i], FGIR_MAX_LINE, "%s", arg_tokens[i]);
    }
    if (arg_count != fn->param_count) {
        snprintf(err, err_size, "call.function.argcount %s", fn->name);
        return -1;
    }
    for (i = 0; i < arg_count; i++) {
        if (is_simple_var_ref(arg_parts[i])) {
            if (flat_str_assign_fl_tmp(line, sizeof line, i, arg_parts[i]) != 0) {
                snprintf(err, err_size, "lowered tmp assign too long");
                return -1;
            }
            ins_init_assign(&expanded[n++], line);
        } else if (flat_str_assign_fl_tmp(line, sizeof line, i, arg_parts[i]) != 0) {
            snprintf(err, err_size, "lowered tmp assign too long");
            return -1;
        } else {
            ins_init_assign(&expanded[n++], line);
        }
    }
    snprintf(line, sizeof line, "__fl_funcIndex = %d",
             call->func_resolved_index >= 0 ? call->func_resolved_index
                                            : flat_func_index(prog, fn->name));
    ins_init_assign(&expanded[n++], line);
    memcpy(&expanded[n], call, sizeof *call);
    expanded[n].kind = FGIR_FUNC_CALL_VM;
    n++;
    if (caller_body_needs_global_vcx(prog, b)) {
        ins_init_assign(&expanded[n++], "V.set(1)");
    }
    if (fn->is_global_executor) {
        int ri;
        for (ri = 0; ri < fn->return_count; ri++) {
            if (flat_str_assign_line(line, sizeof line, call->func_rets[ri], fn->return_names[ri]) !=
                0) {
                snprintf(err, err_size, "lowered return assign too long");
                return -1;
            }
            ins_init_assign(&expanded[n++], line);
        }
    }
    if (b->instr_count - 1 - at + n > FGIR_MAX_INSTR) {
        snprintf(err, err_size, "lowered call too large");
        return -1;
    }
    memmove(&b->instr[at + n], &b->instr[at + 1], (size_t)(b->instr_count - at - 1) * sizeof *b->instr);
    memcpy(&b->instr[at], expanded, (size_t)n * sizeof *b->instr);
    b->instr_count = b->instr_count - 1 + n;
    for (i = 0; i < b->instr_count; i++) {
        b->instr[i].index = i + 1;
    }
    return 0;
}

static int lower_one_call(FgirProgram *prog, FgirBlock *b, int at, const FgirInstr *call,
                          char *err, size_t err_size) {
    const FgirFunction *fn = call_callee(prog, call, err, err_size);
    FgirInstr expanded[48];
    char name[FGIR_MAX_FUNC_NAME];
    char ret[64];
    int n = 0;
    char arg_parts[FGIR_MAX_FUNC_PARAMS][FGIR_MAX_LINE];
    char arg_tokens[FGIR_MAX_FUNC_PARAMS][64];
    int arg_count = 0;
    char args_buf[FGIR_MAX_LINE];
    char line[FGIR_MAX_LINE];
    char plist[FGIR_MAX_LINE];
    char pin_list[FGIR_MAX_LINE];
    uint32_t target_id;
    int cap;
    int i;

    if (!fn) {
        if (err[0] == '\0') {
            snprintf(err, err_size, "unknown function %s", call->func_name);
        }
        return -1;
    }
    if (flat_func_check_call_returns(fn, call, err, err_size) != 0) {
        return -1;
    }
    if (call->func_call_dynamic_tag && !call_site_uses_vm(prog, b, fn)) {
        snprintf(err, err_size, "dynamic func tag requires func:callVM 1");
        return -1;
    }
    if (call_site_uses_vm(prog, b, fn)) {
        return lower_one_call_vm(prog, b, at, call, err, err_size);
    }
    snprintf(args_buf, sizeof args_buf, "%s", call->func_args);
    if ((args_buf[0] == '\0' || strchr(args_buf, ',') == NULL) && call->text[0] != '\0' &&
        fn->param_count > 1) {
        int arrow = 0;
        if (parse_call_inner(call->text, name, sizeof name, args_buf, sizeof args_buf, ret,
                             sizeof ret, &arrow) != 0) {
            snprintf(err, err_size, "call.function.arg %s", fn->name);
            return -1;
        }
    }
    if (flat_func_split_param_list(args_buf, arg_tokens, FGIR_MAX_FUNC_PARAMS, &arg_count) != 0) {
        snprintf(err, err_size, "too many arguments to %s", fn->name);
        return -1;
    }
    for (i = 0; i < arg_count; i++) {
        snprintf(arg_parts[i], FGIR_MAX_LINE, "%s", arg_tokens[i]);
    }
    if (arg_count != fn->param_count) {
        snprintf(err, err_size, "call.function.argcount %s", fn->name);
        return -1;
    }
    target_id = (uint32_t)block_id_for_label(fn->impl_label);
    cap = (int)(FLAT_FUNC_INSTR_VCX_FIXED + (uint32_t)fn->param_count);

    ins_init_assign(&expanded[n++], FLAT_FUNC_RESERVED_CALLE_VAR " = V.current()");
    snprintf(line, sizeof line, "%s = V.create(%d)", FLAT_FUNC_RESERVED_INSTR_VAR, cap);
    ins_init_assign(&expanded[n++], line);

    if (emit_call_arg_slots(expanded, &n, arg_count, arg_parts, line, sizeof line, err,
                            err_size) != 0) {
        return -1;
    }

    snprintf(line, sizeof line, "__fl_targetBlock = %u", (unsigned)target_id);
    ins_init_assign(&expanded[n++], line);
    snprintf(line, sizeof line, "__fl_calleVcx = %s", FLAT_FUNC_RESERVED_CALLE_VAR);
    ins_init_assign(&expanded[n++], line);
    if (build_param_name_list(fn, plist, sizeof plist - 24) != 0) {
        snprintf(err, err_size, "param list too long");
        return -1;
    }
    if (concat2(line, sizeof line, "__fl_funcParamNames = ", plist) != 0) {
        snprintf(err, err_size, "param list too long");
        return -1;
    }
    ins_init_assign(&expanded[n++], line);
    if (fn->return_count > 1) {
        emit_callee_return_slot_setup(expanded, &n, call);
    }
    if (fn->return_count <= 1) {
        snprintf(line, sizeof line, "__fl_returnVar = '%s'", fn->return_names[0]);
        ins_init_assign(&expanded[n++], line);
        snprintf(line, sizeof line, "__fl_calleReturnVar = '%s'", call->func_rets[0]);
        ins_init_assign(&expanded[n++], line);
    }
    snprintf(line, sizeof line, "__fl_funcVcxSlots = %u", (unsigned)fn->func_vcx_slots);
    ins_init_assign(&expanded[n++], line);
    snprintf(line, sizeof line, "__fl_funcIndex = %d",
             call->func_resolved_index >= 0 ? call->func_resolved_index
                                            : flat_func_index(prog, fn->name));
    ins_init_assign(&expanded[n++], line);
    if (build_param_in_names_list(fn->param_count, pin_list, sizeof pin_list - 23) != 0) {
        snprintf(err, err_size, "param_in list too long");
        return -1;
    }
    if (concat2(line, sizeof line, "__fl_paramSrcNames = ", pin_list) != 0) {
        snprintf(err, err_size, "param_in list too long");
        return -1;
    }
    ins_init_assign(&expanded[n++], line);
    snprintf(line, sizeof line,
             "V.varsCopyByNames(%s, %s, "
             "['__fl_targetBlock','__fl_calleVcx','__fl_funcParamNames'], "
             "['targetBlock','calleVcx','funcParamNames'])",
             FLAT_FUNC_RESERVED_CALLE_VAR, FLAT_FUNC_RESERVED_INSTR_VAR);
    ins_init_assign(&expanded[n++], line);
    if (fn->return_count <= 1) {
        snprintf(line, sizeof line,
                 "V.varsCopyByNames(%s, %s, "
                 "['__fl_returnVar','__fl_calleReturnVar','__fl_funcVcxSlots','__fl_paramSrcNames',"
                 "'__fl_funcIndex'], "
                 "['returnVar','calleReturnVar','funcVcxSlots','paramSrcNames','__fl_funcIndex'])",
                 FLAT_FUNC_RESERVED_CALLE_VAR, FLAT_FUNC_RESERVED_INSTR_VAR);
        ins_init_assign(&expanded[n++], line);
    } else {
        char src_list[384];
        char dst_list[384];
        int pos;

        if (build_callee_return_copy_lists(call, src_list, sizeof src_list, dst_list,
                                           sizeof dst_list) != 0) {
            snprintf(err, err_size, "return slot list too long");
            return -1;
        }
        pos = snprintf(line, sizeof line,
                       "V.varsCopyByNames(%s, %s, "
                       "%.*s,'__fl_funcVcxSlots','__fl_paramSrcNames','__fl_funcIndex'], "
                       "%.*s,'funcVcxSlots','paramSrcNames','__fl_funcIndex'])",
                       FLAT_FUNC_RESERVED_CALLE_VAR, FLAT_FUNC_RESERVED_INSTR_VAR,
                       (int)(strlen(src_list) - 1), src_list, (int)(strlen(dst_list) - 1),
                       dst_list);
        if (pos < 0 || (size_t)pos >= sizeof line) {
            snprintf(err, err_size, "lowered copy line too long");
            return -1;
        }
        ins_init_assign(&expanded[n++], line);
    }
    if (arg_count > 0) {
        char src_list[256];
        char dst_list[256];
        int pos;

        pos = snprintf(src_list, sizeof src_list, "[");
        for (i = 0; i < arg_count; i++) {
            pos += snprintf(src_list + pos, sizeof src_list - (size_t)pos, "%s'param_in_%d'", i ? "," : "",
                            i);
        }
        pos += snprintf(src_list + pos, sizeof src_list - (size_t)pos, "]");
        pos = snprintf(dst_list, sizeof dst_list, "[");
        for (i = 0; i < arg_count; i++) {
            pos += snprintf(dst_list + pos, sizeof dst_list - (size_t)pos, "%s'param_in_%d'", i ? "," : "",
                            i);
        }
        pos += snprintf(dst_list + pos, sizeof dst_list - (size_t)pos, "]");
        {
            char tail[320];
            char pfx[128];
            int hlen;

            if (join_strings(tail, sizeof tail, src_list, ", ", dst_list) != 0) {
                snprintf(err, err_size, "lowered copy list too long");
                return -1;
            }
            hlen = snprintf(pfx, sizeof pfx, "V.varsCopyByNames(%s, %s, ", FLAT_FUNC_RESERVED_CALLE_VAR,
                            FLAT_FUNC_RESERVED_INSTR_VAR);
            if (hlen < 0 || (size_t)hlen >= sizeof pfx) {
                snprintf(err, err_size, "lowered copy prefix too long");
                return -1;
            }
            if (concat2(line, sizeof line, pfx, tail) != 0) {
                snprintf(err, err_size, "lowered copy line too long");
                return -1;
            }
            {
                size_t ll = strlen(line);

                if (ll + 2 >= sizeof line) {
                    snprintf(err, err_size, "lowered copy line too long");
                    return -1;
                }
                line[ll] = ')';
                line[ll + 1] = '\0';
            }
        }
        ins_init_assign(&expanded[n++], line);
    }
    ins_init_call(&expanded[n++], fn->executor_label);
    if (caller_body_needs_global_vcx(prog, b)) {
        ins_init_assign(&expanded[n++], "V.set(1)");
    }
    if (fn->is_global_executor) {
        int ri;
        for (ri = 0; ri < fn->return_count; ri++) {
            if (flat_str_assign_line(line, sizeof line, call->func_rets[ri], fn->return_names[ri]) !=
                0) {
                snprintf(err, err_size, "lowered return assign too long");
                return -1;
            }
            ins_init_assign(&expanded[n++], line);
        }
    }

    if (b->instr_count - 1 - at + n > FGIR_MAX_INSTR) {
        snprintf(err, err_size, "lowered call too large");
        return -1;
    }
    memmove(&b->instr[at + n], &b->instr[at + 1], (size_t)(b->instr_count - at - 1) * sizeof *b->instr);
    memcpy(&b->instr[at], expanded, (size_t)n * sizeof *b->instr);
    b->instr_count = b->instr_count - 1 + n;
    {
        int j;
        for (j = 0; j < b->instr_count; j++) {
            b->instr[j].index = j + 1;
        }
    }
    return 0;
}

int flat_func_lower_program(FgirProgram *prog, char *err, size_t err_size) {
    int bi;

    if (!prog) {
        return -1;
    }
    for (bi = 0; bi < prog->block_count; bi++) {
        FgirBlock *b = &prog->blocks[bi];
        int ii = 0;
        while (ii < b->instr_count) {
            if (b->instr[ii].kind == FGIR_FUNC_CALL) {
                if (lower_one_call(prog, b, ii, &b->instr[ii], err, err_size) != 0) {
                    return -1;
                }
                ii += 1;
                continue;
            }
            ii++;
        }
    }
    return 0;
}

int flat_func_validate_program(const FgirProgram *prog, char *err, size_t err_size) {
    int bi, ii;

    for (bi = 0; bi < prog->block_count; bi++) {
        const FgirBlock *b = &prog->blocks[bi];
        for (ii = 0; ii < b->instr_count; ii++) {
            const FgirInstr *ins = &b->instr[ii];
            if (ins->kind == FGIR_FUNC_CALL) {
                snprintf(err, err_size, "unlowered function call");
                return -1;
            }
        }
    }
    if (strcmp(prog->default_executor, FLAT_FUNC_RESERVED_INSTR_VAR) == 0) {
        snprintf(err, err_size, "reserved name");
        return -1;
    }
    return 0;
}
