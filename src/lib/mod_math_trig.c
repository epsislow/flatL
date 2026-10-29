#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_value.h"

#include <math.h>
#include <string.h>

static int number_from_inner(FlatNativeCtx *ctx, double *out_num) {
    FlatValue src;
    char inner_buf[512];

    if (!ctx->inner || !out_num) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (flat_value_parse_assign_rhs(flat_native_trim(inner_buf), &src, ctx->pool, ctx->coll,
                                    ctx->vars, ctx->var_count, ctx->var_names) != 0) {
        return -1;
    }
    if (src.tag != VAL_NUMBER) {
        return -1;
    }
    *out_num = src.u.number;
    return 0;
}

static int unary_math(FlatNativeCtx *ctx, double (*fn)(double)) {
    double x;

    if (!ctx->out || !ctx->io_status || !fn) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (number_from_inner(ctx, &x) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_number(ctx->out, fn(x));
}

int flat_native_math_trig_sin(FlatNativeCtx *ctx) {
    return unary_math(ctx, sin);
}

int flat_native_math_trig_cos(FlatNativeCtx *ctx) {
    return unary_math(ctx, cos);
}

int flat_native_math_trig_tan(FlatNativeCtx *ctx) {
    return unary_math(ctx, tan);
}

int flat_native_math_trig_sqrt(FlatNativeCtx *ctx) {
    return unary_math(ctx, sqrt);
}
