#include "flat_number.h"
#include "flat_date.h"
#include "flat_number.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_value.h"

#include <stdio.h>
#include <string.h>

static int io_ok(FlatNativeCtx *ctx) {
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return 0;
}

static int io_err(FlatNativeCtx *ctx) {
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_ERR;
    }
    return -1;
}

static int parse_expr(FlatNativeCtx *ctx, const char *expr, FlatValue *out) {
    char buf[512];
    if (!expr || !out) {
        return -1;
    }
    snprintf(buf, sizeof buf, "%s", expr);
    return flat_value_parse_assign_rhs(flat_native_trim(buf), out, ctx->pool, ctx->coll, ctx->vars,
                                       ctx->var_count, ctx->var_names);
}

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

static int string_from_value(const FlatValue *v, FlatStringPool *pool, char *buf, size_t buf_sz) {
    if (v->tag == VAL_STR_INLINE) {
        snprintf(buf, buf_sz, "%s", v->u.ins.bytes);
        return 0;
    }
    if (v->tag == VAL_STR_REF) {
        size_t len = 0;
        const uint8_t *bp = flat_pool_read(pool, v->u.ref_id, &len);
        if (!bp || len >= buf_sz) {
            return -1;
        }
        memcpy(buf, bp, len);
        buf[len] = '\0';
        return 0;
    }
    return -1;
}

static int emit_string(FlatNativeCtx *ctx, const char *s) {
    size_t len = strlen(s);
    uint32_t id;
    if (!ctx->out) {
        return -1;
    }
    if (len == 0) {
        return flat_value_set_str_inline(ctx->out, "", 0);
    }
    if (len <= FLAT_INLINE_STR_MAX) {
        return flat_value_set_str_inline(ctx->out, s, len);
    }
    if (flat_pool_alloc_copy(ctx->pool, s, len, &id) != 0) {
        return -1;
    }
    return flat_value_set_str_ref(ctx->out, id);
}

static int string_from_inner(FlatNativeCtx *ctx, char *token, size_t token_size) {
    FlatValue src;
    char inner_buf[4096];
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (parse_expr(ctx, inner_buf, &src) != 0) {
        return -1;
    }
    return string_from_value(&src, ctx->pool, token, token_size);
}

int flat_native_date_format(FlatNativeCtx *ctx) {
    char inner_buf[4096];
    char *a;
    char *b = NULL;
    FlatValue src;
    int64_t sec;
    char out[512];
    FlatDateRunState *st = flat_date_global_state();

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (split_two_args(inner_buf, &a, &b) != 0) {
        a = flat_native_trim(inner_buf);
        b = NULL;
    }
    if (parse_expr(ctx, a, &src) != 0) {
        return io_err(ctx);
    }
    if (flat_date_value_to_utc_sec(&src, ctx->pool, &sec, st) != 0) {
        return io_err(ctx);
    }
    if (flat_date_format(sec, b ? b : "YYYY-MM-DD", out, sizeof out, st) != 0) {
        return io_err(ctx);
    }
    return emit_string(ctx, out);
}

int flat_native_date_parse_auto(FlatNativeCtx *ctx) {
    char inner_buf[4096];
    FlatValue v;
    int64_t sec;
    FlatDateRunState *st = flat_date_global_state();

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (parse_expr(ctx, flat_native_trim(inner_buf), &v) != 0) {
        return io_err(ctx);
    }
    if (v.tag == VAL_NUMBER) {
        if (flat_date_value_to_utc_sec(&v, ctx->pool, &sec, st) != 0) {
            return io_err(ctx);
        }
        return flat_value_set_number(ctx->out, (double)sec);
    }
    if (v.tag == VAL_STR_INLINE || v.tag == VAL_STR_REF) {
        char token[512];
        if (string_from_value(&v, ctx->pool, token, sizeof token) != 0) {
            return io_err(ctx);
        }
        if (flat_date_parse_auto(flat_native_trim(token), &sec, st) != 0) {
            return io_err(ctx);
        }
        return flat_value_set_number(ctx->out, (double)sec);
    }
    return io_err(ctx);
}

