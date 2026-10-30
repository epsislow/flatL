#include "flat_str.h"
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

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static int parse_ident(const char **p, char *out, size_t out_max) {
    const char *s = *p;
    size_t n = 0;

    if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || *s == '_')) {
        return -1;
    }
    while ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
           *s == '_') {
        if (n + 1 >= out_max) {
            return -1;
        }
        out[n++] = *s++;
    }
    out[n] = '\0';
    *p = s;
    return 0;
}

static int flat_value_parse_at(const char **p, FlatValue *out, FlatStringPool *pool,
                               FlatCollectionPool *coll, FlatValue *vars, int var_count,
                               const char (*var_names)[64]);

static int parse_list_literal(const char **p, FlatValue *out, FlatStringPool *pool,
                              FlatCollectionPool *coll, FlatValue *vars, int var_count,
                              const char (*var_names)[64]) {
    FlatValue *items = NULL;
    int count = 0;
    int cap = 0;
    uint32_t id;

    *p = skip_ws(*p);
    if (**p != '[') {
        return -1;
    }
    (*p)++;
    for (;;) {
        *p = skip_ws(*p);
        if (**p == ']') {
            (*p)++;
            break;
        }
        if (count >= cap) {
            int ncap = cap ? cap * 2 : 4;
            FlatValue *grown = items ? realloc(items, (size_t)ncap * sizeof(FlatValue))
                                     : malloc((size_t)ncap * sizeof(FlatValue));
            if (!grown) {
                free(items);
                return -1;
            }
            items = grown;
            cap = ncap;
        }
        if (flat_value_parse_at(p, &items[count], pool, coll, vars, var_count, var_names) != 0) {
            free(items);
            return -1;
        }
        count++;
        *p = skip_ws(*p);
        if (**p == ',') {
            (*p)++;
            continue;
        }
        if (**p == ']') {
            (*p)++;
            break;
        }
        free(items);
        return -1;
    }
    if (!coll) {
        free(items);
        return -1;
    }
    if (flat_list_build(coll, pool, items, count, &id) != 0) {
        free(items);
        return -1;
    }
    free(items);
    return flat_value_set_list(out, id);
}

static int parse_object_key(const char **p, char *key, size_t key_max) {
    char buf[FLAT_OBJ_KEY_MAX + 1];
    size_t len = 0;
    char q;

    *p = skip_ws(*p);
    if (**p == '\'' || **p == '"') {
        q = **p;
        if (parse_quoted_payload(*p, buf, sizeof buf, &len) != 0) {
            return -1;
        }
        (*p)++;
        while (**p && **p != q) {
            (*p)++;
        }
        if (**p == q) {
            (*p)++;
        }
        if (len >= key_max) {
            return -1;
        }
        memcpy(key, buf, len);
        key[len] = '\0';
        return 0;
    }
    return parse_ident(p, key, key_max);
}

static int parse_object_literal(const char **p, FlatValue *out, FlatStringPool *pool,
                                FlatCollectionPool *coll, FlatValue *vars, int var_count,
                                const char (*var_names)[64]) {
    char keys[FLAT_OBJ_MAX_KEYS][FLAT_OBJ_KEY_MAX + 1];
    FlatValue vals[FLAT_OBJ_MAX_KEYS];
    int count = 0;
    uint32_t id;

    *p = skip_ws(*p);
    if (**p != '{') {
        return -1;
    }
    (*p)++;
    for (;;) {
        *p = skip_ws(*p);
        if (**p == '}') {
            (*p)++;
            break;
        }
        if (count >= FLAT_OBJ_MAX_KEYS) {
            return -1;
        }
        if (parse_object_key(p, keys[count], sizeof keys[count]) != 0) {
            return -1;
        }
        *p = skip_ws(*p);
        if (**p != ':') {
            return -1;
        }
        (*p)++;
        if (flat_value_parse_at(p, &vals[count], pool, coll, vars, var_count, var_names) != 0) {
            return -1;
        }
        count++;
        *p = skip_ws(*p);
        if (**p == ',') {
            (*p)++;
            continue;
        }
        if (**p == '}') {
            (*p)++;
            break;
        }
        return -1;
    }
    if (!coll) {
        return -1;
    }
    if (flat_obj_build(coll, pool, keys, vals, count, &id) != 0) {
        return -1;
    }
    return flat_value_set_object(out, id);
}

