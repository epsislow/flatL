#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_value.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int split_two_args(char *inner, char **left, char **right) {
    int depth = 0;
    char *p;

    for (p = inner; *p; p++) {
        if (*p == '[' || *p == '(') {
            depth++;
        } else if (*p == ']' || *p == ')') {
            if (depth > 0) {
                depth--;
            }
        } else if (*p == ',' && depth == 0) {
            *p = '\0';
            *left = flat_native_trim(inner);
            *right = flat_native_trim(p + 1);
            return 0;
        }
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

static int number_from_expr(FlatNativeCtx *ctx, const char *expr, double *out_num) {
    FlatValue src;

    if (parse_expr(ctx, expr, &src) != 0 || src.tag != VAL_NUMBER) {
        return -1;
    }
    *out_num = src.u.number;
    return 0;
}

static int number_from_inner(FlatNativeCtx *ctx, double *out_num) {
    if (!ctx->inner || !out_num) {
        return -1;
    }
    return number_from_expr(ctx, ctx->inner, out_num);
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

static int unary_number(FlatNativeCtx *ctx, double (*fn)(double)) {
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

static int binary_number(FlatNativeCtx *ctx, double (*fn)(double, double)) {
    char inner_buf[512];
    char *left;
    char *right;
    double a;
    double b;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !fn) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (number_from_expr(ctx, left, &a) != 0 || number_from_expr(ctx, right, &b) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_number(ctx->out, fn(a, b));
}

static double round_half_away(double x) {
    if (x >= 0.0) {
        return floor(x + 0.5);
    }
    return ceil(x - 0.5);
}

static int list_to_numbers(FlatNativeCtx *ctx, const FlatValue *list_val, double *nums,
                           int max_nums, int *out_count) {
    int len;
    int i;

    if (!list_val || list_val->tag != VAL_LIST || !out_count) {
        return -1;
    }
    if (flat_list_len(ctx->coll, list_val->u.coll_id, &len) != 0 || len <= 0) {
        return -1;
    }
    if (len > max_nums) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        FlatValue elem;
        if (flat_list_get(ctx->coll, list_val->u.coll_id, i, &elem) != 0) {
            return -1;
        }
        if (elem.tag != VAL_NUMBER) {
            return -1;
        }
        nums[i] = elem.u.number;
    }
    *out_count = len;
    return 0;
}

static int list_from_inner(FlatNativeCtx *ctx, const char *inner, double *nums, int max_nums,
                           int *out_count) {
    FlatValue src;

    if (parse_expr(ctx, inner, &src) != 0) {
        return -1;
    }
    return list_to_numbers(ctx, &src, nums, max_nums, out_count);
}

static int unary_list_agg(FlatNativeCtx *ctx, double (*fn)(const double *, int)) {
    double nums[FLAT_LIST_MAX_ELEMS];
    int n;

    if (!ctx->out || !ctx->io_status || !fn) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (list_from_inner(ctx, ctx->inner, nums, FLAT_LIST_MAX_ELEMS, &n) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_number(ctx->out, fn(nums, n));
}

static double agg_sum(const double *nums, int n) {
    double s = 0.0;
    int i;
    for (i = 0; i < n; i++) {
        s += nums[i];
    }
    return s;
}

static double agg_min(const double *nums, int n) {
    double m = nums[0];
    int i;
    for (i = 1; i < n; i++) {
        if (nums[i] < m) {
            m = nums[i];
        }
    }
    return m;
}

static double agg_max(const double *nums, int n) {
    double m = nums[0];
    int i;
    for (i = 1; i < n; i++) {
        if (nums[i] > m) {
            m = nums[i];
        }
    }
    return m;
}

static int binary_agg2(FlatNativeCtx *ctx, double (*fn)(double, double)) {
    char inner_buf[512];
    char *left;
    char *right;
    double a;
    double b;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !fn) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (number_from_expr(ctx, left, &a) != 0 || number_from_expr(ctx, right, &b) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_number(ctx->out, fn(a, b));
}

static double fn_sum2(double a, double b) {
    return a + b;
}

static double fn_min2(double a, double b) {
    return a < b ? a : b;
}

static double fn_max2(double a, double b) {
    return a > b ? a : b;
}

static int unary_log(FlatNativeCtx *ctx, double (*fn)(double)) {
    double x;

    if (!ctx->out || !ctx->io_status || !fn) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (number_from_inner(ctx, &x) != 0 || x <= 0.0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_number(ctx->out, fn(x));
}

static int binary_floor_mod(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *left;
    char *right;
    double a;
    double b;
    double r;

    if (!ctx->inner || !ctx->out || !ctx->io_status) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (number_from_expr(ctx, left, &a) != 0 || number_from_expr(ctx, right, &b) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (b == 0.0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    r = a - floor(a / b) * b;
    return flat_value_set_number(ctx->out, r);
}

int flat_native_math_pow(FlatNativeCtx *ctx) {
    return binary_number(ctx, pow);
}

int flat_native_math_sqrt(FlatNativeCtx *ctx) {
    double x;
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (number_from_inner(ctx, &x) != 0 || x < 0.0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_number(ctx->out, sqrt(x));
}

int flat_native_math_exp(FlatNativeCtx *ctx) {
    return unary_number(ctx, exp);
}

int flat_native_math_log(FlatNativeCtx *ctx) {
    return unary_log(ctx, log);
}

int flat_native_math_log10(FlatNativeCtx *ctx) {
    return unary_log(ctx, log10);
}

int flat_native_math_floor(FlatNativeCtx *ctx) {
    return unary_number(ctx, floor);
}

int flat_native_math_ceil(FlatNativeCtx *ctx) {
    return unary_number(ctx, ceil);
}

int flat_native_math_round(FlatNativeCtx *ctx) {
    double x;
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (number_from_inner(ctx, &x) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_number(ctx->out, round_half_away(x));
}

int flat_native_math_mod(FlatNativeCtx *ctx) {
    return binary_number(ctx, fmod);
}

int flat_native_math_floor_mod(FlatNativeCtx *ctx) {
    return binary_floor_mod(ctx);
}

int flat_native_math_sum(FlatNativeCtx *ctx) {
    return unary_list_agg(ctx, agg_sum);
}

int flat_native_math_min(FlatNativeCtx *ctx) {
    return unary_list_agg(ctx, agg_min);
}

int flat_native_math_max(FlatNativeCtx *ctx) {
    return unary_list_agg(ctx, agg_max);
}

int flat_native_math_sum2(FlatNativeCtx *ctx) {
    return binary_agg2(ctx, fn_sum2);
}

int flat_native_math_min2(FlatNativeCtx *ctx) {
    return binary_agg2(ctx, fn_min2);
}

int flat_native_math_max2(FlatNativeCtx *ctx) {
    return binary_agg2(ctx, fn_max2);
}

int flat_native_math_num_pi(FlatNativeCtx *ctx) {
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    if (inner_must_be_empty(ctx) != 0) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_number(ctx->out, M_PI);
}

int flat_native_math_num_tau(FlatNativeCtx *ctx) {
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    if (inner_must_be_empty(ctx) != 0) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_number(ctx->out, 2.0 * M_PI);
}

int flat_native_math_num_e(FlatNativeCtx *ctx) {
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    if (inner_must_be_empty(ctx) != 0) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_number(ctx->out, exp(1.0));
}
