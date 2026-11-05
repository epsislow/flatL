#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_obj_path.h"
#include "flat_value.h"

#include <ctype.h>
#include <stdio.h>
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

static int split_three_args(char *inner, char **first, char **second, char **third) {
    int depth = 0;
    char quote = 0;
    char *p;
    char *c1 = NULL;
    char *c2 = NULL;
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

static int parse_obj_arg(FlatNativeCtx *ctx, const char *expr, FlatValue *out) {
    if (parse_expr(ctx, expr, out) != 0 || out->tag != VAL_OBJECT) {
        return -1;
    }
    return 0;
}

static int parse_path_arg(FlatNativeCtx *ctx, const char *expr, FlatObjPathSegs *path,
                          const char *fn) {
    FlatValue pv;
    FlatPathErr err = FLAT_PATH_OK;

    if (!ctx || !expr || !path) {
        return -1;
    }
    if (parse_expr(ctx, expr, &pv) != 0) {
        flat_path_reason(FLAT_PATH_SYNTAX, fn, ctx->panic_detail, sizeof ctx->panic_detail);
        return -1;
    }
    if (pv.tag != VAL_STR_INLINE && pv.tag != VAL_STR_REF) {
        flat_path_reason(FLAT_PATH_ARG_TYPE, fn, ctx->panic_detail, sizeof ctx->panic_detail);
        return -1;
    }
    if (flat_obj_path_from_value(&pv, ctx->pool, path) != 0) {
        flat_path_reason(FLAT_PATH_SYNTAX, fn, ctx->panic_detail, sizeof ctx->panic_detail);
        return -1;
    }
    (void)err;
    return 0;
}

static int io_err(FlatNativeCtx *ctx) {
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_ERR;
    }
    return -1;
}

static int io_ok(FlatNativeCtx *ctx) {
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return 0;
}

int flat_native_obj_empty(FlatNativeCtx *ctx) {
    uint32_t id;
    if (!ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    if (ctx->inner && ctx->inner[0] != '\0') {
        return io_err(ctx);
    }
    io_ok(ctx);
    if (flat_obj_empty(ctx->coll, ctx->pool, &id) != 0) {
        return io_err(ctx);
    }
    return flat_value_set_object(ctx->out, id);
}

int flat_native_obj_get(FlatNativeCtx *ctx) {
    char inner_buf[2048];
    char *obj_e;
    char *key_e;
    FlatValue obj_v;
    FlatObjPathSegs path;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &obj_e, &key_e) != 0) {
        return io_err(ctx);
    }
    io_ok(ctx);
    if (parse_obj_arg(ctx, obj_e, &obj_v) != 0 || parse_path_arg(ctx, key_e, &path, "get") != 0) {
        return io_err(ctx);
    }
    if (flat_obj_get_path(ctx->coll, obj_v.u.coll_id, &path, ctx->out) != 0) {
        flat_path_reason(FLAT_PATH_MISSING, "get", ctx->panic_detail, sizeof ctx->panic_detail);
        return io_err(ctx);
    }
    return 0;
}

int flat_native_obj_set(FlatNativeCtx *ctx) {
    char inner_buf[2048];
    char *obj_e;
    char *key_e;
    char *val_e;
    FlatValue obj_v;
    FlatValue val;
    FlatObjPathSegs path;
    uint32_t new_id;
    int rc;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_three_args(inner_buf, &obj_e, &key_e, &val_e) != 0) {
        return io_err(ctx);
    }
    io_ok(ctx);
    if (parse_obj_arg(ctx, obj_e, &obj_v) != 0 || parse_path_arg(ctx, key_e, &path, "set") != 0 ||
        parse_expr(ctx, val_e, &val) != 0) {
        return io_err(ctx);
    }
    rc = flat_obj_set_path_copy(ctx->coll, ctx->pool, obj_v.u.coll_id, &path, &val, &new_id);
    if (rc == -2) {
        flat_path_reason(FLAT_PATH_SET, "set", ctx->panic_detail, sizeof ctx->panic_detail);
        return io_err(ctx);
    }
    if (rc != 0) {
        return io_err(ctx);
    }
    return flat_value_set_object(ctx->out, new_id);
}

int flat_native_obj_has(FlatNativeCtx *ctx) {
    char inner_buf[2048];
    char *obj_e;
    char *key_e;
    FlatValue obj_v;
    FlatObjPathSegs path;
    int has = 0;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &obj_e, &key_e) != 0) {
        return io_err(ctx);
    }
    io_ok(ctx);
    if (parse_obj_arg(ctx, obj_e, &obj_v) != 0 || parse_path_arg(ctx, key_e, &path, "has") != 0) {
        return io_err(ctx);
    }
    if (flat_obj_has_path(ctx->coll, obj_v.u.coll_id, &path, &has) != 0) {
        return io_err(ctx);
    }
    return flat_value_set_bool(ctx->out, has ? 1 : 0);
}

