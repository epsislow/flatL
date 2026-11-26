#include "flat_native_mod.h"
#include "flat_locale.h"
#include "flat_native_tag.h"
#include "flat_native_util.h"
#include "flat_number.h"
#include "flat_slot_type.h"
#include "flat_value.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int split_two_args(char *inner, char **left, char **right) {
    int depth = 0;
    char quote = 0;
    char *p;
    for (p = inner; *p; p++) {
        if (!quote && (*p == '\'' || *p == '"')) {
            quote = *p;
        } else if (quote && *p == quote) {
            quote = 0;
        } else if (!quote && (*p == '[' || *p == '(')) {
            depth++;
        } else if (!quote && (*p == ']' || *p == ')')) {
            if (depth > 0) {
                depth--;
            }
        } else if (!quote && depth == 0 && *p == ',') {
            *p = '\0';
            *left = flat_native_trim(inner);
            *right = flat_native_trim(p + 1);
            return 0;
        }
    }
    return -1;
}

static int split_three_args(char *inner, char **first, char **second, char **third) {
    int depth = 0;
    char quote = 0;
    char *p;
    char *c1 = NULL;
    char *c2 = NULL;
    for (p = inner; *p; p++) {
        if (!quote && (*p == '\'' || *p == '"')) {
            quote = *p;
        } else if (quote && *p == quote) {
            quote = 0;
        } else if (!quote && (*p == '[' || *p == '(')) {
            depth++;
        } else if (!quote && (*p == ']' || *p == ')')) {
            if (depth > 0) {
                depth--;
            }
        } else if (!quote && depth == 0 && *p == ',') {
            if (!c1) {
                *p = '\0';
                c1 = p;
            } else if (!c2) {
                *p = '\0';
                c2 = p;
                break;
            }
        }
    }
    if (!c1 || !c2) {
        return -1;
    }
    *first = flat_native_trim(inner);
    *second = flat_native_trim(c1 + 1);
    *third = flat_native_trim(c2 + 1);
    return 0;
}

static int split_variadic_args(char *inner, char *parts[], int max_parts, int *out_count) {
    int depth = 0;
    int n = 0;
    char *start;
    char *p;
    if (!inner || !parts || !out_count || max_parts <= 0) {
        return -1;
    }
    start = flat_native_trim(inner);
    if (*start == '\0') {
        return -1;
    }
    parts[n++] = start;
    char quote = 0;
    for (p = start; *p; p++) {
        if (!quote && (*p == '\'' || *p == '"')) {
            quote = *p;
        } else if (quote && *p == quote) {
            quote = 0;
        } else if (!quote && (*p == '[' || *p == '(')) {
            depth++;
        } else if (!quote && (*p == ']' || *p == ')')) {
            if (depth > 0) {
                depth--;
            }
        } else if (!quote && depth == 0 && *p == ',') {
            *p = '\0';
            start = flat_native_trim(p + 1);
            if (*start == '\0') {
                return -1;
            }
            if (n >= max_parts) {
                return -1;
            }
            parts[n++] = start;
            p = start - 1;
        }
    }
    *out_count = n;
    return 0;
}

static int str_bytes(const FlatValue *v, FlatStringPool *pool, const uint8_t **bp, size_t *len) {
    if (!v || !bp || !len) {
        return -1;
    }
    if (v->tag == VAL_STR_INLINE) {
        *bp = (const uint8_t *)v->u.ins.bytes;
        *len = v->u.ins.len;
        return 0;
    }
    if (v->tag == VAL_STR_REF) {
        *bp = flat_pool_read(pool, v->u.ref_id, len);
        return *bp ? 0 : -1;
    }
    return -1;
}

static int str_fail(FlatNativeCtx *ctx, const char *why) {
    if (ctx && ctx->io_status) {
        *ctx->io_status = FLAT_IO_ERR;
    }
    if (ctx && why) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", why);
    }
    return -1;
}

static int emit_bytes(FlatNativeCtx *ctx, const void *data, size_t len) {
    uint32_t id;
    if (!ctx->out) {
        return -1;
    }
    if (len == 0) {
        return flat_value_set_str_inline(ctx->out, "", 0);
    }
    if (len <= FLAT_INLINE_STR_MAX) {
        return flat_value_set_str_inline(ctx->out, (const char *)data, len);
    }
    if (flat_pool_alloc_copy(ctx->pool, data, len, &id) != 0) {
        str_fail(ctx, "pool.full");
        return -1;
    }
    return flat_value_set_str_ref(ctx->out, id);
}

