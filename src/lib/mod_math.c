#include "flat_math_calc.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_slot_type.h"
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

static void calc_set_panic(FlatNativeCtx *ctx, FlatMathCalcErr err) {
    if (ctx && ctx->panic_detail[0] == '\0') {
        flat_math_calc_reason(err, ctx->panic_detail, sizeof ctx->panic_detail);
    }
}

static int calc_set_panic_arg(FlatNativeCtx *ctx) {
    if (!ctx || !ctx->io_status) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_ERR;
    snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "math.calc.arg");
    return -1;
}

static int string_from_value(FlatNativeCtx *ctx, const FlatValue *v, char *buf, size_t buf_sz) {
    if (!v || !buf || buf_sz == 0) {
        return -1;
    }
    if (v->tag == VAL_STR_INLINE) {
        if (v->u.ins.len + 1 > buf_sz) {
            return -1;
        }
        memcpy(buf, v->u.ins.bytes, v->u.ins.len);
        buf[v->u.ins.len] = '\0';
        return 0;
    }
    if (v->tag == VAL_STR_REF && ctx && ctx->pool) {
        size_t len = 0;
        const uint8_t *p = flat_pool_read(ctx->pool, v->u.ref_id, &len);
        if (!p || len + 1 > buf_sz) {
            return -1;
        }
        memcpy(buf, p, len);
        buf[len] = '\0';
        return 0;
    }
    return -1;
}

static int type_spec_from_expr(FlatNativeCtx *ctx, const char *expr, FlatSlotType *out) {
    FlatValue v;
    char spec[96];
    const FlatSchemaCatalog *cat;

    if (!ctx || !expr || !out) {
        return -1;
    }
    if (parse_expr(ctx, expr, &v) != 0) {
        return -1;
    }
    if (string_from_value(ctx, &v, spec, sizeof spec) != 0) {
        return -1;
    }
    cat = ctx->bc ? &ctx->bc->schemas : NULL;
    if (flat_slot_type_parse_spec(spec, out, cat, NULL, 0) != 0) {
        return -1;
    }
    if (!flat_slot_type_is_numeric(out)) {
        return -1;
    }
    return 0;
}

int flat_native_math_calc(FlatNativeCtx *ctx) {
    FlatValue src;
    char inner_buf[4096];
    char expr[4096];
    char *expr_e;
    char *type_e = NULL;
    double result;
    FlatMathCalcErr err;
    char type_panic[64];
    FlatSlotType result_ty;
    int has_result_ty = 0;

    if (!ctx || !ctx->out || !ctx->io_status) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    *ctx->io_status = FLAT_IO_OK;
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner ? ctx->inner : "");
    expr_e = flat_native_trim(inner_buf);
    if (split_two_args(inner_buf, &expr_e, &type_e) == 0 && type_e && type_e[0] != '\0') {
        if (type_spec_from_expr(ctx, type_e, &result_ty) != 0) {
            *ctx->io_status = FLAT_IO_ERR;
            snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.calc.type");
            return -1;
        }
        has_result_ty = 1;
    }
    if (parse_expr(ctx, expr_e, &src) != 0) {
        return calc_set_panic_arg(ctx);
    }
    if (string_from_value(ctx, &src, expr, sizeof expr) != 0) {
        return calc_set_panic_arg(ctx);
    }
    if (expr[0] == '\0') {
        return calc_set_panic_arg(ctx);
    }
    err = flat_math_calc_eval_strict(expr, ctx->vars, ctx->var_count, ctx->var_names, ctx->pool,
                                     ctx->coll, ctx->var_slot_types, ctx->var_has_slot_type,
                                     &result, type_panic, sizeof type_panic);
    if (err != FLAT_MATH_CALC_OK) {
        *ctx->io_status = FLAT_IO_ERR;
        if (err == FLAT_MATH_CALC_TYPE_STRICT) {
            snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s",
                     type_panic[0] ? type_panic : "type.calc.type");
        } else {
            calc_set_panic(ctx, err);
        }
        return -1;
    }
    if (has_result_ty) {
        FlatValue nv;
        FlatValue staged;
        char chk[64];
        const FlatSchemaCatalog *cat = ctx->bc ? &ctx->bc->schemas : NULL;

        flat_value_set_number(&nv, result);
        staged = nv;
        if (flat_slot_type_check_value(&result_ty, &nv, ctx->pool, ctx->coll, cat, &staged, chk,
                                       sizeof chk) != 0) {
            const char *reason = "type.calc.range";
            if (chk[0] != '\0' && strncmp(chk, "type.assign.", 12) == 0) {
                reason = "type.calc.range";
            } else if (chk[0] != '\0') {
                reason = chk;
            }
            *ctx->io_status = FLAT_IO_ERR;
            snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", reason);
            return -1;
        }
        if (staged.tag == VAL_NUMBER) {
            return flat_value_set_number(ctx->out, staged.u.number);
        }
        *ctx->out = staged;
        return 0;
    }
    return flat_value_set_number(ctx->out, result);
}
