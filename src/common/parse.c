#include "fgir.h"
#include "flat_value.h"
#include "flat_func.h"
#include "flat_func_tag.h"
#include "flat_line_label.h"
#include "flat_link.h"
#include "flat_native.h"
#include "flat_str.h"
#include "flat_schema.h"
#include "flat_jump_block.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) {
        *e-- = '\0';
    }
    return s;
}

static int line_starts_with(const char *p, const char *prefix) {
    return strncmp(p, prefix, strlen(prefix)) == 0;
}

static int line_is_block_comment_open(const char *p) {
    return line_starts_with(p, "--<");
}

static int line_is_block_comment_close(const char *p) {
    return line_starts_with(p, "-->");
}

static int line_is_line_comment(const char *p) {
    return line_starts_with(p, "--") && !line_is_block_comment_open(p);
}

static int parse_schema_missing_colon_err(const char *path, int line_no, const char *line,
                                        char *err, size_t err_size) {
    char sname[FLAT_SCHEMA_NAME_MAX + 1];

    if (!flat_schema_header_missing_colon(line, sname, sizeof sname)) {
        return 0;
    }
    snprintf(err, err_size, "%s:%d: expected ':' after schema <%s>", path, line_no, sname);
    return 1;
}

static int try_begin_schema(FgirProgram *out, const char *path, int line_no, const char *line,
                            int *schema_cur, char *err, size_t err_size) {
    char sname[FLAT_SCHEMA_NAME_MAX + 1];

    if (flat_schema_parse_header_line(line, sname, sizeof sname) == 0) {
        if (out->schema_count >= FGIR_SCHEMA_MAX) {
            snprintf(err, err_size, "%s:%d: too many schemas", path, line_no);
            return -1;
        }
        *schema_cur = out->schema_count++;
        snprintf(out->schemas[*schema_cur].name, sizeof out->schemas[*schema_cur].name, "%s", sname);
        out->schemas[*schema_cur].field_count = 0;
        return 1;
    }
    if (parse_schema_missing_colon_err(path, line_no, line, err, err_size)) {
        return -1;
    }
    return 0;
}

static int starts_with_kw(const char *s, const char *kw) {
    size_t n = strlen(kw);
    return strncmp(s, kw, n) == 0 &&
           (s[n] == '\0' || isspace((unsigned char)s[n]) || s[n] == '(');
}

int flat_parse_free_varname(const char *line, char *var_out, size_t var_sz) {
    const char *p;
    const char *start;
    const char *end;

    if (!line || !var_out || var_sz == 0) {
        return -1;
    }
    if (!starts_with_kw(line, "free")) {
        return -1;
    }
    p = line + 4;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '(') {
        return -1;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    start = p;
    while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '-')) {
        p++;
    }
    end = p;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != ')' || end == start) {
        return -1;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '\0') {
        return -1;
    }
    snprintf(var_out, var_sz, "%.*s", (int)(end - start), start);
    if (strcmp(var_out, "null") == 0 || strcmp(var_out, "true") == 0 || strcmp(var_out, "false") == 0) {
        return -1;
    }
    return 0;
}

int flat_parse_freeze_varname(const char *line, char *var_out, size_t var_sz) {
    const char *p;
    const char *start;
    const char *end;

    if (!line || !var_out || var_sz == 0) {
        return -1;
    }
    if (!starts_with_kw(line, "freeze")) {
        return -1;
    }
    p = line + 6;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '(') {
        return -1;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    start = p;
    while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '-')) {
        p++;
    }
    end = p;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != ')' || end == start) {
        return -1;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '\0') {
        return -1;
    }
    snprintf(var_out, var_sz, "%.*s", (int)(end - start), start);
    if (strcmp(var_out, "null") == 0 || strcmp(var_out, "true") == 0 || strcmp(var_out, "false") == 0) {
        return -1;
    }
    return 0;
}