int flat_native_obj_len(FlatNativeCtx *ctx) {
    FlatValue obj_v;
    int len;

    if (!ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    io_ok(ctx);
    if (parse_obj_arg(ctx, ctx->inner, &obj_v) != 0) {
        return io_err(ctx);
    }
    if (flat_obj_len(ctx->coll, obj_v.u.coll_id, &len) != 0) {
        return io_err(ctx);
    }
    return flat_value_set_number(ctx->out, (double)len);
}

int flat_native_obj_remove(FlatNativeCtx *ctx) {
    char inner_buf[2048];
    char *obj_e;
    char *key_e;
    FlatValue obj_v;
    FlatObjPathSegs path;
    uint32_t new_id;
    int rc;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &obj_e, &key_e) != 0) {
        return io_err(ctx);
    }
    io_ok(ctx);
    if (parse_obj_arg(ctx, obj_e, &obj_v) != 0 ||
        parse_path_arg(ctx, key_e, &path, "remove") != 0) {
        return io_err(ctx);
    }
    rc = flat_obj_remove_path_copy(ctx->coll, ctx->pool, obj_v.u.coll_id, &path, &new_id);
    if (rc == -2) {
        flat_path_reason(FLAT_PATH_TYPE, "remove", ctx->panic_detail, sizeof ctx->panic_detail);
        return io_err(ctx);
    }
    if (rc != 0) {
        return io_err(ctx);
    }
    return flat_value_set_object(ctx->out, new_id);
}

int flat_native_obj_set2(FlatNativeCtx *ctx) {
    char inner_buf[2048];
    char *path_e;
    char *val_e;
    FlatValue val;
    uint32_t root_id;
    FlatPathErr err = FLAT_PATH_OK;

    if (!ctx->inner || !ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &path_e, &val_e) != 0) {
        return io_err(ctx);
    }
    io_ok(ctx);
    if (parse_expr(ctx, val_e, &val) != 0) {
        return io_err(ctx);
    }
    if (flat_path_set2(ctx->coll, ctx->pool, ctx->vars, ctx->var_count, ctx->var_names, path_e,
                       &val, &root_id, &err) != 0) {
        flat_path_reason(err, "set2", ctx->panic_detail, sizeof ctx->panic_detail);
        return io_err(ctx);
    }
    return flat_value_set_object(ctx->out, root_id);
}

static int build_key_value_lists(FlatNativeCtx *ctx, uint32_t obj_id, int keys_not_values,
                                 uint32_t *out_list_id) {
    FlatValue items[FLAT_OBJ_MAX_KEYS];
    int len;
    int i;
    const char *k;
    FlatValue v;

    if (flat_obj_len(ctx->coll, obj_id, &len) != 0) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (flat_obj_entry_at(ctx->coll, obj_id, i, &k, &v) != 0 || !k) {
            return -1;
        }
        if (keys_not_values) {
            size_t klen = strlen(k);
            if (flat_value_set_str_inline(&items[i], k, klen) != 0) {
                return -1;
            }
        } else {
            items[i] = v;
        }
    }
    if (len == 0) {
        return flat_list_empty(ctx->coll, ctx->pool, out_list_id);
    }
    return flat_list_build(ctx->coll, ctx->pool, items, len, out_list_id);
}

int flat_native_obj_keys(FlatNativeCtx *ctx) {
    FlatValue obj_v;
    uint32_t list_id;

    if (!ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    io_ok(ctx);
    if (parse_obj_arg(ctx, ctx->inner, &obj_v) != 0) {
        return io_err(ctx);
    }
    if (build_key_value_lists(ctx, obj_v.u.coll_id, 1, &list_id) != 0) {
        return io_err(ctx);
    }
    return flat_value_set_list(ctx->out, list_id);
}

int flat_native_obj_values(FlatNativeCtx *ctx) {
    FlatValue obj_v;
    uint32_t list_id;

    if (!ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    io_ok(ctx);
    if (parse_obj_arg(ctx, ctx->inner, &obj_v) != 0) {
        return io_err(ctx);
    }
    if (build_key_value_lists(ctx, obj_v.u.coll_id, 0, &list_id) != 0) {
        return io_err(ctx);
    }
    return flat_value_set_list(ctx->out, list_id);
}