int flat_native_date_parse(FlatNativeCtx *ctx) {
    char inner_buf[4096];
    char *s;
    char *fmt;
    char stok[512];
    char ftok[256];
    int64_t sec;
    FlatDateRunState *st = flat_date_global_state();

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (split_two_args(inner_buf, &s, &fmt) != 0) {
        return io_err(ctx);
    }
    {
        FlatValue vs;
        if (parse_expr(ctx, s, &vs) != 0 || string_from_value(&vs, ctx->pool, stok, sizeof stok) != 0) {
            return io_err(ctx);
        }
    }
    {
        FlatValue vf;
        if (parse_expr(ctx, fmt, &vf) != 0 ||
            string_from_value(&vf, ctx->pool, ftok, sizeof ftok) != 0) {
            return io_err(ctx);
        }
    }
    if (ftok[0] == '\0') {
        return io_err(ctx);
    }
    if (flat_date_parse_strict(stok, ftok, &sec, st) != 0) {
        return io_err(ctx);
    }
    return flat_value_set_number(ctx->out, (double)sec);
}

int flat_native_date_now(FlatNativeCtx *ctx) {
    FlatDateRunState *st = flat_date_global_state();
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    return flat_value_set_number(ctx->out, (double)flat_date_now_sec(st));
}

int flat_native_date_today(FlatNativeCtx *ctx) {
    char out[32];
    FlatDateRunState *st = flat_date_global_state();
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    if (flat_date_today_string(out, sizeof out, st) != 0) {
        return io_err(ctx);
    }
    return emit_string(ctx, out);
}

int flat_native_date_add(FlatNativeCtx *ctx) {
    char inner_buf[4096];
    char *base;
    char *mod;
    FlatValue vb;
    int64_t sec;
    char out[64];
    FlatDateRunState *st = flat_date_global_state();

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (split_two_args(inner_buf, &base, &mod) != 0) {
        return io_err(ctx);
    }
    if (parse_expr(ctx, base, &vb) != 0) {
        return io_err(ctx);
    }
    if (flat_date_value_to_utc_sec(&vb, ctx->pool, &sec, st) != 0) {
        return io_err(ctx);
    }
    {
        FlatValue vm;
        char modtok[128];
        if (parse_expr(ctx, mod, &vm) != 0 ||
            string_from_value(&vm, ctx->pool, modtok, sizeof modtok) != 0) {
            return io_err(ctx);
        }
        if (flat_date_add_modifier(sec, modtok, &sec) != 0) {
            return io_err(ctx);
        }
    }
    if (vb.tag == VAL_NUMBER) {
        return flat_value_set_number(ctx->out, (double)sec);
    }
    if (flat_date_iso_z_from_sec(sec, out, sizeof out) != 0) {
        return io_err(ctx);
    }
    return emit_string(ctx, out);
}

int flat_native_date_cmp(FlatNativeCtx *ctx) {
    char inner_buf[4096];
    char *a;
    char *b;
    FlatValue va;
    FlatValue vb;
    int64_t sa;
    int64_t sb;
    FlatDateRunState *st = flat_date_global_state();

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (split_two_args(inner_buf, &a, &b) != 0) {
        return io_err(ctx);
    }
    if (parse_expr(ctx, a, &va) != 0 || parse_expr(ctx, b, &vb) != 0) {
        return io_err(ctx);
    }
    if (flat_date_value_to_utc_sec(&va, ctx->pool, &sa, st) != 0 ||
        flat_date_value_to_utc_sec(&vb, ctx->pool, &sb, st) != 0) {
        return io_err(ctx);
    }
    if (sa < sb) {
        return flat_value_set_number(ctx->out, -1.0);
    }
    if (sa > sb) {
        return flat_value_set_number(ctx->out, 1.0);
    }
    return flat_value_set_number(ctx->out, 0.0);
}

int flat_native_date_to_seconds(FlatNativeCtx *ctx) {
    FlatValue v;
    char inner_buf[4096];
    int64_t sec;
    FlatDateRunState *st = flat_date_global_state();

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (parse_expr(ctx, flat_native_trim(inner_buf), &v) != 0) {
        return io_err(ctx);
    }
    if (flat_date_value_to_utc_sec(&v, ctx->pool, &sec, st) != 0) {
        return io_err(ctx);
    }
    return flat_value_set_number(ctx->out, (double)sec);
}