int flat_parse_panic_reset_line(const char *line) {
    const char *p;

    if (!line) {
        return -1;
    }
    if (strncmp(line, "panicReset", 10) != 0) {
        return -1;
    }
    p = line + 10;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '(') {
        return -1;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != ')') {
        return -1;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return (*p == '\0') ? 0 : -1;
}

int flat_parse_panic_reason_bind(const char *line, char *var_out, size_t var_sz) {
    const char *p;
    const char *arrow;
    const char *start;
    const char *end;

    if (!line || !var_out || var_sz == 0) {
        return -1;
    }
    if (strncmp(line, "panicReason", 11) != 0) {
        return -1;
    }
    p = line + 11;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '(') {
        return -1;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != ')') {
        return -1;
    }
    p++;
    arrow = strstr(p, "->");
    if (!arrow) {
        return -1;
    }
    p = arrow + 2;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    start = p;
    while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '-')) {
        p++;
    }
    end = p;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '\0' || end == start) {
        return -1;
    }
    snprintf(var_out, var_sz, "%.*s", (int)(end - start), start);
    if (strcmp(var_out, "null") == 0 || strcmp(var_out, "true") == 0 || strcmp(var_out, "false") == 0) {
        return -1;
    }
    return 0;
}

static int is_free_line(const char *line) {
    char tmp[64];
    return flat_parse_free_varname(line, tmp, sizeof tmp) == 0;
}

static int is_freeze_line(const char *line) {
    char tmp[64];
    return flat_parse_freeze_varname(line, tmp, sizeof tmp) == 0;
}

static int is_panic_reset_line(const char *line) {
    return flat_parse_panic_reset_line(line) == 0;
}

static int is_panic_reason_line(const char *line) {
    char tmp[64];
    return flat_parse_panic_reason_bind(line, tmp, sizeof tmp) == 0;
}

static int assign_rhs_is_native_call(const char *line) {
    char buf[FGIR_MAX_LINE];
    char *bind;
    char *value;
    FlatAssignSplitForm form;

    snprintf(buf, sizeof buf, "%s", line);
    if (flat_assign_split_line(buf, &bind, &value, &form) != 0) {
        return 0;
    }
    return flat_native_is_call_line(value);
}

static int parse_line_modifier(char *line, FgirLineMod *mod_out, const char *path, int line_no,
                               char *err, size_t err_size) {
    char *p = trim(line);

    *mod_out = FGIR_LINE_MOD_NONE;
    if (*p != '?' && *p != '~') {
        memmove(line, p, strlen(p) + 1);
        return 0;
    }
    if (p[1] == '?' || p[1] == '~') {
        snprintf(err, err_size, "%s:%d: line may have only one modifier (? or ~)", path, line_no);
        return -1;
    }
    *mod_out = (*p == '?') ? FGIR_LINE_MOD_Q : FGIR_LINE_MOD_TILDE;
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    memmove(line, p, strlen(p) + 1);
    return 0;
}

static int line_mod_allowed(FgirInstrKind kind, const char *text) {
    if (kind == FGIR_IO) {
        return 1;
    }
    if (kind == FGIR_ASSIGN && assign_rhs_is_native_call(text)) {
        return 1;
    }
    return 0;
}

/* Typed bind uses `:<schema>`; do not treat those angle brackets as relational ops. */
static int schema_lt_at(const char *line, const char *p) {
    while (p > line && isspace((unsigned char)p[-1])) {
        p--;
    }
    return p > line && p[-1] == ':';
}

static size_t skip_schema_angle_token(const char *line, size_t i) {
    if (!line[i] || line[i] != '<' || !schema_lt_at(line, &line[i])) {
        return i;
    }
    i++;
    while (line[i] && line[i] != '>') {
        i++;
    }
    if (line[i] == '>') {
        i++;
    }
    return i;
}

