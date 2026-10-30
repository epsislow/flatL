#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_value.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

static int nullary_number(FlatNativeCtx *ctx, double value) {
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_number(ctx->out, value);
}

static int inner_must_be_empty(FlatNativeCtx *ctx) {
    if (!ctx->inner || ctx->inner[0] != '\0') {
        if (ctx->io_status) {
            *ctx->io_status = FLAT_IO_ERR;
        }
        return -1;
    }
    return 0;
}

int flat_native_math_trig_num_pi(FlatNativeCtx *ctx) {
    if (inner_must_be_empty(ctx) != 0) {
        return -1;
    }
    return nullary_number(ctx, M_PI);
}

int flat_native_math_trig_num_tau(FlatNativeCtx *ctx) {
    if (inner_must_be_empty(ctx) != 0) {
        return -1;
    }
    return nullary_number(ctx, 2.0 * M_PI);
}

static double deg_to_rad_fn(double deg) {
    return deg * (M_PI / 180.0);
}

static double rad_to_deg_fn(double rad) {
    return rad * (180.0 / M_PI);
}

int flat_native_math_trig_deg_to_rad(FlatNativeCtx *ctx) {
    return unary_math(ctx, deg_to_rad_fn);
}

int flat_native_math_trig_rad_to_deg(FlatNativeCtx *ctx) {
    return unary_math(ctx, rad_to_deg_fn);
}

int flat_native_math_trig_sin_deg(FlatNativeCtx *ctx) {
    double x;
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (number_from_inner(ctx, &x) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_number(ctx->out, sin(deg_to_rad_fn(x)));
}

int flat_native_math_trig_cos_deg(FlatNativeCtx *ctx) {
    double x;
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (number_from_inner(ctx, &x) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_number(ctx->out, cos(deg_to_rad_fn(x)));
}

int flat_native_math_trig_tan_deg(FlatNativeCtx *ctx) {
    double x;
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (number_from_inner(ctx, &x) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_number(ctx->out, tan(deg_to_rad_fn(x)));
}