static int parse_whole_number(FlatNativeCtx *ctx, const char *expr, double *out) {
    FlatValue v;
    if (flat_native_parse_expr(ctx, expr, &v) != 0 || v.tag != VAL_NUMBER) {
        return -1;
    }
    if (flat_number_as_f64(&v) != floor(flat_number_as_f64(&v))) {
        return -1;
    }
    *out = flat_number_as_f64(&v);
    return 0;
}

static int parse_nonneg_int(FlatNativeCtx *ctx, const char *expr, int *out) {
    double n;
    if (parse_whole_number(ctx, expr, &n) != 0 || n < 0.0) {
        return -1;
    }
    *out = (int)n;
    return 0;
}

static int parse_int_expr(FlatNativeCtx *ctx, const char *expr, int *out) {
    double n;
    if (parse_whole_number(ctx, expr, &n) != 0) {
        return -1;
    }
    *out = (int)n;
    return 0;
}

static int parse_str_arg(FlatNativeCtx *ctx, const char *expr, FlatValue *out) {
    if (flat_native_parse_expr(ctx, expr, out) != 0) {
        return -1;
    }
    if (out->tag != VAL_STR_INLINE && out->tag != VAL_STR_REF) {
        return -1;
    }
    return 0;
}

static int io_err(FlatNativeCtx *ctx) {
    return str_fail(ctx, NULL);
}

static int io_ok(FlatNativeCtx *ctx) {
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return 0;
}

static int string_from_inner(FlatNativeCtx *ctx, char *token, size_t token_size) {
    FlatValue src;
    char inner_buf[4096];
    const uint8_t *bp;
    size_t len;

    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (parse_str_arg(ctx, inner_buf, &src) != 0) {
        return -1;
    }
    if (str_bytes(&src, ctx->pool, &bp, &len) != 0 || len >= token_size) {
        return -1;
    }
    memcpy(token, bp, len);
    token[len] = '\0';
    return 0;
}

static size_t find_substr(const uint8_t *hay, size_t hlen, const uint8_t *needle, size_t nlen) {
    size_t i;
    if (nlen == 0 || nlen > hlen) {
        return (size_t)-1;
    }
    for (i = 0; i + nlen <= hlen; i++) {
        if (memcmp(hay + i, needle, nlen) == 0) {
            return i;
        }
    }
    return (size_t)-1;
}

int flat_native_str_to_num(FlatNativeCtx *ctx) {
    char token[512];
    FlatSlotType expect;
    int has_expect = 0;

    if (!ctx->out || !ctx->io_status) {
        return str_fail(ctx, "str.to_num");
    }
    io_ok(ctx);
    if (flat_native_tag_resolve_type(ctx, FLAT_NATIVE_STR_TO_NUM, &expect, &has_expect) != 0) {
        return str_fail(ctx, "type.io.expect");
    }
    if (has_expect) {
        if (string_from_inner(ctx, token, sizeof token) != 0) {
            return str_fail(ctx, "str.to_num.type");
        }
        {
            char panic[64];
            if (flat_number_parse_for_slot_kind(flat_native_trim(token), expect.kind, ctx->out,
                                                panic, sizeof panic) != 0) {
                if (ctx->panic_detail[0] == '\0') {
                    snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s",
                             panic[0] ? panic : "type.io.expect");
                }
                return io_err(ctx);
            }
        }
        return 0;
    }
    if (string_from_inner(ctx, token, sizeof token) != 0) {
        return str_fail(ctx, "str.to_num.type");
    }
    {
        double n;
        if (flat_parse_number_token(flat_native_trim(token), &n) != 0) {
            return str_fail(ctx, "str.to_num");
        }
        return flat_value_set_number(ctx->out, n);
    }
}

int flat_native_str_to_bool(FlatNativeCtx *ctx) {
    char token[512];
    int b;

    if (!ctx->out || !ctx->io_status) {
        return str_fail(ctx, "str.to_bool");
    }
    io_ok(ctx);
    if (string_from_inner(ctx, token, sizeof token) != 0) {
        return str_fail(ctx, "str.to_bool.type");
    }
    if (flat_parse_bool_token(flat_native_trim(token), &b) != 0) {
        return str_fail(ctx, "str.to_bool");
    }
    return flat_value_set_bool(ctx->out, b);
}