int flat_native_date_from_seconds(FlatNativeCtx *ctx) {
    FlatValue v;
    char inner_buf[4096];
    char out[64];
    int64_t sec;

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (parse_expr(ctx, flat_native_trim(inner_buf), &v) != 0 || v.tag != VAL_NUMBER) {
        return io_err(ctx);
    }
    sec = (int64_t)flat_number_as_f64(&v);
    if (flat_date_iso_z_from_sec(sec, out, sizeof out) != 0) {
        return io_err(ctx);
    }
    return emit_string(ctx, out);
}

int flat_native_date_to_ms(FlatNativeCtx *ctx) {
    if (flat_native_date_to_seconds(ctx) != 0) {
        return -1;
    }
    if (ctx->out->tag == VAL_NUMBER) {
        double d = flat_number_as_f64(ctx->out) * 1000.0;
        flat_value_set_number(ctx->out, d);
    }
    return 0;
}

int flat_native_date_from_ms(FlatNativeCtx *ctx) {
    FlatValue v;
    char inner_buf[4096];
    char out[64];
    int64_t sec;
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (parse_expr(ctx, flat_native_trim(inner_buf), &v) != 0 || v.tag != VAL_NUMBER) {
        return io_err(ctx);
    }
    sec = (int64_t)(flat_number_as_f64(&v) / 1000.0);
    if (flat_date_iso_z_from_sec(sec, out, sizeof out) != 0) {
        return io_err(ctx);
    }
    return emit_string(ctx, out);
}

int flat_native_date_time_to_sec(FlatNativeCtx *ctx) {
    char token[64];
    int sec;
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    if (string_from_inner(ctx, token, sizeof token) != 0) {
        return io_err(ctx);
    }
    if (flat_date_time_to_sec(flat_native_trim(token), &sec) != 0) {
        return io_err(ctx);
    }
    return flat_value_set_number(ctx->out, (double)sec);
}

int flat_native_date_sec_to_time(FlatNativeCtx *ctx) {
    FlatValue v;
    char inner_buf[4096];
    char out[16];
    int sec;

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    if (parse_expr(ctx, flat_native_trim(inner_buf), &v) != 0 || v.tag != VAL_NUMBER) {
        return io_err(ctx);
    }
    sec = (int)flat_number_as_f64(&v);
    if (flat_date_sec_to_time(sec, out, sizeof out) != 0) {
        return io_err(ctx);
    }
    return emit_string(ctx, out);
}

int flat_native_date_run_offset(FlatNativeCtx *ctx) {
    char token[64];
    FlatDateRunState *st = flat_date_global_state();
    if (!ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    if (string_from_inner(ctx, token, sizeof token) != 0) {
        return io_err(ctx);
    }
    flat_native_trim(token);
    if (token[0] == '\0' || strcmp(token, "Z") == 0 || strcmp(token, "+00:00") == 0) {
        st->run_offset_min = 0;
        st->has_run_offset = 0;
        return 0;
    }
    if (flat_date_parse_offset_hhmm(token, &st->run_offset_min) != 0) {
        return io_err(ctx);
    }
    st->has_run_offset = 1;
    return 0;
}

int flat_native_str_to_date(FlatNativeCtx *ctx) {
    char token[512];
    char out[32];
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    if (string_from_inner(ctx, token, sizeof token) != 0) {
        return io_err(ctx);
    }
    if (flat_date_str_to_date(flat_native_trim(token), out, sizeof out) != 0) {
        return io_err(ctx);
    }
    return emit_string(ctx, out);
}

int flat_native_str_to_time(FlatNativeCtx *ctx) {
    char token[512];
    char out[32];
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    if (string_from_inner(ctx, token, sizeof token) != 0) {
        return io_err(ctx);
    }
    if (flat_date_str_to_time(flat_native_trim(token), out, sizeof out) != 0) {
        return io_err(ctx);
    }
    return emit_string(ctx, out);
}

int flat_native_str_to_datetime(FlatNativeCtx *ctx) {
    char token[512];
    char out[64];
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    io_ok(ctx);
    if (string_from_inner(ctx, token, sizeof token) != 0) {
        return io_err(ctx);
    }
    if (flat_date_str_to_datetime(flat_native_trim(token), out, sizeof out) != 0) {
        return io_err(ctx);
    }
    return emit_string(ctx, out);
}
