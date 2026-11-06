#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_vcx.h"
#include "flat_value.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int vcx_number_to_uint32_strict(double d, uint32_t *out) {
    if (!out || d != d || d < 0.0 || d > (double)UINT32_MAX) {
        return -1;
    }
    if (floor(d) != d) {
        return -1;
    }
    *out = (uint32_t)d;
    return 0;
}

static int vcx_out_num(FlatNativeCtx *ctx, double n) {
    if (!ctx || !ctx->out || !ctx->io_status) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_number(ctx->out, n);
}

static int vcx_out_bool(FlatNativeCtx *ctx, int b) {
    if (!ctx || !ctx->out || !ctx->io_status) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_bool(ctx->out, b);
}

static int vcx_fail(FlatNativeCtx *ctx, const char *reason) {
    if (ctx && ctx->io_status) {
        *ctx->io_status = FLAT_IO_ERR;
    }
    if (ctx && reason && reason != ctx->panic_detail) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", reason);
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

static int parse_ctx_id(FlatNativeCtx *ctx, const char *expr, uint32_t *out_id) {
    FlatValue v;
    char buf[512];

    if (!expr || !out_id) {
        return -1;
    }
    snprintf(buf, sizeof buf, "%s", expr);
    flat_native_trim(buf);
    if (flat_native_is_call_line(buf)) {
        if (flat_native_eval_call(ctx, buf, &v) != 0) {
            return -1;
        }
    } else if (parse_expr(ctx, expr, &v) != 0) {
        return -1;
    }
    if (v.tag != VAL_NUMBER) {
        return -1;
    }
    if (vcx_number_to_uint32_strict(v.u.number, out_id) != 0) {
        return -1;
    }
    if (*out_id < 1u) {
        return -1;
    }
    return 0;
}

static int parse_name_string(FlatNativeCtx *ctx, const char *expr, char *name, size_t name_sz) {
    FlatValue v;
    if (parse_expr(ctx, expr, &v) != 0) {
        return -1;
    }
    if (v.tag == VAL_STR_INLINE) {
        snprintf(name, name_sz, "%s", v.u.ins.bytes);
        return 0;
    }
    if (v.tag == VAL_STR_REF && ctx->pool) {
        return flat_value_get_cstr(&v, ctx->pool, name, name_sz);
    }
    return -1;
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

static FlatVcxRuntime *rt_from(FlatNativeCtx *ctx) {
    if (ctx && ctx->vcx) {
        return ctx->vcx;
    }
    return flat_vcx_runtime_active();
}

int flat_native_vcx_current(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    if (!rt) {
        return vcx_fail(ctx, "vcx.current");
    }
    ctx->panic_detail[0] = '\0';
    return vcx_out_num(ctx, (double)flat_vcx_current_id(rt));
}

int flat_native_vcx_set(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    uint32_t id;
    char inner[512];

    if (!rt || !ctx) {
        return vcx_fail(ctx, "vcx.set");
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (inner[0] == '\0' || parse_ctx_id(ctx, inner, &id) != 0) {
        return vcx_fail(ctx, "vcx.set.arg");
    }
    if (flat_vcx_set(rt, id, ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.set.invalid");
    }
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return 0;
}

int flat_native_vcx_create(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    FlatValue capv;
    uint32_t cap;
    uint32_t new_id;
    char inner[512];

    if (!rt) {
        return vcx_fail(ctx, "vcx.create");
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (inner[0] == '\0' || parse_expr(ctx, inner, &capv) != 0 || capv.tag != VAL_NUMBER) {
        return vcx_fail(ctx, "vcx.create.arg");
    }
    if (vcx_number_to_uint32_strict(capv.u.number, &cap) != 0 || cap < 1u) {
        return vcx_fail(ctx, "vcx.create.arg");
    }
    if (flat_vcx_create(rt, cap, &new_id, ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.create");
    }
    return vcx_out_num(ctx, (double)new_id);
}

int flat_native_vcx_var(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    char inner[512];
    char *a;
    char *b;
    uint32_t src_id;
    char name[FLAT_VAR_NAME_MAX + 1];
    FlatValue v;

    FlatValue sink;
    FlatValue *out;

    if (!rt || !ctx) {
        return vcx_fail(ctx, "vcx.var");
    }
    out = ctx->out ? ctx->out : &sink;
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (split_two_args(inner, &a, &b) != 0) {
        return vcx_fail(ctx, "vcx.var.arg");
    }
    if (parse_ctx_id(ctx, a, &src_id) != 0 || parse_name_string(ctx, b, name, sizeof name) != 0) {
        return vcx_fail(ctx, "vcx.var.arg");
    }
    if (flat_vcx_get_var(rt, src_id, name, &v, ctx->coll, ctx->pool, ctx->file_tbl,
                         ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.var.missing");
    }
    *ctx->io_status = FLAT_IO_OK;
    *out = v;
    return 0;
}

int flat_native_vcx_has_var(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    char inner[512];
    char *a;
    char *b;
    uint32_t cid;
    char name[FLAT_VAR_NAME_MAX + 1];
    int ok = 0;

    if (!rt) {
        return vcx_fail(ctx, "vcx.hasVar");
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (split_two_args(inner, &a, &b) != 0) {
        return vcx_fail(ctx, "vcx.hasVar.arg");
    }
    if (parse_ctx_id(ctx, a, &cid) != 0 || parse_name_string(ctx, b, name, sizeof name) != 0) {
        return vcx_fail(ctx, "vcx.hasVar.arg");
    }
    if (flat_vcx_has_var(rt, cid, name, &ok, ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.hasVar.ctx");
    }
    return vcx_out_bool(ctx, ok);
}

int flat_native_vcx_vars_copy(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    char inner[512];
    char *a;
    char *b;
    uint32_t src;
    uint32_t dst;

    if (!rt) {
        return vcx_fail(ctx, "vcx.varsCopy");
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (split_two_args(inner, &a, &b) != 0) {
        return vcx_fail(ctx, "vcx.varsCopy.arg");
    }
    if (parse_ctx_id(ctx, a, &src) != 0 || parse_ctx_id(ctx, b, &dst) != 0) {
        return vcx_fail(ctx, "vcx.varsCopy.arg");
    }
    if (flat_vcx_vars_copy(rt, src, dst, ctx->coll, ctx->pool, ctx->file_tbl, ctx->mem_live,
                           ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.varsCopy");
    }
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return 0;
}

int flat_native_vcx_vars_copy_like(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    char inner[512];
    char *a;
    char *b;
    char *c;
    uint32_t src;
    uint32_t dst;
    char pattern[256];

    if (!rt) {
        return vcx_fail(ctx, "vcx.varsCopyLike");
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (split_three_args(inner, &a, &b, &c) != 0) {
        return vcx_fail(ctx, "vcx.varsCopyLike.arg");
    }
    if (parse_ctx_id(ctx, a, &src) != 0 || parse_ctx_id(ctx, b, &dst) != 0) {
        return vcx_fail(ctx, "vcx.varsCopyLike.arg");
    }
    if (parse_name_string(ctx, c, pattern, sizeof pattern) != 0) {
        return vcx_fail(ctx, "vcx.varsCopyLike.pattern");
    }
    if (flat_vcx_vars_copy_like(rt, src, dst, pattern, ctx->coll, ctx->pool, ctx->file_tbl,
                                ctx->mem_live, ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.varsCopyLike");
    }
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return 0;
}

static int vcx_ctx_stat(FlatNativeCtx *ctx, const char *fn,
                        int (*fn_stat)(const FlatVcxRuntime *, uint32_t, int *, char *, size_t)) {
    FlatVcxRuntime *rt = rt_from(ctx);
    uint32_t id;
    int n;
    char inner[512];

    if (!rt) {
        return vcx_fail(ctx, fn);
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (inner[0] == '\0' || parse_ctx_id(ctx, inner, &id) != 0) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s.arg", fn);
        return vcx_fail(ctx, ctx->panic_detail);
    }
    if (fn_stat(rt, id, &n, ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : fn);
    }
    return vcx_out_num(ctx, (double)n);
}

int flat_native_vcx_vars_used(FlatNativeCtx *ctx) {
    return vcx_ctx_stat(ctx, "vcx.varsUsed", flat_vcx_vars_used);
}

int flat_native_vcx_vars_avail(FlatNativeCtx *ctx) {
    return vcx_ctx_stat(ctx, "vcx.varsAvail", flat_vcx_vars_avail);
}

int flat_native_vcx_vars_max(FlatNativeCtx *ctx) {
    return vcx_ctx_stat(ctx, "vcx.varsMax", flat_vcx_vars_max);
}