int flat_native_str_len(FlatNativeCtx *ctx) {
    FlatValue s;
    char inner_buf[4096];
    size_t len;

    if (!ctx->out || !ctx->io_status) {
        return str_fail(ctx, "str.len");
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (parse_str_arg(ctx, inner_buf, &s) != 0) {
        return str_fail(ctx, "str.len.type");
    }
    {
        const uint8_t *bp;
        if (str_bytes(&s, ctx->pool, &bp, &len) != 0) {
            return str_fail(ctx, "str.len.type");
        }
    }
    return flat_value_set_number(ctx->out, (double)len);
}

int flat_native_str_repeat(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *s_expr;
    char *n_expr;
    FlatValue s;
    const uint8_t *bp;
    size_t slen;
    int n;
    size_t total;
    char *buf;

    if (!ctx->out || !ctx->io_status || !ctx->inner) {
        return str_fail(ctx, "str.repeat");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &s_expr, &n_expr) != 0) {
        return str_fail(ctx, "str.repeat.args");
    }
    io_ok(ctx);
    if (parse_str_arg(ctx, s_expr, &s) != 0) {
        return str_fail(ctx, "str.repeat.type");
    }
    if (parse_nonneg_int(ctx, n_expr, &n) != 0) {
        return str_fail(ctx, "str.repeat.args");
    }
    if (str_bytes(&s, ctx->pool, &bp, &slen) != 0) {
        return str_fail(ctx, "str.repeat.type");
    }
    if (n == 0) {
        return emit_bytes(ctx, "", 0);
    }
    total = slen * (size_t)n;
    if (slen > 0 && total / slen != (size_t)n) {
        return str_fail(ctx, "str.repeat.size");
    }
    if (total > FLAT_POOL_MAX_CHUNK) {
        return str_fail(ctx, "str.repeat.size");
    }
    buf = malloc(total ? total : 1);
    if (!buf) {
        return str_fail(ctx, "str.repeat.memory");
    }
    {
        int i;
        size_t off = 0;
        for (i = 0; i < n; i++) {
            memcpy(buf + off, bp, slen);
            off += slen;
        }
    }
    if (emit_bytes(ctx, buf, total) != 0) {
        free(buf);
        return -1;
    }
    free(buf);
    return 0;
}

int flat_native_str_concat(FlatNativeCtx *ctx) {
    char inner_buf[4096];
    char *parts[32];
    int count;
    int i;
    size_t total = 0;
    size_t off;
    char *buf;
    const uint8_t *bp;
    size_t plen;

    if (!ctx->out || !ctx->io_status || !ctx->inner) {
        return str_fail(ctx, "str.concat");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_variadic_args(inner_buf, parts, 32, &count) != 0 || count < 1) {
        return str_fail(ctx, "str.concat.args");
    }
    io_ok(ctx);
    for (i = 0; i < count; i++) {
        FlatValue v;
        if (parse_str_arg(ctx, parts[i], &v) != 0) {
            return str_fail(ctx, "str.concat.type");
        }
        if (str_bytes(&v, ctx->pool, &bp, &plen) != 0) {
            return str_fail(ctx, "str.concat.type");
        }
        total += plen;
        if (total > FLAT_POOL_MAX_CHUNK) {
            return str_fail(ctx, "str.concat.size");
        }
    }
    buf = malloc(total ? total : 1);
    if (!buf) {
        return str_fail(ctx, "str.concat.memory");
    }
    off = 0;
    for (i = 0; i < count; i++) {
        FlatValue v;
        if (parse_str_arg(ctx, parts[i], &v) != 0) {
            free(buf);
            return str_fail(ctx, "str.concat.type");
        }
        if (str_bytes(&v, ctx->pool, &bp, &plen) != 0) {
            free(buf);
            return str_fail(ctx, "str.concat.type");
        }
        memcpy(buf + off, bp, plen);
        off += plen;
    }
    if (emit_bytes(ctx, buf, total) != 0) {
        free(buf);
        return -1;
    }
    free(buf);
    return 0;
}

int flat_native_str_slice(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *s_expr;
    char *start_expr;
    char *end_expr;
    FlatValue s;
    const uint8_t *bp;
    size_t slen;
    int start;
    int end;

    if (!ctx->out || !ctx->io_status || !ctx->inner) {
        return str_fail(ctx, "str.slice");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_three_args(inner_buf, &s_expr, &start_expr, &end_expr) != 0) {
        return str_fail(ctx, "str.slice.args");
    }
    io_ok(ctx);
    if (parse_str_arg(ctx, s_expr, &s) != 0) {
        return str_fail(ctx, "str.slice.type");
    }
    if (parse_int_expr(ctx, start_expr, &start) != 0 || parse_int_expr(ctx, end_expr, &end) != 0) {
        return str_fail(ctx, "str.slice.args");
    }
    if (str_bytes(&s, ctx->pool, &bp, &slen) != 0) {
        return str_fail(ctx, "str.slice.type");
    }
    if (start < 0 || end < start || (size_t)end > slen) {
        return str_fail(ctx, "str.slice.range");
    }
    return emit_bytes(ctx, bp + (size_t)start, (size_t)(end - start));
}

