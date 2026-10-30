#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_value.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) {
        return -1;
    }
    if (da > db) {
        return 1;
    }
    return 0;
}

static int compute_mean(const double *nums, int n, double *out_mean) {
    double s = 0.0;
    int i;
    if (n <= 0 || !out_mean) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        s += nums[i];
    }
    *out_mean = s / (double)n;
    return 0;
}

static int unary_list_stat(FlatNativeCtx *ctx, double (*fn)(const double *, int)) {
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

static double fn_mean(const double *nums, int n) {
    double m;
    if (compute_mean(nums, n, &m) != 0) {
        return 0.0;
    }
    return m;
}

static double fn_median(const double *nums, int n) {
    double copy[FLAT_LIST_MAX_ELEMS];
    int i;

    if (n <= 0) {
        return 0.0;
    }
    for (i = 0; i < n; i++) {
        copy[i] = nums[i];
    }
    qsort(copy, (size_t)n, sizeof copy[0], cmp_double);
    if (n % 2 == 1) {
        return copy[n / 2];
    }
    return (copy[n / 2 - 1] + copy[n / 2]) / 2.0;
}

static int binary_var_stdev(FlatNativeCtx *ctx, int is_stdev) {
    char inner_buf[512];
    char *left;
    char *right;
    double nums[FLAT_LIST_MAX_ELEMS];
    int n;
    FlatValue div_v;
    double mean;
    double ss = 0.0;
    double var;
    double div;
    int i;

    if (!ctx->inner || !ctx->out || !ctx->io_status) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (list_from_inner(ctx, left, nums, FLAT_LIST_MAX_ELEMS, &n) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (parse_expr(ctx, right, &div_v) != 0 || div_v.tag != VAL_NUMBER) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    div = div_v.u.number;
    if (div <= 0.0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (compute_mean(nums, n, &mean) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    for (i = 0; i < n; i++) {
        double d = nums[i] - mean;
        ss += d * d;
    }
    var = ss / div;
    if (is_stdev) {
        return flat_value_set_number(ctx->out, sqrt(var));
    }
    return flat_value_set_number(ctx->out, var);
}

int flat_native_math_stats_mean(FlatNativeCtx *ctx) {
    return unary_list_stat(ctx, fn_mean);
}

int flat_native_math_stats_median(FlatNativeCtx *ctx) {
    return unary_list_stat(ctx, fn_median);
}

int flat_native_math_stats_var(FlatNativeCtx *ctx) {
    return binary_var_stdev(ctx, 0);
}

int flat_native_math_stats_stdev(FlatNativeCtx *ctx) {
    return binary_var_stdev(ctx, 1);
}
