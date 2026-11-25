#include "flat_list_shape.h"
#include "flat_number.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_value.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int list_fail(FlatNativeCtx *ctx, const char *why) {
    if (ctx && ctx->io_status) {
        *ctx->io_status = FLAT_IO_ERR;
    }
    if (ctx && why) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", why);
    }
    return -1;
}

static int list_cow_fail(FlatNativeCtx *ctx, uint32_t list_id, const char *fn) {
    int cur_len = 0;
    char why[64];

    if (ctx && ctx->coll && flat_list_len(ctx->coll, list_id, &cur_len) == 0 &&
        cur_len >= FLAT_LIST_MAX_ELEMS) {
        return list_fail(ctx, "list.elems");
    }
    if (ctx && ctx->coll && flat_coll_live_list_count(ctx->coll) >= FLAT_COLL_MAX_LISTS) {
        return list_fail(ctx, "list.full");
    }
    snprintf(why, sizeof why, "list.%s", fn);
    return list_fail(ctx, why);
}

static int parse_list_arg(FlatNativeCtx *ctx, const char *expr, FlatValue *list_v) {
    return flat_native_parse_rhs(ctx, expr, list_v) == 0 && list_v->tag == VAL_LIST ? 0 : -1;
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
        return list_fail(ctx, "list.empty");
    }
    if (ctx->inner && ctx->inner[0] != '\0') {
        return list_fail(ctx, "list.empty.args");
    }
    *ctx->io_status = FLAT_IO_OK;
    if (flat_list_empty(ctx->coll, ctx->pool, &id) != 0) {
        return list_fail(ctx, "list.full");
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
        return list_fail(ctx, "list.pack");
    }
    *ctx->io_status = FLAT_IO_OK;
    if (!ctx->inner) {
        return list_fail(ctx, "list.pack.args");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_variadic_args(inner_buf, parts, FLAT_LIST_MAX_ELEMS, &count) != 0) {
        return list_fail(ctx, "list.pack.args");
    }
    if (count == 0) {
        if (flat_list_empty(ctx->coll, ctx->pool, &id) != 0) {
            return list_fail(ctx, "list.full");
        }
        return flat_value_set_list(ctx->out, id);
    }
    for (i = 0; i < count; i++) {
        if (flat_native_parse_rhs(ctx, parts[i], &items[i]) != 0) {
            int j;
            for (j = 0; j < i; j++) {
                flat_native_parse_rhs_release(ctx, &items[j]);
            }
            return list_fail(ctx, "list.pack.args");
        }
    }
    if (flat_list_build(ctx->coll, ctx->pool, items, count, &id) != 0) {
        int j;
        for (j = 0; j < count; j++) {
            flat_native_parse_rhs_release(ctx, &items[j]);
        }
        if (count > FLAT_LIST_MAX_ELEMS) {
            return list_fail(ctx, "list.elems");
        }
        return list_fail(ctx, "list.full");
    }
    {
        int j;
        for (j = 0; j < count; j++) {
            flat_native_parse_rhs_release(ctx, &items[j]);
        }
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
    int rc = -1;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return list_fail(ctx, "list.push");
    }
    memset(&list_v, 0, sizeof list_v);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        return list_fail(ctx, "list.push.args");
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_list_arg(ctx, left, &list_v) != 0) {
        list_fail(ctx, "list.push.type");
        goto out;
    }
    if (flat_native_parse_expr(ctx, right, &item) != 0) {
        list_fail(ctx, "list.push.args");
        goto out;
    }
    if (flat_list_push_copy(ctx->coll, ctx->pool, list_v.u.coll_id, &item, &new_id) != 0) {
        list_cow_fail(ctx, list_v.u.coll_id, "push");
        goto out;
    }
    if (flat_value_set_list(ctx->out, new_id) != 0) {
        list_fail(ctx, "list.push");
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &list_v);
    return rc;
}

int flat_native_list_get(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *left;
    char *right;
    FlatValue list_v;
    FlatValue idx_v;
    int ix;
    int rc = -1;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return list_fail(ctx, "list.get");
    }
    memset(&list_v, 0, sizeof list_v);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        return list_fail(ctx, "list.get.args");
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_list_arg(ctx, left, &list_v) != 0) {
        list_fail(ctx, "list.get.type");
        goto out;
    }
    if (flat_native_parse_expr(ctx, right, &idx_v) != 0 || idx_v.tag != VAL_NUMBER) {
        list_fail(ctx, "list.get.args");
        goto out;
    }
    ix = (int)flat_number_as_f64(&idx_v);
    if (flat_list_get(ctx->coll, list_v.u.coll_id, ix, ctx->out) != 0) {
        list_fail(ctx, "list.get.range");
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &list_v);
    return rc;
}

