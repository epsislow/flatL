#include "flat_value.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p;
    FlatStringPool *pool;
    FlatCollectionPool *coll;
} JsonCtx;

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static int is_empty_trimmed(const char *s) {
    return skip_ws(s)[0] == '\0';
}

static int append_char(char *buf, size_t *pos, size_t max, char c) {
    if (*pos + 1 >= max) {
        return -1;
    }
    buf[(*pos)++] = c;
    buf[*pos] = '\0';
    return 0;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int read_u16_hex(const char **p, uint16_t *out) {
    uint16_t v = 0;
    for (int i = 0; i < 4; i++) {
        int h = hex_value(**p);
        if (h < 0) {
            return -1;
        }
        (*p)++;
        v = (uint16_t)((v << 4) | (uint16_t)h);
    }
    *out = v;
    return 0;
}

static int append_utf8_cp(char *buf, size_t *len, size_t max, uint32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return -1;
    }
    if (cp <= 0x7F) {
        if (*len + 1 >= max) {
            return -1;
        }
        buf[(*len)++] = (char)cp;
    } else if (cp <= 0x7FF) {
        if (*len + 2 >= max) {
            return -1;
        }
        buf[(*len)++] = (char)(0xC0 | (cp >> 6));
        buf[(*len)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        if (*len + 3 >= max) {
            return -1;
        }
        buf[(*len)++] = (char)(0xE0 | (cp >> 12));
        buf[(*len)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[(*len)++] = (char)(0x80 | (cp & 0x3F));
    } else {
        if (*len + 4 >= max) {
            return -1;
        }
        buf[(*len)++] = (char)(0xF0 | (cp >> 18));
        buf[(*len)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[(*len)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[(*len)++] = (char)(0x80 | (cp & 0x3F));
    }
    return 0;
}

/* After '\\u', *p points at first hex digit; advances *p past the escape(s). */
static int parse_json_u_escape(const char **p, uint32_t *out_cp) {
    uint16_t hi;
    uint32_t cp;

    if (read_u16_hex(p, &hi) != 0) {
        return -1;
    }
    if (hi >= 0xD800 && hi <= 0xDBFF) {
        const char *q = *p;
        uint16_t lo;
        if (q[0] != '\\' || q[1] != 'u') {
            return -1;
        }
        q += 2;
        if (read_u16_hex(&q, &lo) != 0) {
            return -1;
        }
        if (lo < 0xDC00 || lo > 0xDFFF) {
            return -1;
        }
        cp = 0x10000u + (((uint32_t)(hi - 0xD800) << 10) | (uint32_t)(lo - 0xDC00));
        *p = q;
    } else {
        if (hi >= 0xDC00 && hi <= 0xDFFF) {
            return -1;
        }
        cp = hi;
    }
    *out_cp = cp;
    return 0;
}

static int set_str_from_bytes(FlatValue *out, FlatStringPool *pool, const char *s, size_t len) {
    if (len <= FLAT_INLINE_STR_MAX) {
        return flat_value_set_str_inline(out, s, len);
    }
    {
        uint32_t id;
        if (flat_pool_alloc_copy(pool, s, len, &id) != 0) {
            return -1;
        }
        return flat_value_set_str_ref(out, id);
    }
}

static int unescape_simple_token(const char *in, char *out, size_t out_max) {
    size_t j = 0;
    for (size_t i = 0; in[i]; i++) {
        if (in[i] == '\\' && in[i + 1]) {
            i++;
            char c = in[i];
            if (c == 'n') {
                c = '\n';
            } else if (c == 'r') {
                c = '\r';
            } else if (c == 't') {
                c = '\t';
            } else if (c == 'u') {
                const char *q = in + i + 1;
                uint32_t cp;
                if (parse_json_u_escape(&q, &cp) != 0) {
                    return -1;
                }
                if (append_utf8_cp(out, &j, out_max, cp) != 0) {
                    return -1;
                }
                i = (size_t)(q - in) - 1;
                continue;
            }
            if (j + 1 >= out_max) {
                return -1;
            }
            out[j++] = c;
            continue;
        }
        if (j + 1 >= out_max) {
            return -1;
        }
        out[j++] = in[i];
    }
    out[j] = '\0';
    return 0;
}

#define SIMPLE_MAX_TOKENS 1024
#define SIMPLE_TOKEN_MAX 4096

static int split_simple_commas(const char *line, char tokens[][SIMPLE_TOKEN_MAX], int *out_count) {
    int n = 0;
    size_t i = 0;
    size_t len = strlen(line);

    *out_count = 0;
    if (len == 0) {
        return 0;
    }
    while (i <= len) {
        size_t start = i;
        char raw[SIMPLE_TOKEN_MAX];
        char decoded[SIMPLE_TOKEN_MAX];
        size_t ri = 0;

        while (i < len) {
            if (line[i] == '\\' && i + 1 < len) {
                if (ri + 2 >= sizeof raw) {
                    return -1;
                }
                raw[ri++] = line[i++];
                raw[ri++] = line[i++];
                continue;
            }
            if (line[i] == ',') {
                break;
            }
            if (ri + 1 >= sizeof raw) {
                return -1;
            }
            raw[ri++] = line[i++];
        }
        raw[ri] = '\0';
        {
            char *t = raw;
            while (*t && isspace((unsigned char)*t)) {
                t++;
            }
            {
                char *e = t + strlen(t);
                while (e > t && isspace((unsigned char)e[-1])) {
                    e--;
                }
                *e = '\0';
            }
            if (unescape_simple_token(t, decoded, sizeof decoded) != 0) {
                return -1;
            }
        }
        if (n >= SIMPLE_MAX_TOKENS) {
            return -1;
        }
        snprintf(tokens[n], SIMPLE_TOKEN_MAX, "%s", decoded);
        n++;
        if (i < len && line[i] == ',') {
            i++;
        } else if (i >= len) {
            break;
        }
        (void)start;
    }
    *out_count = n;
    return 0;
}

static int parse_simple_list(const char *line, FlatValue *out, FlatStringPool *pool,
                             FlatCollectionPool *coll) {
    char tokens[SIMPLE_MAX_TOKENS][SIMPLE_TOKEN_MAX];
    int count = 0;
    int all_num = 1;
    int i;
    FlatValue *items = NULL;

    (void)pool;
    if (split_simple_commas(line, tokens, &count) != 0) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (tokens[i][0] != '\0') {
            double n;
            if (flat_parse_number_token(tokens[i], &n) != 0) {
                all_num = 0;
                break;
            }
        }
    }
    items = calloc((size_t)count, sizeof(FlatValue));
    if (!items && count > 0) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (all_num) {
            double n = 0;
            if (tokens[i][0] != '\0') {
                if (flat_parse_number_token(tokens[i], &n) != 0) {
                    free(items);
                    return -1;
                }
            }
            if (flat_value_set_number(&items[i], n) != 0) {
                free(items);
                return -1;
            }
        } else {
            if (flat_value_set_str_inline(&items[i], tokens[i], strlen(tokens[i])) != 0) {
                free(items);
                return -1;
            }
        }
    }
    {
        uint32_t id;
        if (count == 0) {
            free(items);
            return flat_list_empty(coll, &id) == 0 ? flat_value_set_list(out, id) : -1;
        }
        if (flat_list_build(coll, items, count, &id) != 0) {
            free(items);
            return -1;
        }
        free(items);
        return flat_value_set_list(out, id);
    }
}

static int parse_simple_bools(const char *line, FlatValue *out, FlatStringPool *pool,
                              FlatCollectionPool *coll, int io_panic_cancel) {
    char tokens[SIMPLE_MAX_TOKENS][SIMPLE_TOKEN_MAX];
    int count = 0;
    int i;
    FlatValue *items = NULL;

    (void)pool;
    if (split_simple_commas(line, tokens, &count) != 0) {
        return -1;
    }
    items = calloc((size_t)count, sizeof(FlatValue));
    if (!items && count > 0) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        int b = 0;
        if (tokens[i][0] == '\0') {
            b = 0;
        } else if (io_panic_cancel && flat_parse_bool_cancel_token(tokens[i])) {
            free(items);
            return -2;
        } else if (flat_parse_bool_token(tokens[i], &b) != 0) {
            free(items);
            return -1;
        }
        if (flat_value_set_bool(&items[i], b) != 0) {
            free(items);
            return -1;
        }
    }
    {
        uint32_t id;
        if (count == 0) {
            free(items);
            return flat_list_empty(coll, &id) == 0 ? flat_value_set_list(out, id) : -1;
        }
        if (flat_list_build(coll, items, count, &id) != 0) {
            free(items);
            return -1;
        }
        free(items);
        return flat_value_set_list(out, id);
    }
}

static int parse_json_string_at(JsonCtx *ctx, FlatValue *out) {
    char buf[8192];
    size_t len = 0;
    const char *p = ctx->p;

    if (*p != '"') {
        return -1;
    }
    p++;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            p++;
            char c = *p++;
            if (c == 'n') {
                c = '\n';
            } else if (c == 'r') {
                c = '\r';
            } else if (c == 't') {
                c = '\t';
            } else if (c == 'u') {
                uint32_t cp;
                if (parse_json_u_escape(&p, &cp) != 0 ||
                    append_utf8_cp(buf, &len, sizeof buf, cp) != 0) {
                    return -1;
                }
                continue;
            }
            if (len + 1 >= sizeof buf) {
                return -1;
            }
            buf[len++] = c;
            continue;
        }
        if (len + 1 >= sizeof buf) {
            return -1;
        }
        buf[len++] = *p++;
    }
    if (*p != '"') {
        return -1;
    }
    p++;
    ctx->p = p;
    return set_str_from_bytes(out, ctx->pool, buf, len);
}

