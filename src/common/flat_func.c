#include "flat_func.h"
#include "flat_native.h"
#include "flat_value.h"
#include "flat_str.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_ident_char(int c) {
    return isalnum((unsigned char)c) || c == '_';
}

static int starts_with_kw(const char *s, const char *kw) {
    size_t n = strlen(kw);
    return strncmp(s, kw, n) == 0 &&
           (s[n] == '\0' || isspace((unsigned char)s[n]) || s[n] == '(' || s[n] == ':');
}

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
    if (strncmp(p, "mod(", 4) == 0) {
        return parse_mod_line(p, prog, err, err_size);
    }
    if (line_starts_with(p, "export")) {
        return parse_export_line(p, prog, err, err_size);
    }
    return 1; /* not a func header */
}

static int split_param_list(char *inner, char out[][64], int max_out, int *out_count) {
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
                        snprintf(out[count], 64, "%s", t);
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

int flat_func_parse_def_header(const char *line, FgirFunction *fn, FgirProgram *prog, char *err,
                               size_t err_size) {
    const char *p = line + 3;
    const char *lp;
    const char *rp;
    const char *arrow;
    char inner[FGIR_MAX_LINE];
    char name_buf[FGIR_MAX_FUNC_NAME];
    size_t n;

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
    snprintf(inner, sizeof inner, "%.*s", (int)(rp - lp - 1), lp + 1);
    if (split_param_list(inner, fn->params, FGIR_MAX_FUNC_PARAMS, &fn->param_count) != 0) {
        snprintf(err, err_size, "def: too many parameters");
        return -1;
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
        char ret[64];
        size_t rl = 0;
        while (arrow[rl] && !isspace((unsigned char)arrow[rl])) {
            rl++;
        }
        if (rl == 0 || rl >= sizeof ret) {
            snprintf(err, err_size, "def: bad return name");
            return -1;
        }
        memcpy(ret, arrow, rl);
        ret[rl] = '\0';
        snprintf(fn->return_name, sizeof fn->return_name, "%s", ret);
    }
    snprintf(fn->executor_label, sizeof fn->executor_label, "%s", prog->default_executor);
    snprintf(fn->impl_label, sizeof fn->impl_label, "%s%s", FLAT_FUNC_IMPL_PREFIX, fn->name);
    fn->is_global_executor = (strstr(fn->executor_label, "global.scope") != NULL) ? 1 : 0;
    return 0;
}

static int varset_add_name(char names[][64], int *count, int max, const char *raw) {
    int i;
    char key[64];

    snprintf(key, sizeof key, "%s", raw);
    if (!key[0]) {
        return 0;
    }
    for (i = 0; i < *count; i++) {
        if (strcmp(names[i], key) == 0) {
            return 0;
        }
    }
    if (*count >= max) {
        return 1;
    }
    snprintf(names[*count], 64, "%s", key);
    (*count)++;
    return 0;
}

int flat_func_finish_def(FgirFunction *fn, const FgirBlock *impl, int manual_slots, char *err,
                         size_t err_size) {
    int scan = 0;
    int pi;

    {
        char names[256][64];
        int count = 0;
        for (pi = 0; pi < fn->param_count; pi++) {
            varset_add_name(names, &count, 256, fn->params[pi]);
        }
        for (pi = 0; pi < impl->instr_count; pi++) {
            const FgirInstr *ins = &impl->instr[pi];
            if (ins->kind == FGIR_ASSIGN) {
                char buf[FGIR_MAX_LINE];
                char *bind;
                char *val;
                FlatAssignSplitForm form;

                snprintf(buf, sizeof buf, "%s", ins->text);
                if (flat_assign_split_line(buf, &bind, &val, &form) == 0 && bind[0]) {
                    varset_add_name(names, &count, 256, bind);
                }
            }
        }
        scan = count;
    }
    if (fn->is_global_executor) {
        fn->func_vcx_slots = 0;
        return 0;
    }
    if (manual_slots >= 0 && manual_slots < scan) {
        snprintf(err, err_size, "func.varSlots.underflow (%d < %d)", manual_slots, scan);
        return -1;
    }
  fn->func_vcx_slots = (manual_slots >= 0) ? (uint32_t)manual_slots : (uint32_t)scan;
    if (fn->func_vcx_slots == 0 && !fn->is_global_executor) {
        fn->func_vcx_slots = (uint32_t)(scan > 0 ? scan : 4);
    }
    if (!fn->is_global_executor) {
        fn->func_vcx_slots += 2u; /* executor: __run_block, __calle_id */
    }
    return 0;
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
    if (lp) {
        const char *dot = NULL;
        const char *q;
        int dot_count = 0;
        for (q = line; q < lp; q++) {
            if (*q == '.') {
                dot = q;
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
    return -1;
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
        if (!flat_func_find(prog, name)) {
            return 0;
        }
        ins->kind = FGIR_FUNC_CALL;
        snprintf(ins->func_name, sizeof ins->func_name, "%s", name);
        if (args[0] != '\0') {
            snprintf(ins->func_args, sizeof ins->func_args, "%s", args);
        }
        snprintf(ins->func_ret, sizeof ins->func_ret, "%s", ret);
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
                if (!flat_func_find(prog, name)) {
                    return 0;
                }
                ins->kind = FGIR_FUNC_CALL;
                snprintf(ins->func_name, sizeof ins->func_name, "%s", name);
                snprintf(ins->func_args, sizeof ins->func_args, "%s", args);
                snprintf(ins->func_ret, sizeof ins->func_ret, "%s", bind);
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
        snprintf(slug, sizeof slug, "%s_%s", mod->mod_id, orig);
        if (export_has(mod, orig)) {
            snprintf(fn->name, sizeof fn->name, "%s.%s", import_as, orig);
        } else {
            snprintf(fn->name, sizeof fn->name, "__%s_%s", mod->mod_id, orig);
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

static int lower_one_call(FgirProgram *prog, FgirBlock *b, int at, const FgirInstr *call,
                          char *err, size_t err_size) {
    const FgirFunction *fn = flat_func_find(prog, call->func_name);
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
        snprintf(err, err_size, "unknown function %s", call->func_name);
        return -1;
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
    if (split_param_list(args_buf, arg_tokens, FGIR_MAX_FUNC_PARAMS, &arg_count) != 0) {
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

    for (i = 0; i < arg_count; i++) {
        snprintf(line, sizeof line, "__fl_tmp_%d = %s", i, arg_parts[i]);
        ins_init_assign(&expanded[n++], line);
    }

    snprintf(line, sizeof line, "__fl_targetBlock = %u", (unsigned)target_id);
    ins_init_assign(&expanded[n++], line);
    snprintf(line, sizeof line, "__fl_calleVcx = %s", FLAT_FUNC_RESERVED_CALLE_VAR);
    ins_init_assign(&expanded[n++], line);
    if (build_param_name_list(fn, plist, sizeof plist) != 0) {
        snprintf(err, err_size, "param list too long");
        return -1;
    }
    snprintf(line, sizeof line, "__fl_funcParamNames = %s", plist);
    ins_init_assign(&expanded[n++], line);
    snprintf(line, sizeof line, "__fl_returnVar = '%s'", fn->return_name);
    ins_init_assign(&expanded[n++], line);
    snprintf(line, sizeof line, "__fl_calleReturnVar = '%s'", call->func_ret);
    ins_init_assign(&expanded[n++], line);
    snprintf(line, sizeof line, "__fl_funcVcxSlots = %u", (unsigned)fn->func_vcx_slots);
    ins_init_assign(&expanded[n++], line);
    if (build_param_in_names_list(fn->param_count, pin_list, sizeof pin_list) != 0) {
        snprintf(err, err_size, "param_in list too long");
        return -1;
    }
    snprintf(line, sizeof line, "__fl_paramSrcNames = %s", pin_list);
    ins_init_assign(&expanded[n++], line);
    for (i = 0; i < arg_count; i++) {
        snprintf(line, sizeof line, "param_in_%d = __fl_tmp_%d", i, i);
        ins_init_assign(&expanded[n++], line);
    }
    snprintf(line, sizeof line,
             "V.varsCopyByNames(%s, %s, "
             "['__fl_targetBlock','__fl_calleVcx','__fl_funcParamNames'], "
             "['targetBlock','calleVcx','funcParamNames'])",
             FLAT_FUNC_RESERVED_CALLE_VAR, FLAT_FUNC_RESERVED_INSTR_VAR);
    ins_init_assign(&expanded[n++], line);
    snprintf(line, sizeof line,
             "V.varsCopyByNames(%s, %s, "
             "['__fl_returnVar','__fl_calleReturnVar','__fl_funcVcxSlots','__fl_paramSrcNames'], "
             "['returnVar','calleReturnVar','funcVcxSlots','paramSrcNames'])",
             FLAT_FUNC_RESERVED_CALLE_VAR, FLAT_FUNC_RESERVED_INSTR_VAR);
    ins_init_assign(&expanded[n++], line);
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
        snprintf(line, sizeof line, "V.varsCopyByNames(%s, %s, %s, %s)", FLAT_FUNC_RESERVED_CALLE_VAR,
                 FLAT_FUNC_RESERVED_INSTR_VAR, src_list, dst_list);
        ins_init_assign(&expanded[n++], line);
    }
    ins_init_call(&expanded[n++], fn->executor_label);
    ins_init_assign(&expanded[n++], "V.set(1)");
    if (fn->is_global_executor) {
        snprintf(line, sizeof line, "%s = %s", call->func_ret, fn->return_name);
        ins_init_assign(&expanded[n++], line);
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