int flat_native_list_len(FlatNativeCtx *ctx) {
    FlatValue list_v;
    int len;

    if (!ctx->out || !ctx->io_status || !ctx->coll) {
        return list_fail(ctx, "list.len");
    }
    *ctx->io_status = FLAT_IO_OK;
    if (flat_native_parse_expr(ctx, ctx->inner, &list_v) != 0 || list_v.tag != VAL_LIST) {
        return list_fail(ctx, "list.len.type");
    }
    if (flat_list_len(ctx->coll, list_v.u.coll_id, &len) != 0) {
        return list_fail(ctx, "list.len");
    }
    return flat_value_set_number(ctx->out, (double)len);
}

int flat_native_list_prepend(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *left;
    char *right;
    FlatValue list_v;
    FlatValue item;
    uint32_t new_id;
    int rc = -1;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return list_fail(ctx, "list.prepend");
    }
    memset(&list_v, 0, sizeof list_v);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        return list_fail(ctx, "list.prepend.args");
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_list_arg(ctx, left, &list_v) != 0) {
        list_fail(ctx, "list.prepend.type");
        goto out;
    }
    if (flat_native_parse_expr(ctx, right, &item) != 0) {
        list_fail(ctx, "list.prepend.args");
        goto out;
    }
    if (flat_list_prepend_copy(ctx->coll, ctx->pool, list_v.u.coll_id, &item, &new_id) != 0) {
        list_cow_fail(ctx, list_v.u.coll_id, "prepend");
        goto out;
    }
    if (flat_value_set_list(ctx->out, new_id) != 0) {
        list_fail(ctx, "list.prepend");
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &list_v);
    return rc;
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
    int rc = -1;
    int ix;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return list_fail(ctx, "list.insert");
    }
    memset(&list_v, 0, sizeof list_v);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_three_args(inner_buf, &list_s, &idx_s, &val_s) != 0) {
        return list_fail(ctx, "list.insert.args");
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_list_arg(ctx, list_s, &list_v) != 0) {
        list_fail(ctx, "list.insert.type");
        goto out;
    }
    if (flat_native_parse_expr(ctx, idx_s, &idx_v) != 0 || idx_v.tag != VAL_NUMBER) {
        list_fail(ctx, "list.insert.args");
        goto out;
    }
    if (flat_native_parse_expr(ctx, val_s, &item) != 0) {
        list_fail(ctx, "list.insert.args");
        goto out;
    }
    ix = (int)flat_number_as_f64(&idx_v);
    if (flat_list_insert_copy(ctx->coll, ctx->pool, list_v.u.coll_id, ix, &item, &new_id) != 0) {
        int len = 0;
        if (flat_list_len(ctx->coll, list_v.u.coll_id, &len) == 0 && (ix < 0 || ix > len)) {
            list_fail(ctx, "list.insert.range");
        } else {
            list_cow_fail(ctx, list_v.u.coll_id, "insert");
        }
        goto out;
    }
    if (flat_value_set_list(ctx->out, new_id) != 0) {
        list_fail(ctx, "list.insert");
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &list_v);
    return rc;
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
    int rc = -1;
    int ix;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return list_fail(ctx, "list.set");
    }
    memset(&list_v, 0, sizeof list_v);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_three_args(inner_buf, &list_s, &idx_s, &val_s) != 0) {
        return list_fail(ctx, "list.set.args");
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_list_arg(ctx, list_s, &list_v) != 0) {
        list_fail(ctx, "list.set.type");
        goto out;
    }
    if (flat_native_parse_expr(ctx, idx_s, &idx_v) != 0 || idx_v.tag != VAL_NUMBER) {
        list_fail(ctx, "list.set.args");
        goto out;
    }
    if (flat_native_parse_expr(ctx, val_s, &item) != 0) {
        list_fail(ctx, "list.set.args");
        goto out;
    }
    ix = (int)flat_number_as_f64(&idx_v);
    if (flat_list_set_copy(ctx->coll, ctx->pool, list_v.u.coll_id, ix, &item, &new_id) != 0) {
        int len = 0;
        if (flat_list_len(ctx->coll, list_v.u.coll_id, &len) == 0 && (ix < 0 || ix >= len)) {
            list_fail(ctx, "list.set.range");
        } else {
            list_cow_fail(ctx, list_v.u.coll_id, "set");
        }
        goto out;
    }
    if (flat_value_set_list(ctx->out, new_id) != 0) {
        list_fail(ctx, "list.set");
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &list_v);
    return rc;
}

