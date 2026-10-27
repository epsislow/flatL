#include "flat_value.h"

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

static int find_var_index(const char *name, int var_count, const char (*names)[64]) {
    int i;
    for (i = 0; i < var_count; i++) {
        if (strcmp(names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

static int parse_quoted_payload(const char *start, char *out, size_t out_max, size_t *out_len) {
    char q;
    size_t i = 0;
    const char *p = start;
    if (*p != '\'' && *p != '"') {
        return -1;
    }
    q = *p++;
    while (*p && *p != q) {
        if (i + 1 >= out_max) {
            return -1;
        }
        out[i++] = *p++;
    }
    if (*p != q) {
        return -1;
    }
    out[i] = '\0';
    if (out_len) {
        *out_len = i;
    }
    return 0;
}

static int try_parse_number(const char *s, double *out) {
    char *end = NULL;
    double v;
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return -1;
    }
    v = strtod(s, &end);
    if (end == s || (end && *end != '\0')) {
        return -1;
    }
    if (v != v || v > 1e308 || v < -1e308) {
        return -1;
    }
    *out = v;
    return 0;
}

static int parse_ref_call(const char *rhs, FlatValue *out, FlatStringPool *pool) {
    const char *p = rhs;
    char stack[512];
    char *buf = stack;
    size_t cap = sizeof stack;
    size_t len = 0;
    uint32_t id;
    const char *qstart;
    char q;
    int ok;

    if (strncmp(p, "__ref__(", 8) != 0) {
        return -1;
    }
    p += 8;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    qstart = p;
    if (*p != '\'' && *p != '"') {
        return -1;
    }
    q = *p++;
    while (*p && *p != q) {
        if (len + 2 >= cap) {
            size_t ncap = cap * 2;
            char *grown = buf == stack ? malloc(ncap) : realloc(buf, ncap);
            if (!grown) {
                if (buf != stack) {
                    free(buf);
                }
                return -1;
            }
            if (buf == stack) {
                memcpy(grown, stack, len);
            }
            buf = grown;
            cap = ncap;
        }
        buf[len++] = *p++;
    }
    if (*p != q) {
        ok = -1;
        goto done;
    }
    if (flat_pool_alloc_copy(pool, buf, len, &id) != 0) {
        ok = -1;
        goto done;
    }
    ok = flat_value_set_str_ref(out, id);

done:
    if (buf != stack) {
        free(buf);
    }
    (void)qstart;
    return ok;
}

static int parse_slice_call(const char *rhs, FlatValue *out, FlatStringPool *pool, FlatValue *vars,
                            int var_count, const char (*names)[64]) {
    char inner[256];
    char *tok;
    char varname[64];
    size_t off = 0;
    size_t len = 0;
    int vi;
    FlatValue *src;
    uint32_t ref_id;
    uint32_t slice_id;

    if (strncmp(rhs, "__slice__(", 10) != 0) {
        return -1;
    }
    snprintf(inner, sizeof inner, "%s", rhs + 10);
    {
        char *close = strrchr(inner, ')');
        if (!close) {
            return -1;
        }
        *close = '\0';
    }
    tok = strtok(inner, ",");
    if (!tok) {
        return -1;
    }
    snprintf(varname, sizeof varname, "%s", trim(tok));
    tok = strtok(NULL, ",");
    if (!tok) {
        return -1;
    }
    off = (size_t)strtoul(trim(tok), NULL, 10);
    tok = strtok(NULL, ",");
    if (!tok) {
        return -1;
    }
    len = (size_t)strtoul(trim(tok), NULL, 10);
    vi = find_var_index(varname, var_count, names);
    if (vi < 0 || vars[vi].tag != VAL_STR_REF) {
        return -1;
    }
    src = &vars[vi];
    ref_id = src->u.ref_id;
    if (flat_pool_slice(pool, ref_id, off, len, &slice_id) != 0) {
        return -1;
    }
    return flat_value_set_str_ref(out, slice_id);
}

int flat_value_parse_assign_rhs(const char *rhs_in, FlatValue *out, FlatStringPool *pool,
                                FlatValue *vars, int var_count, const char (*var_names)[64]) {
    char rhs[4096];
    double num;

    if (!rhs_in || !out) {
        return -1;
    }
    snprintf(rhs, sizeof rhs, "%s", rhs_in);
    trim(rhs);

    if (parse_ref_call(rhs, out, pool) == 0) {
        return 0;
    }
    if (parse_slice_call(rhs, out, pool, vars, var_count, var_names) == 0) {
        return 0;
    }
    if (strcmp(rhs, "true") == 0) {
        return flat_value_set_bool(out, 1);
    }
    if (strcmp(rhs, "false") == 0) {
        return flat_value_set_bool(out, 0);
    }
    if (try_parse_number(rhs, &num) == 0) {
        return flat_value_set_number(out, num);
    }
    if (rhs[0] == '\'' || rhs[0] == '"') {
        char buf[FLAT_INLINE_STR_MAX + 1];
        size_t len = 0;
        if (parse_quoted_payload(rhs, buf, sizeof buf, &len) != 0) {
            return -1;
        }
        if (len > FLAT_INLINE_STR_MAX) {
            uint32_t id;
            if (flat_pool_alloc_copy(pool, buf, len, &id) != 0) {
                return -1;
            }
            return flat_value_set_str_ref(out, id);
        }
        return flat_value_set_str_inline(out, buf, len);
    }
    return -1;
}

static int literal_to_value(const char *lit, FlatValue *out) {
    double num;
    char buf[FLAT_INLINE_STR_MAX + 1];
    size_t len = 0;

    if (strcmp(lit, "true") == 0) {
        return flat_value_set_bool(out, 1);
    }
    if (strcmp(lit, "false") == 0) {
        return flat_value_set_bool(out, 0);
    }
    if (try_parse_number(lit, &num) == 0) {
        return flat_value_set_number(out, num);
    }
    if (lit[0] == '\'' || lit[0] == '"') {
        if (parse_quoted_payload(lit, buf, sizeof buf, &len) != 0) {
            return -1;
        }
        return flat_value_set_str_inline(out, buf, len);
    }
    return -1;
}

int flat_value_eval_guard(const char *expr, FlatValue *vars, int var_count, const char (*names)[64],
                          const FlatStringPool *pool) {
    char buf[512];
    char *oppos = NULL;
    char *lhs;
    char *rhs;
    int op = 0;
    FlatValue lv;
    FlatValue rv;
    int li;
    size_t i;

    if (!expr) {
        return 0;
    }
    snprintf(buf, sizeof buf, "%s", expr);
    trim(buf);

    for (i = 0; buf[i]; i++) {
        if (i + 1 < strlen(buf) && buf[i] == '=' && buf[i + 1] == '=') {
            oppos = &buf[i];
            op = 0;
            break;
        }
        if (i + 1 < strlen(buf) && buf[i] == '>' && buf[i + 1] == '=') {
            oppos = &buf[i];
            op = 1;
            break;
        }
        if (i + 1 < strlen(buf) && buf[i] == '<' && buf[i + 1] == '=') {
            oppos = &buf[i];
            op = 2;
            break;
        }
        if (buf[i] == '>' && (i + 1 >= strlen(buf) || buf[i + 1] != '=')) {
            oppos = &buf[i];
            op = 3;
            break;
        }
        if (buf[i] == '<' && (i + 1 >= strlen(buf) || buf[i + 1] != '=')) {
            oppos = &buf[i];
            op = 4;
            break;
        }
    }
    if (!oppos) {
        return 0;
    }
    *oppos = '\0';
    lhs = trim(buf);
    rhs = trim(oppos + (op <= 2 ? 2 : 1));

    li = find_var_index(lhs, var_count, names);
    if (li >= 0) {
        lv = vars[li];
    } else if (literal_to_value(lhs, &lv) != 0) {
        return 0;
    }
    if (literal_to_value(rhs, &rv) != 0) {
        li = find_var_index(rhs, var_count, names);
        if (li >= 0) {
            rv = vars[li];
        } else {
            return 0;
        }
    }

    if (op == 0) {
        return flat_value_equals(&lv, &rv, pool);
    }
    if (lv.tag == VAL_NUMBER && rv.tag == VAL_NUMBER) {
        return flat_value_compare_number(&lv, rv.u.number, op);
    }
    return 0;
}
