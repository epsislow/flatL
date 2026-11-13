#include "flat_func_tag.h"
#include "flat_native_tag.h"
#include "flat_native_util.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLAT_TAG_MAX_TOKENS 16
#define FLAT_TAG_TOKEN_LEN 64

static int split_call_param_list(char *inner, char out[][64], int max_out, int *out_count) {
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

static int is_empty_string_literal(const char *expr) {
    char scratch[64];
    const char *p;

    if (!expr) {
        return 0;
    }
    snprintf(scratch, sizeof scratch, "%s", expr);
    p = flat_native_trim(scratch);
    if ((p[0] == '"' && p[1] == '"' && p[2] == '\0') ||
        (p[0] == '\'' && p[1] == '\'' && p[2] == '\0')) {
        return 1;
    }
    return 0;
}

static int string_from_tag_expr_compile(const char *tag_expr, char *out, size_t out_sz) {
    const char *p;
    char quote;

    if (!tag_expr || !out || out_sz == 0) {
        return -1;
    }
    p = tag_expr;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '"' && *p != '\'') {
        return -1;
    }
    quote = *p++;
    {
        size_t i = 0;
        while (*p && *p != quote) {
            if (i + 1 >= out_sz) {
                return -1;
            }
            out[i++] = *p++;
        }
        if (*p != quote) {
            return -1;
        }
        out[i] = '\0';
    }
    while (*++p) {
        if (!isspace((unsigned char)*p)) {
            return -1;
        }
    }
    return 0;
}