int flat_native_list_index_of(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *left;
    char *right;
    FlatValue list_v;
    FlatValue needle;
    int ix;
    int rc = -1;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return list_fail(ctx, "list.index_of");
    }
    memset(&list_v, 0, sizeof list_v);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        return list_fail(ctx, "list.index_of.args");
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_list_arg(ctx, left, &list_v) != 0) {
        list_fail(ctx, "list.index_of.type");
        goto out;
    }
    if (flat_native_parse_expr(ctx, right, &needle) != 0) {
        list_fail(ctx, "list.index_of.args");
        goto out;
    }
    if (flat_list_index_of(ctx->coll, ctx->pool, list_v.u.coll_id, &needle, &ix) != 0) {
        list_fail(ctx, "list.index_of");
        goto out;
    }
    if (flat_value_set_number(ctx->out, (double)ix) != 0) {
        list_fail(ctx, "list.index_of");
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &list_v);
    return rc;
}

int flat_native_list_contains(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *left;
    char *right;
    FlatValue list_v;
    FlatValue needle;
    int ix;
    int rc = -1;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return list_fail(ctx, "list.contains");
    }
    memset(&list_v, 0, sizeof list_v);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        return list_fail(ctx, "list.contains.args");
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_list_arg(ctx, left, &list_v) != 0) {
        list_fail(ctx, "list.contains.type");
        goto out;
    }
    if (flat_native_parse_expr(ctx, right, &needle) != 0) {
        list_fail(ctx, "list.contains.args");
        goto out;
    }
    if (flat_list_index_of(ctx->coll, ctx->pool, list_v.u.coll_id, &needle, &ix) != 0) {
        list_fail(ctx, "list.contains");
        goto out;
    }
    if (flat_value_set_bool(ctx->out, ix >= 0) != 0) {
        list_fail(ctx, "list.contains");
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &list_v);
    return rc;
}

int flat_native_list_remove_at(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *left;
    char *right;
    FlatValue list_v;
    FlatValue idx_v;
    uint32_t new_id;
    int rc = -1;
    int ix;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return list_fail(ctx, "list.remove_at");
    }
    memset(&list_v, 0, sizeof list_v);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        return list_fail(ctx, "list.remove_at.args");
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_list_arg(ctx, left, &list_v) != 0) {
        list_fail(ctx, "list.remove_at.type");
        goto out;
    }
    if (flat_native_parse_expr(ctx, right, &idx_v) != 0 || idx_v.tag != VAL_NUMBER) {
        list_fail(ctx, "list.remove_at.args");
        goto out;
    }
    ix = (int)flat_number_as_f64(&idx_v);
    if (flat_list_remove_at_copy(ctx->coll, ctx->pool, list_v.u.coll_id, ix, &new_id) != 0) {
        int len = 0;
        if (flat_list_len(ctx->coll, list_v.u.coll_id, &len) == 0 && (ix < 0 || ix >= len)) {
            list_fail(ctx, "list.remove_at.range");
        } else {
            list_cow_fail(ctx, list_v.u.coll_id, "remove_at");
        }
        goto out;
    }
    if (flat_value_set_list(ctx->out, new_id) != 0) {
        list_fail(ctx, "list.remove_at");
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &list_v);
    return rc;
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
    int rc = -1;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return list_fail(ctx, "list.slice");
    }
    memset(&list_v, 0, sizeof list_v);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_three_args(inner_buf, &list_s, &start_s, &end_s) != 0) {
        return list_fail(ctx, "list.slice.args");
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_list_arg(ctx, list_s, &list_v) != 0) {
        list_fail(ctx, "list.slice.type");
        goto out;
    }
    if (flat_native_parse_expr(ctx, start_s, &start_v) != 0 || start_v.tag != VAL_NUMBER) {
        list_fail(ctx, "list.slice.args");
        goto out;
    }
    if (flat_native_parse_expr(ctx, end_s, &end_v) != 0 || end_v.tag != VAL_NUMBER) {
        list_fail(ctx, "list.slice.args");
        goto out;
    }
    if (flat_list_slice_copy(ctx->coll, ctx->pool, list_v.u.coll_id, (int)flat_number_as_f64(&start_v),
                             (int)flat_number_as_f64(&end_v), &new_id) != 0) {
        list_cow_fail(ctx, list_v.u.coll_id, "slice");
        goto out;
    }
    if (flat_value_set_list(ctx->out, new_id) != 0) {
        list_fail(ctx, "list.slice");
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &list_v);
    return rc;
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
    int rc = -1;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return list_fail(ctx, "list.splice");
    }
    memset(&list_v, 0, sizeof list_v);
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_three_args(inner_buf, &list_s, &start_s, &count_s) != 0) {
        return list_fail(ctx, "list.splice.args");
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_list_arg(ctx, list_s, &list_v) != 0) {
        list_fail(ctx, "list.splice.type");
        goto out;
    }
    if (flat_native_parse_expr(ctx, start_s, &start_v) != 0 || start_v.tag != VAL_NUMBER) {
        list_fail(ctx, "list.splice.args");
        goto out;
    }
    if (flat_native_parse_expr(ctx, count_s, &count_v) != 0 || count_v.tag != VAL_NUMBER) {
        list_fail(ctx, "list.splice.args");
        goto out;
    }
    if (flat_list_splice_copy(ctx->coll, ctx->pool, list_v.u.coll_id, (int)flat_number_as_f64(&start_v),
                              (int)flat_number_as_f64(&count_v), &new_id) != 0) {
        list_cow_fail(ctx, list_v.u.coll_id, "splice");
        goto out;
    }
    if (flat_value_set_list(ctx->out, new_id) != 0) {
        list_fail(ctx, "list.splice");
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &list_v);
    return rc;
}