int flat_native_str_index_of(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *hay_e;
    char *needle_e;
    FlatValue hay;
    FlatValue needle;
    const uint8_t *hb;
    const uint8_t *nb;
    size_t hlen;
    size_t nlen;
    size_t ix;

    if (!ctx->out || !ctx->io_status || !ctx->inner) {
        return str_fail(ctx, "str.index_of");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &hay_e, &needle_e) != 0) {
        return str_fail(ctx, "str.index_of.args");
    }
    io_ok(ctx);
    if (parse_str_arg(ctx, hay_e, &hay) != 0 || parse_str_arg(ctx, needle_e, &needle) != 0) {
        return str_fail(ctx, "str.index_of.type");
    }
    if (str_bytes(&hay, ctx->pool, &hb, &hlen) != 0 ||
        str_bytes(&needle, ctx->pool, &nb, &nlen) != 0) {
        return str_fail(ctx, "str.index_of.type");
    }
    if (nlen == 0) {
        return flat_value_set_number(ctx->out, 0.0);
    }
    ix = find_substr(hb, hlen, nb, nlen);
    if (ix == (size_t)-1) {
        return flat_value_set_number(ctx->out, -1.0);
    }
    return flat_value_set_number(ctx->out, (double)ix);
}

int flat_native_str_contains(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *hay_e;
    char *needle_e;
    FlatValue hay;
    FlatValue needle;
    const uint8_t *hb;
    const uint8_t *nb;
    size_t hlen;
    size_t nlen;
    size_t ix;

    if (!ctx->out || !ctx->io_status || !ctx->inner) {
        return str_fail(ctx, "str.contains");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &hay_e, &needle_e) != 0) {
        return str_fail(ctx, "str.contains.args");
    }
    io_ok(ctx);
    if (parse_str_arg(ctx, hay_e, &hay) != 0 || parse_str_arg(ctx, needle_e, &needle) != 0) {
        return str_fail(ctx, "str.contains.type");
    }
    if (str_bytes(&hay, ctx->pool, &hb, &hlen) != 0 ||
        str_bytes(&needle, ctx->pool, &nb, &nlen) != 0) {
        return str_fail(ctx, "str.contains.type");
    }
    if (nlen == 0) {
        return flat_value_set_bool(ctx->out, 1);
    }
    ix = find_substr(hb, hlen, nb, nlen);
    return flat_value_set_bool(ctx->out, ix != (size_t)-1 ? 1 : 0);
}

int flat_native_str_starts_with(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *s_e;
    char *pfx_e;
    FlatValue s;
    FlatValue pfx;
    const uint8_t *sb;
    const uint8_t *pb;
    size_t slen;
    size_t plen;

    if (!ctx->out || !ctx->io_status || !ctx->inner) {
        return str_fail(ctx, "str.starts_with");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &s_e, &pfx_e) != 0) {
        return str_fail(ctx, "str.starts_with.args");
    }
    io_ok(ctx);
    if (parse_str_arg(ctx, s_e, &s) != 0 || parse_str_arg(ctx, pfx_e, &pfx) != 0) {
        return str_fail(ctx, "str.starts_with.type");
    }
    if (str_bytes(&s, ctx->pool, &sb, &slen) != 0 || str_bytes(&pfx, ctx->pool, &pb, &plen) != 0) {
        return str_fail(ctx, "str.starts_with.type");
    }
    if (plen > slen) {
        return flat_value_set_bool(ctx->out, 0);
    }
    return flat_value_set_bool(ctx->out, memcmp(sb, pb, plen) == 0 ? 1 : 0);
}