static int compare_tag_token_rows(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

static int tokenize_sort(char tokens[][FLAT_TAG_TOKEN_LEN], int *count, const char *spec) {
    const char *p;
    int n = 0;

    if (!spec || !count) {
        return -1;
    }
    *count = 0;
    p = spec;

    while (*p) {
        size_t i = 0;

        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        if (n >= FLAT_TAG_MAX_TOKENS) {
            return -1;
        }
        while (*p && !isspace((unsigned char)*p)) {
            if (i + 1 >= FLAT_TAG_TOKEN_LEN) {
                return -1;
            }
            tokens[n][i++] = *p++;
        }
        tokens[n][i] = '\0';
        n++;
    }

    if (n > 1) {
        qsort(tokens, (size_t)n, sizeof tokens[0], compare_tag_token_rows);
    }
    *count = n;
    return 0;
}

static int canonical_from_spec(const char *spec, char *canonical, size_t canon_sz) {
    char tokens[FLAT_TAG_MAX_TOKENS][FLAT_TAG_TOKEN_LEN];
    int n;
    int i;
    size_t pos = 0;

    if (!canonical || canon_sz == 0) {
        return -1;
    }
    canonical[0] = '\0';
    if (!spec || spec[0] == '\0') {
        return 0;
    }
    if (tokenize_sort(tokens, &n, spec) != 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        size_t len = strlen(tokens[i]);
        if (pos + len + (i > 0 ? 1u : 0u) + 1 > canon_sz) {
            return -1;
        }
        if (i > 0) {
            canonical[pos++] = ' ';
        }
        memcpy(canonical + pos, tokens[i], len);
        pos += len;
    }
    canonical[pos] = '\0';
    return 0;
}

int flat_func_tag_canonical_from_runtime_string(const char *spec, char *canonical, size_t canon_sz) {
    return canonical_from_spec(spec, canonical, canon_sz);
}

int flat_func_tag_canonical_from_literal(const char *tag_expr, char *canonical, size_t canon_sz,
                                         char *err, size_t err_sz) {
    char spec[256];

    if (!canonical || canon_sz == 0) {
        return -1;
    }
    canonical[0] = '\0';
    if (!tag_expr) {
        return 0;
    }
    while (*tag_expr && isspace((unsigned char)*tag_expr)) {
        tag_expr++;
    }
    if (*tag_expr == '\0') {
        return 0;
    }
    if (is_empty_string_literal(tag_expr)) {
        snprintf(err, err_sz, "type.tag.arg");
        return -1;
    }
    if (string_from_tag_expr_compile(tag_expr, spec, sizeof spec) != 0) {
        return 1; /* non-const */
    }
    if (spec[0] == '\0') {
        snprintf(err, err_sz, "type.tag.arg");
        return -1;
    }
    if (canonical_from_spec(spec, canonical, canon_sz) != 0) {
        snprintf(err, err_sz, "type.tag.not_accepted");
        return -1;
    }
    return 0;
}

int flat_func_tag_key_empty(int has_tag_section, const char *canonical) {
    if (!has_tag_section) {
        return 1;
    }
    return (!canonical || canonical[0] == '\0') ? 1 : 0;
}

int flat_func_overload_key_equal(const FgirFunction *a, const FgirFunction *b) {
    if (!a || !b) {
        return 0;
    }
    if (strcmp(a->name, b->name) != 0 || a->param_count != b->param_count) {
        return 0;
    }
    if (flat_func_tag_key_empty(a->def_tag_has_section, a->def_tag_canonical) !=
        flat_func_tag_key_empty(b->def_tag_has_section, b->def_tag_canonical)) {
        return 0;
    }
    return strcmp(a->def_tag_canonical, b->def_tag_canonical) == 0;
}

int flat_func_check_new_def_overload(const FgirProgram *prog, int new_ix, char *err, size_t err_sz) {
    int i;
    const FgirFunction *fn;

    if (!prog || new_ix < 0 || new_ix >= prog->function_count) {
        if (err && err_sz) {
            snprintf(err, err_sz, "def overload internal");
        }
        return -1;
    }
    fn = &prog->functions[new_ix];
    for (i = 0; i < prog->function_count; i++) {
        const FgirFunction *prev;

        if (i == new_ix) {
            continue;
        }
        prev = &prog->functions[i];
        if (strcmp(prev->name, fn->name) != 0) {
            continue;
        }
        if (flat_func_overload_key_equal(prev, fn)) {
            snprintf(err, err_sz, "type.def.overload.duplicate");
            return -1;
        }
    }
    return 0;
}

int flat_func_name_used(const FgirProgram *prog, const char *name) {
    int i;

    if (!prog || !name) {
        return 0;
    }
    for (i = 0; i < prog->function_count; i++) {
        if (strcmp(prog->functions[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int overload_matches(const FgirFunction *fn, const char *name, int argc, int call_empty,
                            const char *call_canonical) {
    if (strcmp(fn->name, name) != 0 || fn->param_count != argc) {
        return 0;
    }
  if (call_empty) {
        return flat_func_tag_key_empty(fn->def_tag_has_section, fn->def_tag_canonical);
    }
    return strcmp(fn->def_tag_canonical, call_canonical) == 0;
}

static int count_overloads_named_arity(const FgirProgram *prog, const char *name, int argc) {
    int i;
    int c = 0;

    for (i = 0; i < prog->function_count; i++) {
        if (strcmp(prog->functions[i].name, name) == 0 &&
            prog->functions[i].param_count == argc) {
            c++;
        }
    }
    return c;
}

int flat_func_resolve_call_index(const FgirProgram *prog, const char *name, int argc,
                                 int call_has_tag, const char *call_tag_expr, int *runtime_dynamic,
                                 char *err, size_t err_sz) {
    char canonical[256];
    char terr[64];
    int call_empty;
    int i;
    int match = -1;
    int rc;
    int tagged_only = 1;
    int has_empty_variant = 0;

    if (runtime_dynamic) {
        *runtime_dynamic = 0;
    }
    if (!prog || !name) {
        return -1;
    }
    canonical[0] = '\0';
    if (call_has_tag) {
        if (!call_tag_expr) {
            call_tag_expr = "";
        }
        terr[0] = '\0';
        rc = flat_func_tag_canonical_from_literal(call_tag_expr, canonical, sizeof canonical, terr,
                                                  sizeof terr);
        if (rc < 0) {
            snprintf(err, err_sz, "%s", terr[0] ? terr : "type.tag.arg");
            return -1;
        }
        if (rc > 0) {
            if (runtime_dynamic) {
                *runtime_dynamic = 1;
            }
            if (count_overloads_named_arity(prog, name, argc) <= 0) {
                snprintf(err, err_sz, "unknown function %s", name);
                return -1;
            }
            /* defer: return first with matching arity if only one? No - must be runtime */
            return -2;
        }
    }
    call_empty = flat_func_tag_key_empty(call_has_tag,
                                         call_has_tag ? canonical : "");
    for (i = 0; i < prog->function_count; i++) {
        const FgirFunction *fn = &prog->functions[i];
        if (strcmp(fn->name, name) != 0 || fn->param_count != argc) {
            continue;
        }
        if (flat_func_tag_key_empty(fn->def_tag_has_section, fn->def_tag_canonical)) {
            has_empty_variant = 1;
        } else {
            tagged_only = 0;
        }
        if (overload_matches(fn, name, argc, call_empty, canonical)) {
            if (match >= 0) {
                snprintf(err, err_sz, "type.def.overload.ambiguous");
                return -1;
            }
            match = i;
        }
    }
    if (match >= 0) {
        return match;
    }
    if (call_empty && !has_empty_variant && !tagged_only) {
        snprintf(err, err_sz, "type.tag.not_accepted");
        return -1;
    }
    if (call_empty && tagged_only) {
        snprintf(err, err_sz, "type.tag.not_accepted");
        return -1;
    }
    snprintf(err, err_sz, "type.tag.not_accepted");
    return -1;
}

static int split_call_args_tag(FgirInstr *ins) {
    char buf[FGIR_MAX_LINE];
    char *operands;
    char *tag_expr;
    int has_tag;

    if (!ins) {
        return -1;
    }
    snprintf(buf, sizeof buf, "%s", ins->func_args);
    flat_native_split_operands_tag(buf, &operands, &tag_expr, &has_tag);
    snprintf(ins->func_args, sizeof ins->func_args, "%s", operands ? operands : "");
    ins->func_call_has_tag_section = (uint8_t)(has_tag ? 1 : 0);
    ins->func_call_tag_expr[0] = '\0';
    if (has_tag && tag_expr) {
        snprintf(ins->func_call_tag_expr, sizeof ins->func_call_tag_expr, "%s", tag_expr);
    }
    return 0;
}

static int resolve_one_call(const FgirProgram *prog, FgirInstr *ins, char *err, size_t err_sz) {
    char arg_tokens[FGIR_MAX_FUNC_PARAMS][64];
    int argc = 0;
    int idx;
    int dynamic = 0;

    if (split_call_args_tag(ins) != 0) {
        return -1;
    }
    if (split_call_param_list(ins->func_args, arg_tokens, FGIR_MAX_FUNC_PARAMS, &argc) != 0) {
        snprintf(err, err_sz, "too many arguments");
        return -1;
    }
    idx = flat_func_resolve_call_index(prog, ins->func_name, argc, ins->func_call_has_tag_section,
                                       ins->func_call_tag_expr, &dynamic, err, err_sz);
    if (idx == -2) {
        ins->func_resolved_index = -1;
        ins->func_call_dynamic_tag = 1;
        return 0;
    }
    if (idx < 0) {
        return -1;
    }
    ins->func_resolved_index = idx;
    ins->func_call_dynamic_tag = 0;
    return 0;
}

int flat_func_resolve_call_sites(FgirProgram *prog, char *err, size_t err_sz) {
    int bi, ii;

    if (!prog) {
        return -1;
    }
    for (bi = 0; bi < prog->block_count; bi++) {
        FgirBlock *b = &prog->blocks[bi];
        for (ii = 0; ii < b->instr_count; ii++) {
            FgirInstr *ins = &b->instr[ii];
            if (ins->kind != FGIR_FUNC_CALL) {
                continue;
            }
            if (resolve_one_call(prog, ins, err, err_sz) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

int flat_func_tag_validate_program(const FgirProgram *prog, char *err, size_t err_sz) {
    return flat_func_resolve_call_sites((FgirProgram *)prog, err, err_sz);
}

uint16_t flat_fb_resolve_function_index(const FlatBytecode *bc, const char *name, uint8_t argc,
                                        int call_has_tag, const char *tag_canonical,
                                        char *panic, size_t panic_sz) {
    int i;
    int match = UINT16_MAX;
    int call_empty;
    int has_empty = 0;
    int tagged_only = 1;

    if (!bc || !name) {
        snprintf(panic, panic_sz, "type.tag.not_accepted");
        return UINT16_MAX;
    }
    call_empty = flat_func_tag_key_empty(call_has_tag, tag_canonical);
    for (i = 0; i < (int)bc->function_count; i++) {
        const FlatFbFunctionEntry *fn = &bc->functions[i];
        if (strcmp(fn->name, name) != 0 || fn->param_count != argc) {
            continue;
        }
        if (flat_func_tag_key_empty(fn->def_tag_has_section, fn->def_tag_canonical)) {
            has_empty = 1;
        } else {
            tagged_only = 0;
        }
        if (call_empty) {
            if (flat_func_tag_key_empty(fn->def_tag_has_section, fn->def_tag_canonical)) {
                if (match != UINT16_MAX) {
                    snprintf(panic, panic_sz, "type.def.overload.ambiguous");
                    return UINT16_MAX;
                }
                match = (uint16_t)i;
            }
        } else if (strcmp(fn->def_tag_canonical, tag_canonical) == 0) {
            if (match != UINT16_MAX) {
                snprintf(panic, panic_sz, "type.def.overload.ambiguous");
                return UINT16_MAX;
            }
            match = (uint16_t)i;
        }
    }
    if (match != UINT16_MAX) {
        return match;
    }
    if (call_empty && !has_empty && !tagged_only) {
        snprintf(panic, panic_sz, "type.tag.not_accepted");
        return UINT16_MAX;
    }
    if (call_empty && tagged_only) {
        snprintf(panic, panic_sz, "type.tag.not_accepted");
        return UINT16_MAX;
    }
    snprintf(panic, panic_sz, "type.tag.not_accepted");
    return UINT16_MAX;
}