/* Comparison guards: ==, != is not a guard, <, >, <=, >= (outside quotes). */
static int line_is_comparison_guard(const char *line) {
    size_t i;
    int in_s = 0;
    int in_d = 0;

    if (!line) {
        return 0;
    }
    for (i = 0; line[i]; i++) {
        if (line[i] == '\'' && !in_d) {
            in_s = !in_s;
        } else if (line[i] == '"' && !in_s) {
            in_d = !in_d;
        } else if (!in_s && !in_d) {
            size_t after = skip_schema_angle_token(line, i);
            if (after > i) {
                i = after - 1;
                continue;
            }
            if (line[i] == '=' && line[i + 1] == '=') {
                return 1;
            }
            if (line[i] == '>' && line[i + 1] == '=') {
                return 1;
            }
            if (line[i] == '<' && line[i + 1] == '=') {
                return 1;
            }
            if (line[i] == '>' && line[i + 1] != '=') {
                return 1;
            }
            if (line[i] == '<' && line[i + 1] != '=') {
                return 1;
            }
        }
    }
    return 0;
}

static FgirInstrKind classify(const char *line) {
    if (strcmp(line, "OK") == 0) {
        return FGIR_OK;
    }
    if (strcmp(line, "FAIL") == 0) {
        return FGIR_FAIL;
    }
    if (strcmp(line, "PANIC") == 0) {
        return FGIR_RAISE_PANIC;
    }
    if (strcmp(line, "next") == 0) {
        return FGIR_NEXT;
    }
    if (starts_with_kw(line, "return")) {
        return FGIR_RETURN;
    }
    if (starts_with_kw(line, "foreach")) {
        return FGIR_FOREACH;
    }
    if (starts_with_kw(line, "for")) {
        return FGIR_FOR;
    }
    if (starts_with_kw(line, "call block")) {
        return FGIR_CALL_BLOCK;
    }
    if (starts_with_kw(line, "call")) {
        return FGIR_CALL;
    }
    if (starts_with_kw(line, "jump")) {
        return FGIR_JUMP;
    }
    if (is_panic_reason_line(line)) {
        return FGIR_PANIC_REASON;
    }
    if (is_panic_reset_line(line)) {
        return FGIR_PANIC_RESET;
    }
    if (strstr(line, "->") != NULL) {
        return FGIR_ASSIGN;
    }
    if (flat_native_is_call_line(line)) {
        return FGIR_IO;
    }
    if (is_freeze_line(line)) {
        return FGIR_FREEZE;
    }
    if (is_free_line(line)) {
        return FGIR_FREE;
    }
    if (line_is_comparison_guard(line)) {
        return FGIR_GUARD;
    }
    if (strchr(line, '=') != NULL) {
        return FGIR_ASSIGN;
    }
    return FGIR_ASSIGN;
}

static int parse_block_header(char *line, FgirBlock *b, char *err, size_t err_size) {
    char *p = line + 1;
    char *paren = strchr(p, '(');
    int override_set = 0;
    uint32_t override = 0;

    b->param_count = 0;
    b->jump_block_max_override = -1;
    if (flat_block_header_strip_jump_block_max(line, &override_set, &override, err, err_size) != 0) {
        return -1;
    }
    if (override_set) {
        b->jump_block_max_override = (int32_t)override;
    }
    if (paren) {
        *paren = '\0';
        snprintf(b->label, sizeof b->label, "#%s", trim(p));
        {
            char args[256];
            char *end = strchr(paren + 1, ')');
            if (!end) {
                return -1;
            }
            *end = '\0';
            snprintf(args, sizeof args, "%s", paren + 1);
            {
                char *ap = args;
                char *tok;
                while (b->param_count < FGIR_MAX_PARAMS &&
                       (tok = flat_str_next(&ap, ',')) != NULL) {
                    snprintf(b->params[b->param_count], 64, "%s", trim(tok));
                    b->param_count++;
                }
            }
        }
    } else {
        snprintf(b->label, sizeof b->label, "#%s", trim(p));
    }
    return 0;
}