int flat_native_str_ends_with(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *s_e;
    char *sfx_e;
    FlatValue s;
    FlatValue sfx;
    const uint8_t *sb;
    const uint8_t *fb;
    size_t slen;
    size_t flen;

    if (!ctx->out || !ctx->io_status || !ctx->inner) {
        return str_fail(ctx, "str.ends_with");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &s_e, &sfx_e) != 0) {
        return str_fail(ctx, "str.ends_with.args");
    }
    io_ok(ctx);
    if (parse_str_arg(ctx, s_e, &s) != 0 || parse_str_arg(ctx, sfx_e, &sfx) != 0) {
        return str_fail(ctx, "str.ends_with.type");
    }
    if (str_bytes(&s, ctx->pool, &sb, &slen) != 0 || str_bytes(&sfx, ctx->pool, &fb, &flen) != 0) {
        return str_fail(ctx, "str.ends_with.type");
    }
    if (flen > slen) {
        return flat_value_set_bool(ctx->out, 0);
    }
    return flat_value_set_bool(ctx->out, memcmp(sb + slen - flen, fb, flen) == 0 ? 1 : 0);
}

static size_t trim_bounds(const uint8_t *bp, size_t len, size_t *out_start, size_t *out_len) {
    size_t i = 0;
    size_t j = len;
    while (i < j && isspace((unsigned char)bp[i])) {
        i++;
    }
    while (j > i && isspace((unsigned char)bp[j - 1])) {
        j--;
    }
    *out_start = i;
    *out_len = j - i;
    return 0;
}

int flat_native_str_trim(FlatNativeCtx *ctx) {
    FlatValue s;
    char inner_buf[4096];
    const uint8_t *bp;
    size_t slen;
    size_t start;
    size_t tlen;

    if (!ctx->out || !ctx->io_status) {
        return str_fail(ctx, "str.trim");
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (parse_str_arg(ctx, inner_buf, &s) != 0) {
        return str_fail(ctx, "str.trim.type");
    }
    if (str_bytes(&s, ctx->pool, &bp, &slen) != 0) {
        return str_fail(ctx, "str.trim.type");
    }
    trim_bounds(bp, slen, &start, &tlen);
    return emit_bytes(ctx, bp + start, tlen);
}

int flat_native_str_split(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *s_e;
    char *sep_e;
    FlatValue s;
    FlatValue sep;
    const uint8_t *sb;
    const uint8_t *sep_b;
    size_t slen;
    size_t seplen;
    FlatValue parts[FLAT_LIST_MAX_ELEMS];
    int nparts = 0;
    size_t pos = 0;
    uint32_t list_id;

    if (!ctx->out || !ctx->io_status || !ctx->coll || !ctx->inner) {
        return str_fail(ctx, "str.split");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &s_e, &sep_e) != 0) {
        return str_fail(ctx, "str.split.args");
    }
    io_ok(ctx);
    if (parse_str_arg(ctx, s_e, &s) != 0 || parse_str_arg(ctx, sep_e, &sep) != 0) {
        return str_fail(ctx, "str.split.type");
    }
    if (str_bytes(&s, ctx->pool, &sb, &slen) != 0 ||
        str_bytes(&sep, ctx->pool, &sep_b, &seplen) != 0) {
        return str_fail(ctx, "str.split.type");
    }
    if (seplen == 0) {
        return str_fail(ctx, "str.split.sep");
    }
    while (pos <= slen) {
        size_t ix = find_substr(sb + pos, slen - pos, sep_b, seplen);
        size_t chunk_len;
        if (nparts >= FLAT_LIST_MAX_ELEMS) {
            return str_fail(ctx, "list.elems");
        }
        if (ix == (size_t)-1) {
            chunk_len = slen - pos;
            if (emit_bytes(ctx, sb + pos, chunk_len) != 0) {
                return -1;
            }
            parts[nparts++] = *ctx->out;
            break;
        }
        chunk_len = ix;
        if (emit_bytes(ctx, sb + pos, chunk_len) != 0) {
            return -1;
        }
        parts[nparts++] = *ctx->out;
        pos += ix + seplen;
    }
    if (flat_list_build(ctx->coll, ctx->pool, parts, nparts, &list_id) != 0) {
        return str_fail(ctx, "list.full");
    }
    return flat_value_set_list(ctx->out, list_id);
}