static int parse_json_number_at(JsonCtx *ctx, FlatValue *out) {
    char tmp[128];
    size_t i = 0;
    const char *p = ctx->p;
    double n;

    while (p[i] && (isdigit((unsigned char)p[i]) || p[i] == '-' || p[i] == '+' || p[i] == '.' ||
                          p[i] == 'e' || p[i] == 'E')) {
        if (i + 1 >= sizeof tmp) {
            return -1;
        }
        tmp[i] = p[i];
        i++;
    }
    if (i == 0) {
        return -1;
    }
    tmp[i] = '\0';
    if (flat_parse_number_token(tmp, &n) != 0) {
        return -1;
    }
    ctx->p = p + i;
    return flat_value_set_number(out, n);
}

static int parse_value_at(JsonCtx *ctx, FlatValue *out, int depth);

static int parse_json_array_at(JsonCtx *ctx, FlatValue *out, int depth) {
    FlatValue *items = NULL;
    int cap = 0;
    int count = 0;

    if (depth > FLAT_JSON_MAX_DEPTH) {
        return -1;
    }
    if (*ctx->p != '[') {
        return -1;
    }
    ctx->p++;
    ctx->p = skip_ws(ctx->p);
    if (*ctx->p == ']') {
        uint32_t id;
        ctx->p++;
        if (flat_list_empty(ctx->coll, &id) != 0) {
            return -1;
        }
        return flat_value_set_list(out, id);
    }
    for (;;) {
        FlatValue el;
        if (count >= cap) {
            int ncap = cap ? cap * 2 : 4;
            FlatValue *g = items ? realloc(items, (size_t)ncap * sizeof(FlatValue))
                                 : malloc((size_t)ncap * sizeof(FlatValue));
            if (!g) {
                free(items);
                return -1;
            }
            items = g;
            cap = ncap;
        }
        ctx->p = skip_ws(ctx->p);
        if (parse_value_at(ctx, &el, depth + 1) != 0) {
            free(items);
            return -1;
        }
        items[count++] = el;
        ctx->p = skip_ws(ctx->p);
        if (*ctx->p == ',') {
            ctx->p++;
            ctx->p = skip_ws(ctx->p);
            if (*ctx->p == ']') {
                free(items);
                return -1;
            }
            continue;
        }
        if (*ctx->p == ']') {
            ctx->p++;
            break;
        }
        free(items);
        return -1;
    }
    {
        uint32_t id;
        if (flat_list_build(ctx->coll, items, count, &id) != 0) {
            free(items);
            return -1;
        }
        free(items);
        return flat_value_set_list(out, id);
    }
}