static void parse_call_block_extras(FgirInstr *ins) {
    char buf[FGIR_MAX_LINE];
    char *expr;
    char *arrow;

    snprintf(buf, sizeof buf, "%s", ins->text);
    expr = buf + strlen("call block");
    while (*expr == ' ' || *expr == '\t') {
        expr++;
    }
    arrow = strstr(expr, "->");
    if (arrow) {
        snprintf(ins->call_out, sizeof ins->call_out, "%s", trim(arrow + 2));
        *arrow = '\0';
        trim(expr);
    } else {
        ins->call_out[0] = '\0';
    }
    snprintf(ins->call_target, sizeof ins->call_target, "%s", expr);
}

static void parse_call_extras(FgirInstr *ins) {
    const char *s = ins->text;
    const char *hash = strstr(s, "#");
    const char *arrow = strstr(s, "->");
    if (hash) {
        const char *start = hash;
        const char *end = start + 1;
        while (*end && *end != '(' && *end != ' ' && *end != '\t') {
            end++;
        }
        snprintf(ins->call_target, sizeof ins->call_target, "%.*s", (int)(end - start), start);
    }
    if (arrow) {
        snprintf(ins->call_out, sizeof ins->call_out, "%s", trim((char *)(arrow + 2)));
    }
}

static void parse_io_bind(FgirInstr *ins) {
    char *arrow = strstr(ins->text, "->");
    if (!arrow) {
        ins->call_out[0] = '\0';
        return;
    }
    {
        char *bind = trim(arrow + 2);
        *arrow = '\0';
        trim(ins->text);
        snprintf(ins->call_out, sizeof ins->call_out, "%s", bind);
    }
}

static void strip_block_quotes(char *t) {
    size_t n;
    if (!t || !t[0]) {
        return;
    }
    if (t[0] == '\'' || t[0] == '"') {
        n = strlen(t);
        if (n >= 2 && t[n - 1] == t[0]) {
            t[n - 1] = '\0';
            memmove(t, t + 1, n - 1);
        }
    }
}

static void parse_loop_third_arg(const char *raw, char *dest, size_t dest_sz) {
    char buf[FGIR_MAX_LABEL];

    if (!raw || !dest || dest_sz == 0) {
        if (dest && dest_sz > 0) {
            dest[0] = '\0';
        }
        return;
    }
    snprintf(buf, sizeof buf, "%s", raw);
    trim(buf);
    strip_block_quotes(buf);
    if (buf[0] == '#') {
        snprintf(dest, dest_sz, "%s", buf);
        return;
    }
    snprintf(dest, dest_sz, "%s", buf);
}

static void parse_for_extras(FgirInstr *ins) {
    /* for(ntimes, i, '#block') */
    const char *s = ins->text;
    const char *lp = strchr(s, '(');
    const char *rp = strrchr(s, ')');
    char inner[FGIR_MAX_LINE];
    char *a;
    char *b;
    char *c;

    if (!lp || !rp || rp <= lp) {
        return;
    }
    snprintf(inner, sizeof inner, "%.*s", (int)(rp - lp - 1), lp + 1);
    if (flat_str_split_three(inner, &a, &b, &c) != 0) {
        return;
    }
    snprintf(ins->for_limit, sizeof ins->for_limit, "%s", a);
    snprintf(ins->foreach_item, sizeof ins->foreach_item, "%s", b);
    parse_loop_third_arg(c, ins->foreach_block, sizeof ins->foreach_block);
}

static void parse_foreach_extras(FgirInstr *ins) {
    /* foreach(coll, item, '#block') */
    const char *s = ins->text;
    const char *lp = strchr(s, '(');
    const char *rp = strrchr(s, ')');
    char inner[FGIR_MAX_LINE];
    char *a;
    char *b;
    char *c;

    if (!lp || !rp || rp <= lp) {
        return;
    }
    snprintf(inner, sizeof inner, "%.*s", (int)(rp - lp - 1), lp + 1);
    if (flat_str_split_three(inner, &a, &b, &c) != 0) {
        return;
    }
    (void)a;
    snprintf(ins->foreach_item, sizeof ins->foreach_item, "%s", b);
    parse_loop_third_arg(c, ins->foreach_block, sizeof ins->foreach_block);
}

