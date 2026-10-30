#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_value.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_expr(FlatNativeCtx *ctx, const char *expr, FlatValue *out) {
    char buf[512];

    if (!expr || !out) {
        return -1;
    }
    snprintf(buf, sizeof buf, "%s", expr);
    return flat_value_parse_assign_rhs(flat_native_trim(buf), out, ctx->pool, ctx->coll, ctx->vars,
                                       ctx->var_count, ctx->var_names);
}

static int split_three_args(char *inner, char **first, char **second, char **third) {
    int depth = 0;
    char *p;
    char *c1 = NULL;
    char *c2 = NULL;

    for (p = inner; *p; p++) {
        if (*p == '[' || *p == '(') {
            depth++;
        } else if (*p == ']' || *p == ')') {
            if (depth > 0) {
                depth--;
            }
        } else if (*p == ',' && depth == 0) {
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
        *out_count = 0;
        return 0;
    }
    parts[n++] = start;
    for (p = start; *p; p++) {
        if (*p == '[' || *p == '(') {
            depth++;
        } else if (*p == ']' || *p == ')') {
            if (depth > 0) {
                depth--;
            }
        } else if (*p == ',' && depth == 0) {
            *p = '\0';
            start = flat_native_trim(p + 1);
            if (*start == '\0') {
                return -1;
            }
            if (n >= max_parts) {
                return -1;
            }
            parts[n++] = start;
            p = start;
        }
    }
    *out_count = n;
    return 0;
}

int flat_native_list_empty(FlatNativeCtx *ctx) {
    uint32_t id;

    if (!ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    if (ctx->inner && ctx->inner[0] != '\0') {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (flat_list_empty(ctx->coll, ctx->pool, &id) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_list(ctx->out, id);
}

int flat_native_list_pack(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *parts[FLAT_LIST_MAX_ELEMS];
    FlatValue items[FLAT_LIST_MAX_ELEMS];
    int count;
    int i;
    uint32_t id;

    if (!ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (!ctx->inner) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_variadic_args(inner_buf, parts, FLAT_LIST_MAX_ELEMS, &count) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (count == 0) {
        if (flat_list_empty(ctx->coll, ctx->pool, &id) != 0) {
            *ctx->io_status = FLAT_IO_ERR;
            return -1;
        }
        return flat_value_set_list(ctx->out, id);
    }
    for (i = 0; i < count; i++) {
        if (parse_expr(ctx, parts[i], &items[i]) != 0) {
            *ctx->io_status = FLAT_IO_ERR;
            return -1;
        }
    }
    if (flat_list_build(ctx->coll, ctx->pool, items, count, &id) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_list(ctx->out, id);
}

int flat_native_list_push(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *left;
    char *right;
    FlatValue list_v;
    FlatValue item;
    uint32_t new_id;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_expr(ctx, left, &list_v) != 0 || list_v.tag != VAL_LIST) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (parse_expr(ctx, right, &item) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (flat_list_push_copy(ctx->coll, ctx->pool, list_v.u.coll_id, &item, &new_id) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_list(ctx->out, new_id);
}

int flat_native_list_get(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *left;
    char *right;
    FlatValue list_v;
    FlatValue idx_v;
    int ix;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_expr(ctx, left, &list_v) != 0 || list_v.tag != VAL_LIST) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (parse_expr(ctx, right, &idx_v) != 0 || idx_v.tag != VAL_NUMBER) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    ix = (int)idx_v.u.number;
    if (flat_list_get(ctx->coll, list_v.u.coll_id, ix, ctx->out) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return 0;
}

int flat_native_list_len(FlatNativeCtx *ctx) {
    FlatValue list_v;
    int len;

    if (!ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_expr(ctx, ctx->inner, &list_v) != 0 || list_v.tag != VAL_LIST) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (flat_list_len(ctx->coll, list_v.u.coll_id, &len) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_number(ctx->out, (double)len);
}

static int parse_list_arg(FlatNativeCtx *ctx, const char *expr, FlatValue *list_v) {
    return parse_expr(ctx, expr, list_v) == 0 && list_v->tag == VAL_LIST ? 0 : -1;
}

int flat_native_list_prepend(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *left;
    char *right;
    FlatValue list_v;
    FlatValue item;
    uint32_t new_id;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_list_arg(ctx, left, &list_v) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (parse_expr(ctx, right, &item) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (flat_list_prepend_copy(ctx->coll, ctx->pool, list_v.u.coll_id, &item, &new_id) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_list(ctx->out, new_id);
}

int flat_native_list_insert(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *list_s;
    char *idx_s;
    char *val_s;
    FlatValue list_v;
    FlatValue idx_v;
    FlatValue item;
    uint32_t new_id;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_three_args(inner_buf, &list_s, &idx_s, &val_s) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_list_arg(ctx, list_s, &list_v) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (parse_expr(ctx, idx_s, &idx_v) != 0 || idx_v.tag != VAL_NUMBER) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (parse_expr(ctx, val_s, &item) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (flat_list_insert_copy(ctx->coll, ctx->pool, list_v.u.coll_id, (int)idx_v.u.number, &item, &new_id) !=
        0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_list(ctx->out, new_id);
}

int flat_native_list_set(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *list_s;
    char *idx_s;
    char *val_s;
    FlatValue list_v;
    FlatValue idx_v;
    FlatValue item;
    uint32_t new_id;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_three_args(inner_buf, &list_s, &idx_s, &val_s) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_list_arg(ctx, list_s, &list_v) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (parse_expr(ctx, idx_s, &idx_v) != 0 || idx_v.tag != VAL_NUMBER) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (parse_expr(ctx, val_s, &item) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (flat_list_set_copy(ctx->coll, ctx->pool, list_v.u.coll_id, (int)idx_v.u.number, &item, &new_id) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_list(ctx->out, new_id);
}

int flat_native_list_index_of(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *left;
    char *right;
    FlatValue list_v;
    FlatValue needle;
    int ix;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_list_arg(ctx, left, &list_v) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (parse_expr(ctx, right, &needle) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (flat_list_index_of(ctx->coll, ctx->pool, list_v.u.coll_id, &needle, &ix) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_number(ctx->out, (double)ix);
}

int flat_native_list_contains(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *left;
    char *right;
    FlatValue list_v;
    FlatValue needle;
    int ix;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_list_arg(ctx, left, &list_v) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (parse_expr(ctx, right, &needle) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (flat_list_index_of(ctx->coll, ctx->pool, list_v.u.coll_id, &needle, &ix) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_bool(ctx->out, ix >= 0);
}

int flat_native_list_remove_at(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *left;
    char *right;
    FlatValue list_v;
    FlatValue idx_v;
    uint32_t new_id;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_list_arg(ctx, left, &list_v) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (parse_expr(ctx, right, &idx_v) != 0 || idx_v.tag != VAL_NUMBER) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (flat_list_remove_at_copy(ctx->coll, ctx->pool, list_v.u.coll_id, (int)idx_v.u.number, &new_id) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_list(ctx->out, new_id);
}

int flat_native_list_slice(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *list_s;
    char *start_s;
    char *end_s;
    FlatValue list_v;
    FlatValue start_v;
    FlatValue end_v;
    uint32_t new_id;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_three_args(inner_buf, &list_s, &start_s, &end_s) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_list_arg(ctx, list_s, &list_v) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (parse_expr(ctx, start_s, &start_v) != 0 || start_v.tag != VAL_NUMBER) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (parse_expr(ctx, end_s, &end_v) != 0 || end_v.tag != VAL_NUMBER) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (flat_list_slice_copy(ctx->coll, ctx->pool, list_v.u.coll_id, (int)start_v.u.number,
                             (int)end_v.u.number, &new_id) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_list(ctx->out, new_id);
}

int flat_native_list_splice(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *list_s;
    char *start_s;
    char *count_s;
    FlatValue list_v;
    FlatValue start_v;
    FlatValue count_v;
    uint32_t new_id;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_three_args(inner_buf, &list_s, &start_s, &count_s) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_list_arg(ctx, list_s, &list_v) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (parse_expr(ctx, start_s, &start_v) != 0 || start_v.tag != VAL_NUMBER) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (parse_expr(ctx, count_s, &count_v) != 0 || count_v.tag != VAL_NUMBER) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (flat_list_splice_copy(ctx->coll, ctx->pool, list_v.u.coll_id, (int)start_v.u.number,
                              (int)count_v.u.number, &new_id) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_list(ctx->out, new_id);
}

static int parse_whole_number_expr(FlatNativeCtx *ctx, const char *expr, double *out) {
    FlatValue v;
    if (parse_expr(ctx, expr, &v) != 0 || v.tag != VAL_NUMBER) {
        return -1;
    }
    if (v.u.number != floor(v.u.number)) {
        return -1;
    }
    *out = v.u.number;
    return 0;
}

static int parse_nonneg_int_expr(FlatNativeCtx *ctx, const char *expr, int *out) {
    double n;
    if (parse_whole_number_expr(ctx, expr, &n) != 0 || n < 0.0) {
        return -1;
    }
    *out = (int)n;
    return 0;
}

static int build_range_list(FlatNativeCtx *ctx, double start, double stop, double step,
                            uint32_t *out_id) {
    FlatValue items[FLAT_LIST_MAX_ELEMS];
    int n = 0;
    double x;

    if (!ctx->coll || !out_id) {
        return -1;
    }
    if (step == 0.0) {
        return -1;
    }
    if (step > 0.0) {
        for (x = start; x < stop; x += step) {
            if (n >= FLAT_LIST_MAX_ELEMS) {
                return -1;
            }
            flat_value_set_number(&items[n], x);
            n++;
        }
    } else {
        for (x = start; x > stop; x += step) {
            if (n >= FLAT_LIST_MAX_ELEMS) {
                return -1;
            }
            flat_value_set_number(&items[n], x);
            n++;
        }
    }
    if (n == 0) {
        return flat_list_empty(ctx->coll, ctx->pool, out_id);
    }
    return flat_list_build(ctx->coll, ctx->pool, items, n, out_id);
}

int flat_native_list_range(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *parts[3];
    int argc;
    double start = 0.0;
    double stop;
    double step = 1.0;
    uint32_t id;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_variadic_args(inner_buf, parts, 3, &argc) != 0 || argc < 1 || argc > 3) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (argc == 1) {
        if (parse_whole_number_expr(ctx, parts[0], &stop) != 0) {
            *ctx->io_status = FLAT_IO_ERR;
            return -1;
        }
    } else if (argc == 2) {
        if (parse_whole_number_expr(ctx, parts[0], &start) != 0 ||
            parse_whole_number_expr(ctx, parts[1], &stop) != 0) {
            *ctx->io_status = FLAT_IO_ERR;
            return -1;
        }
    } else {
        if (parse_whole_number_expr(ctx, parts[0], &start) != 0 ||
            parse_whole_number_expr(ctx, parts[1], &stop) != 0 ||
            parse_whole_number_expr(ctx, parts[2], &step) != 0) {
            *ctx->io_status = FLAT_IO_ERR;
            return -1;
        }
    }
    if (build_range_list(ctx, start, stop, step, &id) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_list(ctx->out, id);
}

int flat_native_list_repeat(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *n_s;
    char *val_s;
    FlatValue val;
    int n;
    FlatValue items[FLAT_LIST_MAX_ELEMS];
    int i;
    uint32_t id;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &n_s, &val_s) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_nonneg_int_expr(ctx, n_s, &n) != 0 || n > FLAT_LIST_MAX_ELEMS) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (parse_expr(ctx, val_s, &val) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (n == 0) {
        if (flat_list_empty(ctx->coll, ctx->pool, &id) != 0) {
            *ctx->io_status = FLAT_IO_ERR;
            return -1;
        }
        return flat_value_set_list(ctx->out, id);
    }
    for (i = 0; i < n; i++) {
        items[i] = val;
    }
    if (flat_list_build(ctx->coll, ctx->pool, items, n, &id) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_list(ctx->out, id);
}