static int parse_json_object_at(JsonCtx *ctx, FlatValue *out, int depth) {
    char keys[FLAT_OBJ_MAX_KEYS][FLAT_OBJ_KEY_MAX + 1];
    FlatValue vals[FLAT_OBJ_MAX_KEYS];
    int count = 0;

    if (depth > FLAT_JSON_MAX_DEPTH) {
        return -1;
    }
    if (*ctx->p != '{') {
        return -1;
    }
    ctx->p++;
    ctx->p = skip_ws(ctx->p);
    if (*ctx->p == '}') {
        ctx->p++;
        {
            uint32_t id;
            return flat_obj_empty(ctx->coll, &id) == 0 ? flat_value_set_object(out, id) : -1;
        }
    }
    for (;;) {
        FlatValue keyval;
        const char *keyptr;
        char keybuf[FLAT_OBJ_KEY_MAX + 1];
        size_t klen;

        ctx->p = skip_ws(ctx->p);
        if (parse_json_string_at(ctx, &keyval) != 0) {
            return -1;
        }
        if (keyval.tag != VAL_STR_INLINE && keyval.tag != VAL_STR_REF) {
            return -1;
        }
        if (flat_value_get_cstr(&keyval, ctx->pool, keybuf, sizeof keybuf) != 0) {
            return -1;
        }
        keyptr = keybuf;
        klen = strlen(keyptr);
        if (klen > FLAT_OBJ_KEY_MAX) {
            return -1;
        }
        ctx->p = skip_ws(ctx->p);
        if (*ctx->p != ':') {
            return -1;
        }
        ctx->p++;
        ctx->p = skip_ws(ctx->p);
        if (count >= FLAT_OBJ_MAX_KEYS) {
            return -1;
        }
        if (parse_value_at(ctx, &vals[count], depth + 1) != 0) {
            return -1;
        }
        memcpy(keys[count], keyptr, klen + 1);
        count++;
        ctx->p = skip_ws(ctx->p);
        if (*ctx->p == ',') {
            ctx->p++;
            ctx->p = skip_ws(ctx->p);
            if (*ctx->p == '}') {
                return -1;
            }
            continue;
        }
        if (*ctx->p == '}') {
            ctx->p++;
            break;
        }
        return -1;
    }
    {
        uint32_t id;
        if (flat_obj_build(ctx->coll, keys, vals, count, &id) != 0) {
            return -1;
        }
        return flat_value_set_object(out, id);
    }
}

