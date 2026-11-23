#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_fs.h"
#include "flat_value.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int fs_fail(FlatNativeCtx *ctx, const char *why) {
    if (ctx && ctx->io_status) *ctx->io_status = FLAT_IO_ERR;
    if (ctx && why) snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", why);
    return -1;
}

static int fs_ok(FlatNativeCtx *ctx) {
    if (ctx && ctx->io_status) *ctx->io_status = FLAT_IO_OK;
    return 0;
}

static int value_string(FlatNativeCtx *ctx, const FlatValue *v, char **out, size_t *len) {
    const uint8_t *p;
    size_t n;
    if (!ctx || !v || !out || !len) return -1;
    if (v->tag == VAL_STR_INLINE) {
        n = v->u.ins.len;
        p = (const uint8_t *)v->u.ins.bytes;
    } else if (v->tag == VAL_STR_REF) {
        p = flat_pool_read(ctx->pool, v->u.ref_id, &n);
        if (!p) return -1;
    } else {
        return -1;
    }
    *out = (char *)malloc(n + 1);
    if (!*out) return -1;
    memcpy(*out, p, n);
    (*out)[n] = '\0';
    *len = n;
    return 0;
}

static int unary_path(FlatNativeCtx *ctx, char **out, size_t *len, const char *arg_why,
                      const char *empty_why) {
    FlatValue v;
    memset(&v, 0, sizeof v);
    *out = NULL;
    *len = 0;
    if (!ctx || !ctx->inner) return fs_fail(ctx, arg_why);
    if (flat_native_parse_rhs(ctx, ctx->inner, &v) != 0 || value_string(ctx, &v, out, len) != 0) {
        free(*out);
        *out = NULL;
        flat_native_parse_rhs_release(ctx, &v);
        return fs_fail(ctx, arg_why);
    }
    flat_native_parse_rhs_release(ctx, &v);
    if (!*out || !*len) {
        free(*out);
        *out = NULL;
        return fs_fail(ctx, empty_why);
    }
    return 0;
}

int flat_native_fs_exists(FlatNativeCtx *ctx) {
    char *p = NULL;
    size_t len = 0;
    int yes;
    if (unary_path(ctx, &p, &len, "fs.exists.args", "fs.exists.empty") != 0) return -1;
    yes = flat_fs_exists(p);
    free(p);
    fs_ok(ctx);
    return flat_value_set_bool(ctx->out, yes ? 1 : 0);
}

int flat_native_fs_is_file(FlatNativeCtx *ctx) {
    char *p = NULL;
    size_t len = 0;
    int yes;
    if (unary_path(ctx, &p, &len, "fs.isFile.args", "fs.isFile.empty") != 0) return -1;
    yes = flat_fs_is_file(p);
    free(p);
    fs_ok(ctx);
    return flat_value_set_bool(ctx->out, yes ? 1 : 0);
}

int flat_native_fs_is_dir(FlatNativeCtx *ctx) {
    char *p = NULL;
    size_t len = 0;
    int yes;
    if (unary_path(ctx, &p, &len, "fs.isDir.args", "fs.isDir.empty") != 0) return -1;
    yes = flat_fs_is_dir(p);
    free(p);
    fs_ok(ctx);
    return flat_value_set_bool(ctx->out, yes ? 1 : 0);
}

int flat_native_fs_mkdir(FlatNativeCtx *ctx) {
    char *p = NULL;
    size_t len = 0;
    const char *why = NULL;
    if (unary_path(ctx, &p, &len, "fs.mkdir.args", "fs.mkdir.empty") != 0) return -1;
    if (flat_fs_mkdir(p, &why) != 0) {
        free(p);
        return fs_fail(ctx, why ? why : "fs.mkdir");
    }
    free(p);
    fs_ok(ctx);
    if (ctx->out) flat_value_set_null(ctx->out);
    return 0;
}

int flat_native_fs_remove(FlatNativeCtx *ctx) {
    char *p = NULL;
    size_t len = 0;
    const char *why = NULL;
    if (unary_path(ctx, &p, &len, "fs.remove.args", "fs.remove.empty") != 0) return -1;
    if (flat_fs_remove(p, &why) != 0) {
        free(p);
        return fs_fail(ctx, why ? why : "fs.remove");
    }
    free(p);
    fs_ok(ctx);
    if (ctx->out) flat_value_set_null(ctx->out);
    return 0;
}

int flat_native_fs_list_dir(FlatNativeCtx *ctx) {
    char *p = NULL;
    size_t len = 0;
    char names[FLAT_FS_LIST_MAX][FLAT_FS_NAME_MAX];
    int n = 0, i;
    const char *why = NULL;
    FlatValue *items = NULL;
    uint32_t list_id;
    if (unary_path(ctx, &p, &len, "fs.listDir.args", "fs.listDir.empty") != 0) return -1;
    if (flat_fs_list_dir(p, names, FLAT_FS_LIST_MAX, &n, &why) != 0) {
        free(p);
        return fs_fail(ctx, why ? why : "fs.listDir");
    }
    free(p);
    if (n == 0) {
        if (flat_list_empty(ctx->coll, ctx->pool, &list_id) != 0)
            return fs_fail(ctx, "fs.listDir.memory");
        fs_ok(ctx);
        return flat_value_set_list(ctx->out, list_id);
    }
    items = (FlatValue *)calloc((size_t)n, sizeof *items);
    if (!items) return fs_fail(ctx, "fs.listDir.memory");
    for (i = 0; i < n; i++) {
        size_t sl = strlen(names[i]);
        if (sl <= FLAT_INLINE_STR_MAX) {
            if (flat_value_set_str_inline(&items[i], names[i], sl) != 0) {
                free(items);
                return fs_fail(ctx, "fs.listDir.memory");
            }
        } else {
            uint32_t ref;
            if (flat_pool_alloc_copy(ctx->pool, names[i], sl, &ref) != 0 ||
                flat_value_set_str_ref(&items[i], ref) != 0) {
                free(items);
                return fs_fail(ctx, "fs.listDir.memory");
            }
        }
    }
    if (flat_list_build(ctx->coll, ctx->pool, items, n, &list_id) != 0) {
        free(items);
        return fs_fail(ctx, "fs.listDir.memory");
    }
    free(items);
    fs_ok(ctx);
    return flat_value_set_list(ctx->out, list_id);
}