static int parse_quoted_string_at(const char **p, FlatValue *out, FlatStringPool *pool) {
    char buf[FLAT_INLINE_STR_MAX + 1];
    size_t len = 0;
    char q;

    if (**p != '\'' && **p != '"') {
        return -1;
    }
    q = **p;
    if (parse_quoted_payload(*p, buf, sizeof buf, &len) != 0) {
        return -1;
    }
    (*p)++;
    while (**p && **p != q) {
        (*p)++;
    }
    if (**p == q) {
        (*p)++;
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

static int parse_number_at(const char **p, FlatValue *out) {
    char tmp[64];
    char *end = NULL;
    double v;
    size_t i = 0;
    const char *s = *p;

    while (*s && (isdigit((unsigned char)*s) || *s == '.' || *s == '-' || *s == '+') &&
           i + 1 < sizeof tmp) {
        tmp[i++] = *s++;
    }
    tmp[i] = '\0';
    if (i == 0) {
        return -1;
    }
    v = strtod(tmp, &end);
    if (end == tmp || (end && *end != '\0')) {
        return -1;
    }
    *p = s;
    return flat_value_set_number(out, v);
}

static int parse_paren_call(const char *rhs, FlatValue *out, FlatStringPool *pool,
                            FlatCollectionPool *coll, FlatValue *vars, int var_count,
                            const char (*var_names)[64]) {
    char inner[2048];
    char name[64];
    const char *p = rhs;
    char *close;
    char *tok;
    int vi;
    uint32_t new_id;
    FlatValue item;

    if (parse_ident(&p, name, sizeof name) != 0) {
        return -1;
    }
    p = skip_ws(p);
    if (*p != '(') {
        return -1;
    }
    snprintf(inner, sizeof inner, "%s", p + 1);
    close = strrchr(inner, ')');
    if (!close) {
        return -1;
    }
    *close = '\0';

    if (strcmp(name, "__list__") == 0 && inner[0] == '\0') {
        if (!coll || flat_list_empty(coll, pool, &new_id) != 0) {
            return -1;
        }
        return flat_value_set_list(out, new_id);
    }
    if (strcmp(name, "__list_push__") == 0) {
        char *ip = inner;
        tok = flat_str_next(&ip, ',');
        if (!tok) {
            return -1;
        }
        vi = find_var_index(trim(tok), var_count, var_names);
        if (vi < 0 || vars[vi].tag != VAL_LIST) {
            return -1;
        }
        if (!ip || *ip == '\0') {
            return -1;
        }
        p = skip_ws(ip);
        if (flat_value_parse_at(&p, &item, pool, coll, vars, var_count, var_names) != 0) {
            return -1;
        }
        if (flat_list_push_copy(coll, pool, vars[vi].u.coll_id, &item, &new_id) != 0) {
            return -1;
        }
        return flat_value_set_list(out, new_id);
    }
    if (strcmp(name, "__list_len__") == 0) {
        int len = 0;
        tok = trim(inner);
        vi = find_var_index(tok, var_count, var_names);
        if (vi < 0 || vars[vi].tag != VAL_LIST || !coll) {
            return -1;
        }
        if (flat_list_len(coll, vars[vi].u.coll_id, &len) != 0) {
            return -1;
        }
        return flat_value_set_number(out, (double)len);
    }
    if (strcmp(name, "__list_get__") == 0) {
        int ix;
        char *ip = inner;
        tok = flat_str_next(&ip, ',');
        if (!tok) {
            return -1;
        }
        vi = find_var_index(trim(tok), var_count, var_names);
        if (vi < 0 || vars[vi].tag != VAL_LIST || !coll) {
            return -1;
        }
        if (!ip || *ip == '\0') {
            return -1;
        }
        ix = (int)strtol(trim(ip), NULL, 10);
        return flat_list_get(coll, vars[vi].u.coll_id, ix, out);
    }
    return -1;
}

static int flat_value_parse_at(const char **p, FlatValue *out, FlatStringPool *pool,
                               FlatCollectionPool *coll, FlatValue *vars, int var_count,
                               const char (*var_names)[64]) {
    char ident[64];
    const char *s;
    int vi;

    if (!p || !*p || !out) {
        return -1;
    }
    s = skip_ws(*p);
    *p = s;
    if (*s == '[') {
        return parse_list_literal(p, out, pool, coll, vars, var_count, var_names);
    }
    if (*s == '{') {
        return parse_object_literal(p, out, pool, coll, vars, var_count, var_names);
    }
    if (*s == '\'' || *s == '"') {
        return parse_quoted_string_at(p, out, pool);
    }
    if (strncmp(s, "true", 4) == 0 &&
        (s[4] == '\0' || !(isalnum((unsigned char)s[4]) || s[4] == '_'))) {
        *p = s + 4;
        return flat_value_set_bool(out, 1);
    }
    if (strncmp(s, "false", 5) == 0 &&
        (s[5] == '\0' || !(isalnum((unsigned char)s[5]) || s[5] == '_'))) {
        *p = s + 5;
        return flat_value_set_bool(out, 0);
    }
    if (strncmp(s, "null", 4) == 0 &&
        (s[4] == '\0' || !(isalnum((unsigned char)s[4]) || s[4] == '_'))) {
        *p = s + 4;
        return flat_value_set_null(out);
    }
    if (isdigit((unsigned char)*s) || *s == '-' || *s == '+') {
        return parse_number_at(p, out);
    }
    if (parse_ident(&s, ident, sizeof ident) == 0) {
        vi = find_var_index(ident, var_count, var_names);
        if (vi >= 0) {
            *p = s;
            *out = vars[vi];
            return 0;
        }
        if (*s == '(') {
            char line[4096];
            const char *end = strchr(s, ')');
            size_t n;
            if (!end) {
                return -1;
            }
            n = (size_t)(end - *p + 1);
            if (n >= sizeof line) {
                return -1;
            }
            memcpy(line, *p, n);
            line[n] = '\0';
            if (parse_paren_call(line, out, pool, coll, vars, var_count, var_names) == 0) {
                *p = end + 1;
                return 0;
            }
        }
    }
    return -1;
}

int flat_value_parse_assign_rhs(const char *rhs_in, FlatValue *out, FlatStringPool *pool,
                                FlatCollectionPool *coll, FlatValue *vars, int var_count,
                                const char (*var_names)[64]) {
    char rhs[4096];
    const char *p;

    if (!rhs_in || !out) {
        return -1;
    }
    snprintf(rhs, sizeof rhs, "%s", rhs_in);
    trim(rhs);

    if (parse_ref_call(rhs, out, pool) == 0) {
        return 0;
    }
    if (strchr(rhs, '(') &&
        (strncmp(rhs, "__list__", 8) == 0 || strncmp(rhs, "__list_push__", 13) == 0 ||
         strncmp(rhs, "__list_len__", 12) == 0 || strncmp(rhs, "__list_get__", 12) == 0)) {
        if (parse_paren_call(rhs, out, pool, coll, vars, var_count, var_names) == 0) {
            return 0;
        }
    }
    p = rhs;
    if (flat_value_parse_at(&p, out, pool, coll, vars, var_count, var_names) != 0) {
        return -1;
    }
    p = skip_ws(p);
    if (*p != '\0') {
        return -1;
    }
    return 0;
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
    if (strcmp(lit, "null") == 0) {
        return flat_value_set_null(out);
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