static int parse_value_at(JsonCtx *ctx, FlatValue *out, int depth) {
    const char *p;

    ctx->p = skip_ws(ctx->p);
    p = ctx->p;
    if (*p == '"') {
        return parse_json_string_at(ctx, out);
    }
    if (*p == '[') {
        return parse_json_array_at(ctx, out, depth);
    }
    if (*p == '{') {
        return parse_json_object_at(ctx, out, depth);
    }
    if (strncmp(p, "true", 4) == 0 && !isalnum((unsigned char)p[4]) && p[4] != '_') {
        ctx->p = p + 4;
        return flat_value_set_bool(out, 1);
    }
    if (strncmp(p, "false", 5) == 0 && !isalnum((unsigned char)p[5]) && p[5] != '_') {
        ctx->p = p + 5;
        return flat_value_set_bool(out, 0);
    }
    if (strncmp(p, "null", 4) == 0 && !isalnum((unsigned char)p[4]) && p[4] != '_') {
        ctx->p = p + 4;
        return flat_value_set_null(out);
    }
    if (*p == '-' || isdigit((unsigned char)*p)) {
        return parse_json_number_at(ctx, out);
    }
    return -1;
}

static int parse_json_document(const char *s, FlatValue *out, FlatStringPool *pool,
                               FlatCollectionPool *coll, int expect_object) {
    JsonCtx ctx;
    FlatValue v;

    if (!s || !out || !coll) {
        return -1;
    }
    ctx.p = skip_ws(s);
    ctx.pool = pool;
    ctx.coll = coll;
    if (parse_value_at(&ctx, &v, 0) != 0) {
        return -1;
    }
    ctx.p = skip_ws(ctx.p);
    if (*ctx.p != '\0') {
        return -1;
    }
    if (expect_object && v.tag != VAL_OBJECT) {
        return -1;
    }
    *out = v;
    return 0;
}