int flat_native_str_join(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *sep_e;
    char *list_e;
    FlatValue sep;
    FlatValue list_v;
    const uint8_t *sep_b;
    size_t seplen;
    int len;
    int i;
    size_t total = 0;
    size_t off;
    char *buf;

    if (!ctx->out || !ctx->io_status || !ctx->coll || !ctx->inner) {
        return str_fail(ctx, "str.join");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &sep_e, &list_e) != 0) {
        return str_fail(ctx, "str.join.args");
    }
    io_ok(ctx);
    if (parse_str_arg(ctx, sep_e, &sep) != 0) {
        return str_fail(ctx, "str.join.type");
    }
    if (flat_native_parse_expr(ctx, list_e, &list_v) != 0 || list_v.tag != VAL_LIST) {
        return str_fail(ctx, "str.join.type");
    }
    if (str_bytes(&sep, ctx->pool, &sep_b, &seplen) != 0) {
        return str_fail(ctx, "str.join.type");
    }
    if (flat_list_len(ctx->coll, list_v.u.coll_id, &len) != 0) {
        return str_fail(ctx, "str.join");
    }
    for (i = 0; i < len; i++) {
        FlatValue el;
        const uint8_t *bp;
        size_t elen;
        if (flat_list_get(ctx->coll, list_v.u.coll_id, i, &el) != 0) {
            return str_fail(ctx, "str.join");
        }
        if (str_bytes(&el, ctx->pool, &bp, &elen) != 0) {
            return str_fail(ctx, "str.join.elem");
        }
        total += elen;
        if (i + 1 < len) {
            total += seplen;
        }
        if (total > FLAT_POOL_MAX_CHUNK) {
            return str_fail(ctx, "str.join.size");
        }
    }
    buf = malloc(total ? total : 1);
    if (!buf) {
        return str_fail(ctx, "str.join.memory");
    }
    off = 0;
    for (i = 0; i < len; i++) {
        FlatValue el;
        const uint8_t *bp;
        size_t elen;
        flat_list_get(ctx->coll, list_v.u.coll_id, i, &el);
        str_bytes(&el, ctx->pool, &bp, &elen);
        memcpy(buf + off, bp, elen);
        off += elen;
        if (i + 1 < len) {
            memcpy(buf + off, sep_b, seplen);
            off += seplen;
        }
    }
    if (emit_bytes(ctx, buf, total) != 0) {
        free(buf);
        return -1;
    }
    free(buf);
    return 0;
}

static void ascii_lower_buf(uint8_t *p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (p[i] >= 'A' && p[i] <= 'Z') {
            p[i] = (uint8_t)(p[i] - 'A' + 'a');
        }
    }
}

static void ascii_upper_buf(uint8_t *p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (p[i] >= 'a' && p[i] <= 'z') {
            p[i] = (uint8_t)(p[i] - 'a' + 'A');
        }
    }
}

