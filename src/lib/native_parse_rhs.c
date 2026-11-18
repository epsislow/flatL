#include "flat_native.h"
#include "flat_native_util.h"
#include "flat_value.h"

#include <stdio.h>
#include <string.h>

int flat_native_parse_expr(FlatNativeCtx *ctx, const char *expr, FlatValue *out) {
    char buf[512];

    if (!ctx || !expr || !out) {
        return -1;
    }
    snprintf(buf, sizeof buf, "%s", expr);
    return flat_value_parse_assign_rhs(flat_native_trim(buf), out, ctx->pool, ctx->coll, ctx->vars,
                                       ctx->var_count, ctx->var_names);
}

int flat_native_parse_rhs(FlatNativeCtx *ctx, const char *expr, FlatValue *out) {
    char buf[512];

    if (!ctx || !expr || !out) {
        return -1;
    }
    snprintf(buf, sizeof buf, "%s", expr);
    if (flat_value_parse_assign_rhs(flat_native_trim(buf), out, ctx->pool, ctx->coll, ctx->vars,
                                    ctx->var_count, ctx->var_names) != 0) {
        return -1;
    }
    if (out->tag == VAL_LIST || out->tag == VAL_OBJECT) {
        flat_value_retain(ctx->coll, ctx->pool, out);
    }
    return 0;
}

void flat_native_parse_rhs_release(FlatNativeCtx *ctx, const FlatValue *v) {
    if (!ctx || !v) {
        return;
    }
    if (v->tag == VAL_LIST || v->tag == VAL_OBJECT) {
        flat_value_release(ctx->coll, ctx->pool, v);
    }
}