int flat_json_parse(const char *s, FlatValue *out, FlatStringPool *pool, FlatCollectionPool *coll) {
    if (is_empty_trimmed(s)) {
        return flat_value_set_null(out);
    }
    return parse_json_document(s, out, pool, coll, 0);
}

int flat_json_parse_list(const char *s, FlatValue *out, FlatStringPool *pool,
                         FlatCollectionPool *coll) {
    const char *t;

    if (!out || !coll) {
        return -1;
    }
    t = skip_ws(s);
    if (*t == '\0') {
        uint32_t id;
        return flat_list_empty(coll, &id) == 0 ? flat_value_set_list(out, id) : -1;
    }
    if (*t == '[') {
        FlatValue v;
        if (parse_json_document(s, &v, pool, coll, 0) != 0 || v.tag != VAL_LIST) {
            return -1;
        }
        *out = v;
        return 0;
    }
    return parse_simple_list(t, out, pool, coll);
}

int flat_json_parse_object(const char *s, FlatValue *out, FlatStringPool *pool,
                           FlatCollectionPool *coll) {
    const char *t;

    if (!out || !coll) {
        return -1;
    }
    t = skip_ws(s);
    if (*t == '\0') {
        uint32_t id;
        return flat_obj_empty(coll, &id) == 0 ? flat_value_set_object(out, id) : -1;
    }
    if (*t != '{') {
        return -1;
    }
    return parse_json_document(s, out, pool, coll, 1);
}

int flat_json_parse_bools(const char *s, FlatValue *out, FlatStringPool *pool,
                          FlatCollectionPool *coll) {
    const char *t;

    if (!out || !coll) {
        return -1;
    }
    t = skip_ws(s);
    if (*t == '\0') {
        uint32_t id;
        return flat_list_empty(coll, &id) == 0 ? flat_value_set_list(out, id) : -1;
    }
    if (*t == '[') {
        FlatValue v;
        int i;
        int len;
        if (parse_json_document(s, &v, pool, coll, 0) != 0 || v.tag != VAL_LIST) {
            return -1;
        }
        if (flat_list_len(coll, v.u.coll_id, &len) != 0) {
            return -1;
        }
        for (i = 0; i < len; i++) {
            FlatValue el;
            if (flat_list_get(coll, v.u.coll_id, i, &el) != 0 || el.tag != VAL_BOOL) {
                return -1;
            }
        }
        *out = v;
        return 0;
    }
    return parse_simple_bools(t, out, pool, coll, 0);
}

int flat_json_parse_bools_io(const char *s, FlatValue *out, FlatStringPool *pool,
                             FlatCollectionPool *coll) {
    const char *t;

    if (!out || !coll) {
        return -1;
    }
    t = skip_ws(s);
    if (*t == '\0') {
        uint32_t id;
        return flat_list_empty(coll, &id) == 0 ? flat_value_set_list(out, id) : -1;
    }
    if (*t == '[') {
        return flat_json_parse_bools(s, out, pool, coll);
    }
    return parse_simple_bools(t, out, pool, coll, 1);
}

static int json_escape_append(char *buf, size_t *pos, size_t max, const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') {
            if (append_char(buf, pos, max, '\\') != 0) {
                return -1;
            }
        }
        if (c == '\n') {
            if (append_char(buf, pos, max, '\\') != 0 || append_char(buf, pos, max, 'n') != 0) {
                return -1;
            }
            continue;
        }
        if (c == '\r') {
            if (append_char(buf, pos, max, '\\') != 0 || append_char(buf, pos, max, 'r') != 0) {
                return -1;
            }
            continue;
        }
        if (c == '\t') {
            if (append_char(buf, pos, max, '\\') != 0 || append_char(buf, pos, max, 't') != 0) {
                return -1;
            }
            continue;
        }
        if (append_char(buf, pos, max, (char)c) != 0) {
            return -1;
        }
    }
    return 0;
}