static int transform_ascii(FlatNativeCtx *ctx, int to_upper) {
    FlatValue s;
    char inner_buf[4096];
    const uint8_t *bp;
    size_t slen;
    char *buf;
    const char *fn = to_upper ? "str.upper" : "str.lower";
    char why[32];

    if (!ctx->out || !ctx->io_status) {
        return str_fail(ctx, fn);
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (parse_str_arg(ctx, inner_buf, &s) != 0) {
        snprintf(why, sizeof why, "%s.type", fn);
        return str_fail(ctx, why);
    }
    if (str_bytes(&s, ctx->pool, &bp, &slen) != 0) {
        snprintf(why, sizeof why, "%s.type", fn);
        return str_fail(ctx, why);
    }
    buf = malloc(slen ? slen : 1);
    if (!buf) {
        snprintf(why, sizeof why, "%s.memory", fn);
        return str_fail(ctx, why);
    }
    memcpy(buf, bp, slen);
    if (to_upper) {
        ascii_upper_buf((uint8_t *)buf, slen);
    } else {
        ascii_lower_buf((uint8_t *)buf, slen);
    }
    if (emit_bytes(ctx, buf, slen) != 0) {
        free(buf);
        return -1;
    }
    free(buf);
    return 0;
}

int flat_native_str_lower(FlatNativeCtx *ctx) {
    return transform_ascii(ctx, 0);
}

int flat_native_str_upper(FlatNativeCtx *ctx) {
    return transform_ascii(ctx, 1);
}

static int replace_impl(FlatNativeCtx *ctx, int all) {
    char inner_buf[512];
    char *s_e;
    char *old_e;
    char *new_e;
    FlatValue s;
    FlatValue old_v;
    FlatValue new_v;
    const uint8_t *sb;
    const uint8_t *ob;
    const uint8_t *nb;
    size_t slen;
    size_t olen;
    size_t nlen;
    size_t pos = 0;
    char *out;
    size_t out_cap = 0;
    size_t out_len = 0;
    const char *fn = all ? "str.replace_all" : "str.replace";

    if (!ctx->out || !ctx->io_status || !ctx->inner) {
        return str_fail(ctx, fn);
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_three_args(inner_buf, &s_e, &old_e, &new_e) != 0) {
        return str_fail(ctx, all ? "str.replace_all.args" : "str.replace.args");
    }
    io_ok(ctx);
    if (parse_str_arg(ctx, s_e, &s) != 0 || parse_str_arg(ctx, old_e, &old_v) != 0 ||
        parse_str_arg(ctx, new_e, &new_v) != 0) {
        return str_fail(ctx, all ? "str.replace_all.type" : "str.replace.type");
    }
    if (str_bytes(&s, ctx->pool, &sb, &slen) != 0 || str_bytes(&old_v, ctx->pool, &ob, &olen) != 0 ||
        str_bytes(&new_v, ctx->pool, &nb, &nlen) != 0) {
        return str_fail(ctx, all ? "str.replace_all.type" : "str.replace.type");
    }
    if (olen == 0) {
        return str_fail(ctx, all ? "str.replace_all.old" : "str.replace.old");
    }
    out = malloc(slen + 1);
    if (!out) {
        return str_fail(ctx, all ? "str.replace_all.memory" : "str.replace.memory");
    }
    out_cap = slen + 1;
    while (pos <= slen) {
        size_t ix = find_substr(sb + pos, slen - pos, ob, olen);
        if (ix == (size_t)-1) {
            size_t rest = slen - pos;
            if (out_len + rest > out_cap) {
                free(out);
                return str_fail(ctx, all ? "str.replace_all.size" : "str.replace.size");
            }
            memcpy(out + out_len, sb + pos, rest);
            out_len += rest;
            break;
        }
        ix += pos;
        if (out_len + (ix - pos) + nlen > FLAT_POOL_MAX_CHUNK) {
            free(out);
            return str_fail(ctx, all ? "str.replace_all.size" : "str.replace.size");
        }
        if (out_len + (ix - pos) + nlen + (slen - ix - olen) > out_cap) {
            out_cap = out_len + (ix - pos) + nlen + (slen - ix - olen) + 64;
            {
                char *n = realloc(out, out_cap);
                if (!n) {
                    free(out);
                    return str_fail(ctx, all ? "str.replace_all.memory" : "str.replace.memory");
                }
                out = n;
            }
        }
        memcpy(out + out_len, sb + pos, ix - pos);
        out_len += ix - pos;
        memcpy(out + out_len, nb, nlen);
        out_len += nlen;
        pos = ix + olen;
        if (!all) {
            if (out_len + (slen - pos) > out_cap) {
                free(out);
                return str_fail(ctx, "str.replace.size");
            }
            memcpy(out + out_len, sb + pos, slen - pos);
            out_len += slen - pos;
            break;
        }
    }
    if (emit_bytes(ctx, out, out_len) != 0) {
        free(out);
        return -1;
    }
    free(out);
    return 0;
}

int flat_native_str_replace(FlatNativeCtx *ctx) {
    return replace_impl(ctx, 0);
}

int flat_native_str_replace_all(FlatNativeCtx *ctx) {
    return replace_impl(ctx, 1);
}

#define FLAT_STR_FROM_NUM_DEC_MAX 15

static double from_num_round_half_away(double x) {
    if (x >= 0.0) {
        return floor(x + 0.5);
    }
    return ceil(x - 0.5);
}

static void from_num_trim_fraction(char *s) {
    char *dot;
    char *end;

    if (!s) {
        return;
    }
    dot = strchr(s, '.');
    if (!dot) {
        return;
    }
    end = s + strlen(s);
    while (end > dot + 1 && end[-1] == '0') {
        end--;
    }
    if (end == dot) {
        end--;
    }
    *end = '\0';
}

static int from_num_group_integer(const char *digits, char sep, char *out, size_t out_sz) {
    size_t n;
    size_t first;
    size_t pos = 0;
    size_t i;

    if (!digits || !out || out_sz == 0) {
        return -1;
    }
    n = strlen(digits);
    if (n == 0) {
        snprintf(out, out_sz, "0");
        return 0;
    }
    first = n % 3;
    if (first == 0) {
        first = 3;
    }
    if (pos + first >= out_sz) {
        return -1;
    }
    memcpy(out + pos, digits, first);
    pos += first;
    for (i = first; i < n; i += 3) {
        if (pos + 2 >= out_sz) {
            return -1;
        }
        out[pos++] = sep;
        if (pos + 3 >= out_sz) {
            return -1;
        }
        memcpy(out + pos, digits + i, 3);
        pos += 3;
    }
    out[pos] = '\0';
    return 0;
}

static int from_num_format(double x, int decimals, int trim_frac, char group_ch, int has_group,
                         char *out, size_t out_sz) {
    double scale;
    double rounded;
    int negative;
    double ax;
    char body[96];
    char int_raw[96];
    char int_grp[64];
    char frac[FLAT_STR_FROM_NUM_DEC_MAX + 2];
    char *dot;
    const char *int_part;
    const char *frac_part;
    size_t total_len;

    if (!out || out_sz == 0 || decimals < 0 || decimals > FLAT_STR_FROM_NUM_DEC_MAX) {
        return -1;
    }
    if (!isfinite(x)) {
        return -1;
    }
    scale = pow(10.0, (double)decimals);
    rounded = from_num_round_half_away(x * scale) / scale;
    negative = rounded < 0.0;
    ax = negative ? -rounded : rounded;

    if (decimals == 0) {
        flat_snprintf(body, sizeof body, "%.0f", ax);
    } else {
        flat_snprintf(body, sizeof body, "%.*f", decimals, ax);
    }
    if (trim_frac) {
        from_num_trim_fraction(body);
    }

    dot = strchr(body, '.');
    if (dot) {
        size_t ilen = (size_t)(dot - body);
        if (ilen >= sizeof int_raw) {
            return -1;
        }
        memcpy(int_raw, body, ilen);
        int_raw[ilen] = '\0';
        frac_part = dot + 1;
    } else {
        snprintf(int_raw, sizeof int_raw, "%s", body);
        frac_part = "";
    }
    int_part = int_raw;

    if (has_group) {
        if (from_num_group_integer(int_part, group_ch, int_grp, sizeof int_grp) != 0) {
            return -1;
        }
        int_part = int_grp;
    }

    if (frac_part[0] != '\0') {
        snprintf(frac, sizeof frac, ".%s", frac_part);
    } else {
        frac[0] = '\0';
    }

    total_len = (negative ? 1 : 0) + strlen(int_part) + strlen(frac);
    if (total_len + 1 > out_sz) {
        return -1;
    }
    if (negative) {
        snprintf(out, out_sz, "-%s%s", int_part, frac);
    } else {
        snprintf(out, out_sz, "%s%s", int_part, frac);
    }
    return 0;
}

int flat_native_str_from_num(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *x_expr;
    char *dec_expr;
    char *grp_expr;
    double x;
    int decimals;
    int trim_frac = 0;
    FlatValue grp_val;
    const uint8_t *gbp;
    size_t glen;
    char group_ch = 0;
    int has_group = 0;
    char out[128];

    if (!ctx->out || !ctx->io_status || !ctx->inner) {
        return str_fail(ctx, "str.from_num");
    }
    io_ok(ctx);
    if (flat_native_tag_resolve_trim(ctx, FLAT_NATIVE_STR_FROM_NUM, &trim_frac) != 0) {
        return str_fail(ctx, "type.tag.not_accepted");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_three_args(inner_buf, &x_expr, &dec_expr, &grp_expr) != 0) {
        snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
        if (split_two_args(inner_buf, &x_expr, &dec_expr) != 0) {
            return str_fail(ctx, "str.from_num.args");
        }
        grp_expr = NULL;
    }
    {
        FlatValue xv;
        if (flat_native_parse_expr(ctx, x_expr, &xv) != 0 || xv.tag != VAL_NUMBER) {
            return str_fail(ctx, "str.from_num.args");
        }
        x = flat_number_as_f64(&xv);
    }
    if (parse_nonneg_int(ctx, dec_expr, &decimals) != 0 ||
        decimals > FLAT_STR_FROM_NUM_DEC_MAX) {
        return str_fail(ctx, "str.from_num.args");
    }
    if (grp_expr) {
        if (parse_str_arg(ctx, grp_expr, &grp_val) != 0) {
            return str_fail(ctx, "str.from_num.group");
        }
        if (str_bytes(&grp_val, ctx->pool, &gbp, &glen) != 0) {
            return str_fail(ctx, "str.from_num.group");
        }
        if (glen > 1) {
            return str_fail(ctx, "str.from_num.group");
        }
        if (glen == 1) {
            has_group = 1;
            group_ch = (char)gbp[0];
        }
    }
    if (from_num_format(x, decimals, trim_frac, group_ch, has_group, out, sizeof out) != 0) {
        return str_fail(ctx, "str.from_num");
    }
    return emit_bytes(ctx, out, strlen(out));
}
