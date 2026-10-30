#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_value.h"

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
    if (flat_list_empty(ctx->coll, &id) != 0) {
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
        if (flat_list_empty(ctx->coll, &id) != 0) {
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
    if (flat_list_build(ctx->coll, items, count, &id) != 0) {
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
    if (flat_list_push_copy(ctx->coll, list_v.u.coll_id, &item, &new_id) != 0) {
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