static int parse_whole_number_expr(FlatNativeCtx *ctx, const char *expr, double *out) {
    FlatValue v;
    if (flat_native_parse_expr(ctx, expr, &v) != 0 || v.tag != VAL_NUMBER) {
        return -1;
    }
    if (flat_number_as_f64(&v) != floor(flat_number_as_f64(&v))) {
        return -1;
    }
    *out = flat_number_as_f64(&v);
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

/* 0 ok; -2 step; -3 elems overflow; -1 other (full/build) */
static int build_range_list(FlatNativeCtx *ctx, double start, double stop, double step,
                            uint32_t *out_id) {
    FlatValue items[FLAT_LIST_MAX_ELEMS];
    int n = 0;
    double x;

    if (!ctx->coll || !out_id) {
        return -1;
    }
    if (step == 0.0) {
        return -2;
    }
    if (step > 0.0) {
        for (x = start; x < stop; x += step) {
            if (n >= FLAT_LIST_MAX_ELEMS) {
                return -3;
            }
            flat_value_set_number(&items[n], x);
            n++;
        }
    } else {
        for (x = start; x > stop; x += step) {
            if (n >= FLAT_LIST_MAX_ELEMS) {
                return -3;
            }
            flat_value_set_number(&items[n], x);
            n++;
        }
    }
    if (n == 0) {
        return flat_list_empty(ctx->coll, ctx->pool, out_id) != 0 ? -1 : 0;
    }
    return flat_list_build(ctx->coll, ctx->pool, items, n, out_id) != 0 ? -1 : 0;
}

int flat_native_list_range(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char *parts[3];
    int argc;
    double start = 0.0;
    double stop;
    double step = 1.0;
    uint32_t id;
    int brc;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return list_fail(ctx, "list.range");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_variadic_args(inner_buf, parts, 3, &argc) != 0 || argc < 1 || argc > 3) {
        return list_fail(ctx, "list.range.args");
    }
    *ctx->io_status = FLAT_IO_OK;
    if (argc == 1) {
        if (parse_whole_number_expr(ctx, parts[0], &stop) != 0) {
            return list_fail(ctx, "list.range.args");
        }
    } else if (argc == 2) {
        if (parse_whole_number_expr(ctx, parts[0], &start) != 0 ||
            parse_whole_number_expr(ctx, parts[1], &stop) != 0) {
            return list_fail(ctx, "list.range.args");
        }
    } else {
        if (parse_whole_number_expr(ctx, parts[0], &start) != 0 ||
            parse_whole_number_expr(ctx, parts[1], &stop) != 0 ||
            parse_whole_number_expr(ctx, parts[2], &step) != 0) {
            return list_fail(ctx, "list.range.args");
        }
    }
    brc = build_range_list(ctx, start, stop, step, &id);
    if (brc == -2) {
        return list_fail(ctx, "list.range.step");
    }
    if (brc == -3) {
        return list_fail(ctx, "list.elems");
    }
    if (brc != 0) {
        return list_fail(ctx, "list.full");
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
        return list_fail(ctx, "list.repeat");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &n_s, &val_s) != 0) {
        return list_fail(ctx, "list.repeat.args");
    }
    *ctx->io_status = FLAT_IO_OK;
    if (parse_nonneg_int_expr(ctx, n_s, &n) != 0) {
        return list_fail(ctx, "list.repeat.args");
    }
    if (n > FLAT_LIST_MAX_ELEMS) {
        return list_fail(ctx, "list.elems");
    }
    if (flat_native_parse_expr(ctx, val_s, &val) != 0) {
        return list_fail(ctx, "list.repeat.args");
    }
    if (n == 0) {
        if (flat_list_empty(ctx->coll, ctx->pool, &id) != 0) {
            return list_fail(ctx, "list.full");
        }
        return flat_value_set_list(ctx->out, id);
    }
    for (i = 0; i < n; i++) {
        items[i] = val;
    }
    if (flat_list_build(ctx->coll, ctx->pool, items, n, &id) != 0) {
        return list_fail(ctx, "list.full");
    }
    return flat_value_set_list(ctx->out, id);
}