static int stringify_value(const FlatValue *v, FlatStringPool *pool, FlatCollectionPool *coll,
                           char *buf, size_t *pos, size_t max, int depth);

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int stringify_object(const FlatValue *v, FlatStringPool *pool, FlatCollectionPool *coll,
                            char *buf, size_t *pos, size_t max, int depth) {
    int len = 0;
    int i;
    const char *keys[FLAT_OBJ_MAX_KEYS];

    if (depth > FLAT_JSON_MAX_DEPTH) {
        return -1;
    }
    if (flat_obj_len(coll, v->u.coll_id, &len) != 0) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (flat_obj_entry_at(coll, v->u.coll_id, i, &keys[i], NULL) != 0) {
            return -1;
        }
    }
    qsort(keys, (size_t)len, sizeof keys[0], cmp_str);
    if (append_char(buf, pos, max, '{') != 0) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        FlatValue val;
        char keybuf[FLAT_OBJ_KEY_MAX + 1];
        if (i > 0 && append_char(buf, pos, max, ',') != 0) {
            return -1;
        }
        snprintf(keybuf, sizeof keybuf, "%s", keys[i]);
        if (append_char(buf, pos, max, '"') != 0) {
            return -1;
        }
        if (json_escape_append(buf, pos, max, keybuf, strlen(keybuf)) != 0) {
            return -1;
        }
        if (append_char(buf, pos, max, '"') != 0 || append_char(buf, pos, max, ':') != 0) {
            return -1;
        }
        if (flat_obj_get(coll, v->u.coll_id, keybuf, &val) != 0) {
            return -1;
        }
        if (stringify_value(&val, pool, coll, buf, pos, max, depth + 1) != 0) {
            return -1;
        }
    }
    return append_char(buf, pos, max, '}');
}

static int stringify_array(const FlatValue *v, FlatStringPool *pool, FlatCollectionPool *coll,
                           char *buf, size_t *pos, size_t max, int depth) {
    int len = 0;
    int i;

    if (depth > FLAT_JSON_MAX_DEPTH) {
        return -1;
    }
    if (flat_list_len(coll, v->u.coll_id, &len) != 0) {
        return -1;
    }
    if (append_char(buf, pos, max, '[') != 0) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        FlatValue el;
        if (i > 0 && append_char(buf, pos, max, ',') != 0) {
            return -1;
        }
        if (flat_list_get(coll, v->u.coll_id, i, &el) != 0) {
            return -1;
        }
        if (stringify_value(&el, pool, coll, buf, pos, max, depth + 1) != 0) {
            return -1;
        }
    }
    return append_char(buf, pos, max, ']');
}

static int stringify_value(const FlatValue *v, FlatStringPool *pool, FlatCollectionPool *coll,
                           char *buf, size_t *pos, size_t max, int depth) {
    char tmp[128];
    char sbuf[4096];
    size_t slen;

    if (!v) {
        return -1;
    }
    switch (v->tag) {
    case VAL_NULL:
        return append_char(buf, pos, max, 'n') || append_char(buf, pos, max, 'u') ||
               append_char(buf, pos, max, 'l') || append_char(buf, pos, max, 'l');
    case VAL_BOOL:
        if (v->u.boolean) {
            return append_char(buf, pos, max, 't') || append_char(buf, pos, max, 'r') ||
                   append_char(buf, pos, max, 'u') || append_char(buf, pos, max, 'e');
        }
        return append_char(buf, pos, max, 'f') || append_char(buf, pos, max, 'a') ||
               append_char(buf, pos, max, 'l') || append_char(buf, pos, max, 's') ||
               append_char(buf, pos, max, 'e');
    case VAL_NUMBER:
        snprintf(tmp, sizeof tmp, "%.17g", v->u.number);
        for (size_t i = 0; tmp[i]; i++) {
            if (append_char(buf, pos, max, tmp[i]) != 0) {
                return -1;
            }
        }
        return 0;
    case VAL_STR_INLINE:
        if (append_char(buf, pos, max, '"') != 0) {
            return -1;
        }
        if (json_escape_append(buf, pos, max, v->u.ins.bytes, v->u.ins.len) != 0) {
            return -1;
        }
        return append_char(buf, pos, max, '"');
    case VAL_STR_REF: {
        const uint8_t *p = flat_pool_read(pool, v->u.ref_id, &slen);
        if (!p) {
            return -1;
        }
        if (slen >= sizeof sbuf) {
            return -1;
        }
        memcpy(sbuf, p, slen);
        sbuf[slen] = '\0';
        if (append_char(buf, pos, max, '"') != 0) {
            return -1;
        }
        if (json_escape_append(buf, pos, max, sbuf, slen) != 0) {
            return -1;
        }
        return append_char(buf, pos, max, '"');
    }
    case VAL_LIST:
        return stringify_array(v, pool, coll, buf, pos, max, depth);
    case VAL_OBJECT:
        return stringify_object(v, pool, coll, buf, pos, max, depth);
    default:
        return -1;
    }
}

