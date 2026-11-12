/* VCX dSchema* natives — kept separate to avoid growing mod_vcx.c further. */
#include "flat_number.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_vcx.h"
#include "flat_value.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int vcx_fail(FlatNativeCtx *ctx, const char *reason) {
    if (ctx && ctx->io_status) {
        *ctx->io_status = FLAT_IO_ERR;
    }
    if (ctx && reason && reason != ctx->panic_detail) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", reason);
    }
    return -1;
}

static FlatVcxRuntime *rt_from(FlatNativeCtx *ctx) {
    return ctx && ctx->vcx ? ctx->vcx : flat_vcx_runtime_active();
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
    if (!expr || !out_id) {
        return -1;
    }
    if (parse_expr(ctx, expr, &v) != 0) {
        return -1;
    }
    if (v.tag != VAL_NUMBER) {
        return -1;
    }
    if (flat_number_as_f64(&v) != floor(flat_number_as_f64(&v)) || flat_number_as_f64(&v) < 0.0 || flat_number_as_f64(&v) > (double)UINT32_MAX) {
        return -1;
    }
    *out_id = (uint32_t)flat_number_as_f64(&v);
    return 0;
}

static int split_two_args(char *inner, char **a, char **b) {
    char *comma;
    if (!inner || !a || !b) {
        return -1;
    }
    comma = strchr(inner, ',');
    if (!comma) {
        return -1;
    }
    *comma = '\0';
    *a = flat_native_trim(inner);
    *b = flat_native_trim(comma + 1);
    return (*a)[0] && (*b)[0] ? 0 : -1;
}

int flat_native_vcx_dschema_allow(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    char inner[256];
    char *a;
    char *b;
    uint32_t cid;
    FlatValue allow_v;
    int allow = 0;

    if (!rt) {
        return vcx_fail(ctx, "vcx.dSchemaAllow");
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (split_two_args(inner, &a, &b) != 0) {
        return vcx_fail(ctx, "vcx.dSchemaAllow.arg");
    }
    if (parse_ctx_id(ctx, a, &cid) != 0) {
        return vcx_fail(ctx, "vcx.dSchemaAllow.arg");
    }
    if (parse_expr(ctx, b, &allow_v) != 0 || allow_v.tag != VAL_NUMBER) {
        return vcx_fail(ctx, "vcx.dSchemaAllow.arg");
    }
    if (flat_number_as_f64(&allow_v) != 0.0 && flat_number_as_f64(&allow_v) != 1.0) {
        return vcx_fail(ctx, "vcx.dSchemaAllow.arg");
    }
    allow = flat_number_as_f64(&allow_v) != 0.0 ? 1 : 0;
    if (flat_vcx_dschema_allow(rt, cid, allow, ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.dSchemaAllow");
    }
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return 0;
}

int flat_native_vcx_dschema_list(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    char inner[512];
    char *a;
    char *b;
    uint32_t cid;
    FlatValue list_v;

    if (!rt) {
        return vcx_fail(ctx, "vcx.dSchemaList");
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (split_two_args(inner, &a, &b) != 0) {
        return vcx_fail(ctx, "vcx.dSchemaList.arg");
    }
    if (parse_ctx_id(ctx, a, &cid) != 0) {
        return vcx_fail(ctx, "vcx.dSchemaList.arg");
    }
    if (parse_expr(ctx, b, &list_v) != 0) {
        return vcx_fail(ctx, "vcx.dSchemaList.arg");
    }
    if (flat_vcx_dschema_list(rt, cid, &list_v, ctx->coll, ctx->pool, ctx->panic_detail,
                              sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.dSchemaList");
    }
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return 0;
}