static int native_list_one(FlatNativeCtx *ctx, FlatValue *list_v) {
    if (!ctx || !list_v) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    *ctx->io_status = FLAT_IO_OK;
    if (flat_native_parse_expr(ctx, ctx->inner, list_v) != 0 || list_v->tag != VAL_LIST) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "list.shape");
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return 0;
}

int flat_native_list_is_rect(FlatNativeCtx *ctx) {
    FlatValue list_v;
    FlatListShapeInfo info;

    if (!ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    if (native_list_one(ctx, &list_v) != 0) {
        return -1;
    }
    if (flat_list_shape_query(ctx->coll, list_v.u.coll_id, &info) != 0) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "list.shape");
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_bool(ctx->out, info.rect ? 1 : 0);
}

int flat_native_list_dims(FlatNativeCtx *ctx) {
    FlatValue list_v;
    FlatListShapeInfo info;
    FlatValue items[FLAT_LIST_TYPED_MAX_RANK];
    int i;
    uint32_t id;

    if (!ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    if (native_list_one(ctx, &list_v) != 0) {
        return -1;
    }
    if (flat_list_shape_query(ctx->coll, list_v.u.coll_id, &info) != 0) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "list.shape");
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (!info.rect) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "type.list.not_rect");
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    for (i = 0; i < (int)info.dims_n; i++) {
        if (flat_value_set_number(&items[i], (double)info.dims[i]) != 0) {
            *ctx->io_status = FLAT_IO_ERR;
            return -1;
        }
    }
    if (flat_list_build(ctx->coll, ctx->pool, items, (int)info.dims_n, &id) != 0) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "list.dims");
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_list(ctx->out, id);
}

int flat_native_list_rank(FlatNativeCtx *ctx) {
    FlatValue list_v;
    FlatListShapeInfo info;

    if (!ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    if (native_list_one(ctx, &list_v) != 0) {
        return -1;
    }
    if (flat_list_shape_query(ctx->coll, list_v.u.coll_id, &info) != 0) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "list.shape");
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (!info.rect) {
        return flat_value_set_number(ctx->out, 0.0);
    }
    return flat_value_set_number(ctx->out, (double)info.rank);
}

int flat_native_list_is_matrix(FlatNativeCtx *ctx) {
    FlatValue list_v;
    FlatListShapeInfo info;

    if (!ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    if (native_list_one(ctx, &list_v) != 0) {
        return -1;
    }
    if (flat_list_shape_query(ctx->coll, list_v.u.coll_id, &info) != 0) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "list.shape");
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_bool(ctx->out, flat_list_shape_info_is_matrix(&info) ? 1 : 0);
}

int flat_native_list_is_vector(FlatNativeCtx *ctx) {
    FlatValue list_v;
    FlatListShapeInfo info;

    if (!ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    if (native_list_one(ctx, &list_v) != 0) {
        return -1;
    }
    if (flat_list_shape_query(ctx->coll, list_v.u.coll_id, &info) != 0) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "list.shape");
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_bool(ctx->out, flat_list_shape_info_is_vector(&info) ? 1 : 0);
}