static int line_is_use_directive(const char *p) {
    return starts_with_kw(p, "use");
}

static int finish_open_def(FgirProgram *out, int open_fn_idx, const char *path, int line_no,
                           char *err, size_t err_size) {
    FgirFunction *fn;
    const FgirBlock *impl = NULL;
    int bi;

    if (open_fn_idx < 0 || open_fn_idx >= out->function_count) {
        return 0;
    }
    fn = &out->functions[open_fn_idx];
    for (bi = 0; bi < out->block_count; bi++) {
        if (strcmp(out->blocks[bi].label, fn->impl_label) == 0) {
            impl = &out->blocks[bi];
            break;
        }
    }
    if (!impl) {
        snprintf(err, err_size, "%s:%d: missing body for def %s", path, line_no, fn->name);
        return -1;
    }
    return flat_func_finish_def(fn, impl, out->func_var_slots_manual, err, err_size);
}

int flat_parse_unit(const char *path, int allow_main, FgirProgram *out, char *err, size_t err_size) {
    FILE *f;
    char line[FGIR_MAX_LINE + 8];
    int line_no = 0;
    int cur = -1;
    int in_block_comment = 0;
    int in_header = 1;
    int open_fn_idx = -1;
    int open_fn_sugar_base = -1;
    int schema_cur = -1;
    int hdr;

    if (!path || !out) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    out->main_index = -1;
    flat_func_meta_init(out);

    f = fopen(path, "r");
    if (!f) {
        snprintf(err, err_size, "cannot open %s", path);
        return -1;
    }

    while (fgets(line, sizeof line, f)) {
        char *p;
        line_no++;
        if (schema_cur >= 0 && (line[0] == ' ' || line[0] == '\t')) {
            FgirSchemaDecl *sd = &out->schemas[schema_cur];
            char fkey[FGIR_OBJ_KEY_MAX + 1];
            char fspec[FGIR_SCHEMA_TYPE_SPEC_MAX + 1];
            char body[FGIR_MAX_LINE + 8];
            char *bp;

            if (in_block_comment) {
                bp = trim(line);
                if (line_is_block_comment_close(bp)) {
                    in_block_comment = 0;
                } else if (line_is_block_comment_open(bp)) {
                    snprintf(err, err_size, "%s:%d: nested block comment", path, line_no);
                    fclose(f);
                    return -1;
                }
                continue;
            }
            snprintf(body, sizeof body, "%s", line);
            bp = trim(body);
            if (line_is_block_comment_open(bp)) {
                in_block_comment = 1;
                continue;
            }
            if (flat_schema_body_line_ignorable(bp)) {
                continue;
            }
            if (flat_schema_parse_field_line(line, fkey, sizeof fkey, fspec, sizeof fspec) != 0 ||
                sd->field_count >= FGIR_SCHEMA_FIELD_MAX) {
                snprintf(err, err_size, "%s:%d: bad schema field", path, line_no);
                fclose(f);
                return -1;
            }
            snprintf(sd->field_keys[sd->field_count], sizeof sd->field_keys[sd->field_count], "%s",
                     fkey);
            snprintf(sd->field_specs[sd->field_count], sizeof sd->field_specs[sd->field_count],
                     "%s", fspec);
            sd->field_count++;
            continue;
        }
        p = trim(line);
        if (in_block_comment) {
            if (line_is_block_comment_close(p)) {
                in_block_comment = 0;
                continue;
            }
            if (line_is_block_comment_open(p)) {
                snprintf(err, err_size, "%s:%d: nested block comment", path, line_no);
                fclose(f);
                return -1;
            }
            continue;
        }
        if (*p == '\0') {
            continue;
        }
        if (line_is_block_comment_open(p)) {
            in_block_comment = 1;
            continue;
        }
        if (line_is_line_comment(p)) {
            continue;
        }
        if (cur < 0) {
            int sr = try_begin_schema(out, path, line_no, p, &schema_cur, err, err_size);
            if (sr < 0) {
                fclose(f);
                return -1;
            }
            if (sr > 0) {
                continue;
            }
            if (schema_cur >= 0) {
                schema_cur = -1;
            }
        }
        if (in_header) {
            int sr = try_begin_schema(out, path, line_no, p, &schema_cur, err, err_size);
            if (sr < 0) {
                fclose(f);
                return -1;
            }
            if (sr > 0) {
                continue;
            }
            if (line_is_use_directive(p)) {
                continue;
            }
            {
                uint32_t jbmax = 0;
                int jr = flat_parse_jump_block_max_directive(p, &jbmax, err, err_size);
                if (jr < 0) {
                    char detail[256];
                    snprintf(detail, sizeof detail, "%s", err);
                    snprintf(err, err_size, "%s:%d: %s", path, line_no, detail);
                    fclose(f);
                    return -1;
                }
                if (jr > 0) {
                    if (out->jump_block_max_global_set) {
                        snprintf(err, err_size, "%s:%d: duplicate jump:blockMax", path, line_no);
                        fclose(f);
                        return -1;
                    }
                    out->jump_block_max_global = jbmax;
                    out->jump_block_max_global_set = 1;
                    continue;
                }
            }
            if (strncmp(p, "mod(", 4) == 0 || strncmp(p, "export", 6) == 0 ||
                strncmp(p, "func:", 5) == 0) {
                if (flat_func_parse_header_line(p, out, path, line_no, err, err_size) < 0) {
                    fclose(f);
                    return -1;
                }
                continue;
            }
            hdr = flat_func_parse_header_line(p, out, path, line_no, err, err_size);
            if (hdr < 0) {
                fclose(f);
                return -1;
            }
            if (hdr == 0) {
                continue;
            }
            if (starts_with_kw(p, "def")) {
                in_header = 0;
            } else if (*p != '#') {
                snprintf(err, err_size, "%s:%d: expected use, func header, def, or block", path,
                         line_no);
                fclose(f);
                return -1;
            } else {
                in_header = 0;
            }
        } else if (line_is_use_directive(p)) {
            snprintf(err, err_size, "%s:%d: use must appear before any block", path, line_no);
            fclose(f);
            return -1;
        }
        if (strncmp(p, "func:", 5) == 0) {
            if (finish_open_def(out, open_fn_idx, path, line_no, err, err_size) != 0) {
                fclose(f);
                return -1;
            }
            open_fn_idx = -1;
            open_fn_sugar_base = -1;
            if (flat_func_parse_header_line(p, out, path, line_no, err, err_size) < 0) {
                fclose(f);
                return -1;
            }
            continue;
        }
        if (starts_with_kw(p, "def")) {
            FgirFunction *fn;

            if (finish_open_def(out, open_fn_idx, path, line_no, err, err_size) != 0) {
                fclose(f);
                return -1;
            }
            open_fn_idx = -1;
            open_fn_sugar_base = -1;
            if (out->function_count >= FGIR_MAX_FUNCTIONS) {
                snprintf(err, err_size, "%s:%d: too many functions", path, line_no);
                fclose(f);
                return -1;
            }
            fn = &out->functions[out->function_count];
            if (flat_func_parse_def_header(p, fn, out, err, err_size) != 0) {
                char detail[FGIR_MAX_LINE];

                snprintf(detail, sizeof detail, "%s", err);
                snprintf(err, err_size, "%s:%d: %s", path, line_no, detail);
                fclose(f);
                return -1;
            }
            open_fn_idx = out->function_count++;
            open_fn_sugar_base = open_fn_idx;
            if (flat_func_check_new_def_overload(out, open_fn_idx, err, err_size) != 0) {
                out->function_count--;
                open_fn_sugar_base = -1;
                char detail[FGIR_MAX_LINE];

                snprintf(detail, sizeof detail, "%s", err);
                snprintf(err, err_size, "%s:%d: %s", path, line_no, detail);
                fclose(f);
                return -1;
            }
            if (out->block_count >= FGIR_MAX_BLOCKS) {
                snprintf(err, err_size, "%s:%d: too many blocks", path, line_no);
                fclose(f);
                return -1;
            }
            cur = out->block_count++;
            memset(&out->blocks[cur], 0, sizeof out->blocks[cur]);
            out->blocks[cur].jump_block_max_override = -1;
            snprintf(out->blocks[cur].label, FGIR_MAX_LABEL, "%s", fn->impl_label);
            continue;
        }
        if (*p == '#') {
            schema_cur = -1;
            if (finish_open_def(out, open_fn_idx, path, line_no, err, err_size) != 0) {
                fclose(f);
                return -1;
            }
            open_fn_idx = -1;
            open_fn_sugar_base = -1;
            if (out->block_count >= FGIR_MAX_BLOCKS) {
                snprintf(err, err_size, "%s:%d: too many blocks", path, line_no);
                fclose(f);
                return -1;
            }
            cur = out->block_count++;
            if (parse_block_header(p, &out->blocks[cur], err, err_size) != 0) {
                char detail[256];
                snprintf(detail, sizeof detail, "%s", err[0] ? err : "bad block header");
                snprintf(err, err_size, "%s:%d: %s", path, line_no, detail);
                fclose(f);
                return -1;
            }
            if (strcmp(out->blocks[cur].label, "#main") == 0) {
                if (!allow_main) {
                    snprintf(err, err_size, "%s:%d: #main not allowed in imported module", path, line_no);
                    fclose(f);
                    return -1;
                }
                if (out->main_index >= 0) {
                    snprintf(err, err_size, "%s:%d: duplicate #main", path, line_no);
                    fclose(f);
                    return -1;
                }
                out->main_index = cur;
            }
            continue;
        }
        if (open_fn_idx >= 0 && open_fn_sugar_base >= 0 && flat_func_is_def_clause_line(p)) {
            const FgirFunction *proto;
            FgirFunction *fn;

            if (finish_open_def(out, open_fn_idx, path, line_no, err, err_size) != 0) {
                fclose(f);
                return -1;
            }
            if (out->function_count >= FGIR_MAX_FUNCTIONS) {
                snprintf(err, err_size, "%s:%d: too many functions", path, line_no);
                fclose(f);
                return -1;
            }
            proto = &out->functions[open_fn_sugar_base];
            fn = &out->functions[out->function_count];
            if (flat_func_parse_def_clause_line(p, fn, proto, out, err, err_size) != 0) {
                char detail[FGIR_MAX_LINE];

                snprintf(detail, sizeof detail, "%s", err);
                snprintf(err, err_size, "%s:%d: %s", path, line_no, detail);
                fclose(f);
                return -1;
            }
            open_fn_idx = out->function_count++;
            if (flat_func_check_new_def_overload(out, open_fn_idx, err, err_size) != 0) {
                out->function_count--;
                char detail[FGIR_MAX_LINE];

                snprintf(detail, sizeof detail, "%s", err);
                snprintf(err, err_size, "%s:%d: %s", path, line_no, detail);
                fclose(f);
                return -1;
            }
            if (out->block_count >= FGIR_MAX_BLOCKS) {
                snprintf(err, err_size, "%s:%d: too many blocks", path, line_no);
                fclose(f);
                return -1;
            }
            cur = out->block_count++;
            memset(&out->blocks[cur], 0, sizeof(out->blocks[cur]));
            out->blocks[cur].jump_block_max_override = -1;
            snprintf(out->blocks[cur].label, FGIR_MAX_LABEL, "%s", fn->impl_label);
            continue;
        }
        if (cur < 0) {
            snprintf(err, err_size, "%s:%d: instruction outside block", path, line_no);
            fclose(f);
            return -1;
        }
        {
            FgirBlock *b = &out->blocks[cur];
            FgirInstr *ins;
            if (b->instr_count >= FGIR_MAX_INSTR) {
                snprintf(err, err_size, "%s:%d: too many instructions", path, line_no);
                fclose(f);
                return -1;
            }
            ins = &b->instr[b->instr_count];
            memset(ins, 0, sizeof *ins);
            {
                char work[FGIR_MAX_LINE + 8];
                snprintf(work, sizeof work, "%s", p);
                if (parse_line_modifier(work, &ins->line_mod, path, line_no, err, err_size) < 0) {
                    fclose(f);
                    return -1;
                }
                if (flat_line_label_strip_eol(work, ins->line_label, sizeof ins->line_label,
                                                path, line_no, err, err_size) < 0) {
                    fclose(f);
                    return -1;
                }
                if (strlen(work) >= sizeof ins->text) {
                    snprintf(err, err_size, "%s:%d: line too long", path, line_no);
                    fclose(f);
                    return -1;
                }
                memcpy(ins->text, work, strlen(work) + 1);
            }
            ins->kind = classify(ins->text);
            (void)flat_func_try_classify_call(ins->text, out, ins);
            if (ins->line_mod != FGIR_LINE_MOD_NONE && !line_mod_allowed(ins->kind, ins->text)) {
                snprintf(err, err_size,
                         "%s:%d: line modifier (? or ~) requires a native call on this line", path,
                         line_no);
                fclose(f);
                return -1;
            }
            ins->index = b->instr_count + 1;
            if (ins->kind == FGIR_CALL_BLOCK) {
                parse_call_block_extras(ins);
                if (ins->call_target[0] == '\0') {
                    snprintf(err, err_size, "%s:%d: call block requires a block id expression",
                             path, line_no);
                    fclose(f);
                    return -1;
                }
            }
            if (ins->kind == FGIR_CALL || ins->kind == FGIR_JUMP) {
                parse_call_extras(ins);
            }
            if (ins->kind == FGIR_FOREACH) {
                parse_foreach_extras(ins);
            }
            if (ins->kind == FGIR_FOR) {
                parse_for_extras(ins);
            }
            if (ins->kind == FGIR_IO) {
                parse_io_bind(ins);
            }
            b->instr_count++;
        }
    }
    fclose(f);

    if (in_block_comment) {
        snprintf(err, err_size, "%s: unclosed block comment", path);
        return -1;
    }

    if (finish_open_def(out, open_fn_idx, path, line_no, err, err_size) != 0) {
        return -1;
    }
    if (allow_main && out->main_index < 0) {
        snprintf(err, err_size, "%s: missing #main", path);
        return -1;
    }
    if (flat_line_label_validate_unit(out, path, err, err_size) != 0) {
        return -1;
    }
    return 0;
}

int flat_parse_file(const char *path, FgirProgram *out, char *err, size_t err_size) {
    FlatLinkOptions opts = {0};
    return flat_link_program(path, &opts, out, NULL, err, err_size);
}

void flat_fgir_dump(const FgirProgram *prog, FILE *out) {
    int i, j;
    fprintf(out, "FGIR blocks=%d main_index=%d\n", prog->block_count, prog->main_index);
    for (i = 0; i < prog->block_count; i++) {
        const FgirBlock *b = &prog->blocks[i];
        fprintf(out, "block %s (%d instr)\n", b->label, b->instr_count);
        for (j = 0; j < b->instr_count; j++) {
            fprintf(out, "  %d: %s\n", b->instr[j].index, b->instr[j].text);
        }
    }
}