int flat_json_stringify(const FlatValue *v, FlatStringPool *pool, FlatCollectionPool *coll,
                        char *out, size_t out_size) {
    size_t pos = 0;

    if (!out || out_size == 0) {
        return -1;
    }
    out[0] = '\0';
    if (stringify_value(v, pool, coll, out, &pos, out_size, 0) != 0) {
        return -1;
    }
    return 0;
}

int flat_json_list_is_homogeneous_numbers(const FlatCollectionPool *coll, uint32_t list_id) {
    int len = 0;
    int i;
    if (flat_list_len(coll, list_id, &len) != 0) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        FlatValue el;
        if (flat_list_get(coll, list_id, i, &el) != 0 || el.tag != VAL_NUMBER) {
            return 0;
        }
    }
    return 1;
}

int flat_json_list_is_homogeneous_strings(const FlatCollectionPool *coll, uint32_t list_id) {
    int len = 0;
    int i;
    if (flat_list_len(coll, list_id, &len) != 0) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        FlatValue el;
        if (flat_list_get(coll, list_id, i, &el) != 0 ||
            (el.tag != VAL_STR_INLINE && el.tag != VAL_STR_REF)) {
            return 0;
        }
    }
    return 1;
}

int flat_json_fprint_list_io(FILE *out, const FlatValue *v, FlatStringPool *pool,
                             FlatCollectionPool *coll) {
    int len = 0;
    int i;

    if (!v || v->tag != VAL_LIST || !coll) {
        return -1;
    }
    if (flat_list_len(coll, v->u.coll_id, &len) != 0) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    if (flat_json_list_is_homogeneous_numbers(coll, v->u.coll_id)) {
        for (i = 0; i < len; i++) {
            FlatValue el;
            if (flat_list_get(coll, v->u.coll_id, i, &el) != 0) {
                return -1;
            }
            if (i > 0) {
                fputc(',', out);
            }
            fprintf(out, "%.17g", el.u.number);
        }
        return 0;
    }
    if (flat_json_list_is_homogeneous_strings(coll, v->u.coll_id)) {
        char buf[4096];
        size_t pos;
        for (i = 0; i < len; i++) {
            FlatValue el;
            size_t slen = 0;
            const char *sp = NULL;

            if (flat_list_get(coll, v->u.coll_id, i, &el) != 0) {
                return -1;
            }
            if (el.tag == VAL_STR_INLINE) {
                sp = el.u.ins.bytes;
                slen = el.u.ins.len;
            } else if (el.tag == VAL_STR_REF) {
                const uint8_t *p = flat_pool_read(pool, el.u.ref_id, &slen);
                if (!p) {
                    return -1;
                }
                sp = (const char *)p;
            } else {
                return -1;
            }
            if (i > 0) {
                fputc(',', out);
            }
            pos = 0;
            buf[0] = '\0';
            append_char(buf, &pos, sizeof buf, '"');
            json_escape_append(buf, &pos, sizeof buf, sp, slen);
            append_char(buf, &pos, sizeof buf, '"');
            fputs(buf, out);
        }
        return 0;
    }
    {
        char jbuf[16384];
        if (flat_json_stringify(v, pool, coll, jbuf, sizeof jbuf) != 0) {
            return -1;
        }
        fputs(jbuf, out);
        return 0;
    }
}

int flat_json_fprint_object_io(FILE *out, const FlatValue *v, FlatStringPool *pool,
                               FlatCollectionPool *coll) {
    char buf[16384];
    if (!v || v->tag != VAL_OBJECT) {
        return -1;
    }
    if (flat_json_stringify(v, pool, coll, buf, sizeof buf) != 0) {
        return -1;
    }
    fputs(buf, out);
    return 0;
}

int flat_json_fprint_null_io(FILE *out) {
    fputs("null", out);
    return 0;
}
