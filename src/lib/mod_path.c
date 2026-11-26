#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_path.h"
#include "flat_value.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define PATH_JOIN_MAX 64

static int path_fail(FlatNativeCtx *ctx, const char *why) {
    if (ctx && ctx->io_status) *ctx->io_status = FLAT_IO_ERR;
    if (ctx && why) snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", why);
    return -1;
}

static int path_ok(FlatNativeCtx *ctx) {
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

static int out_string(FlatNativeCtx *ctx, const char *data, size_t len) {
    uint32_t ref;
    if (!ctx || !ctx->out) return -1;
    if (len <= FLAT_INLINE_STR_MAX) {
        path_ok(ctx);
        return flat_value_set_str_inline(ctx->out, data ? data : "", len);
    }
    if (flat_pool_alloc_copy(ctx->pool, data ? data : "", len, &ref) != 0)
        return path_fail(ctx, "path.memory");
    path_ok(ctx);
    return flat_value_set_str_ref(ctx->out, ref);
}

static int split_variadic(char *inner, char *parts[], int max_parts, int *out_count) {
    int depth = 0;
    char q = 0;
    char *p;
    char *s;
    int n = 0;
    if (!inner || !parts || !out_count) return -1;
    s = flat_native_trim(inner);
    if (!*s) {
        *out_count = 0;
        return 0;
    }
    parts[n++] = s;
    for (p = s; *p; p++) {
        if (q) {
            if (*p == q && (p == s || p[-1] != '\\')) q = 0;
            continue;
        }
        if (*p == '\'' || *p == '"') {
            q = *p;
            continue;
        }
        if (*p == '[' || *p == '{' || *p == '(') depth++;
        else if ((*p == ']' || *p == '}' || *p == ')') && depth) depth--;
        else if (*p == ',' && depth == 0) {
            *p = '\0';
            if (n >= max_parts) return -1;
            parts[n++] = flat_native_trim(p + 1);
        }
    }
    *out_count = n;
    return 0;
}

static int unary_path_string(FlatNativeCtx *ctx, char **out, size_t *len, const char *arg_why,
                             const char *empty_why) {
    FlatValue v;
    memset(&v, 0, sizeof v);
    if (!ctx || !ctx->inner) return path_fail(ctx, arg_why);
    if (flat_native_parse_rhs(ctx, ctx->inner, &v) != 0 || value_string(ctx, &v, out, len) != 0) {
        free(*out);
        *out = NULL;
        flat_native_parse_rhs_release(ctx, &v);
        return path_fail(ctx, arg_why);
    }
    flat_native_parse_rhs_release(ctx, &v);
    if (!*out || !*len) {
        free(*out);
        *out = NULL;
        return path_fail(ctx, empty_why);
    }
    return 0;
}

int flat_native_path_join(FlatNativeCtx *ctx) {
    char raw[8192];
    char *parts_tok[PATH_JOIN_MAX];
    char *strs[PATH_JOIN_MAX];
    const char *cparts[PATH_JOIN_MAX];
    FlatValue vals[PATH_JOIN_MAX];
    int n = 0, i, argc = 0;
    char out[FLAT_PATH_MAX];
    const char *why = NULL;
    memset(strs, 0, sizeof strs);
    memset(vals, 0, sizeof vals);
    if (!ctx || !ctx->out || !ctx->inner) return -1;
    snprintf(raw, sizeof raw, "%s", ctx->inner);
    if (split_variadic(raw, parts_tok, PATH_JOIN_MAX, &argc) != 0 || argc < 2) {
        return path_fail(ctx, "path.join.args");
    }
    for (i = 0; i < argc; i++) {
        size_t len = 0;
        if (flat_native_parse_rhs(ctx, parts_tok[i], &vals[i]) != 0 ||
            value_string(ctx, &vals[i], &strs[i], &len) != 0) {
            for (n = 0; n <= i; n++) {
                free(strs[n]);
                flat_native_parse_rhs_release(ctx, &vals[n]);
            }
            return path_fail(ctx, "path.join.args");
        }
        cparts[i] = strs[i];
    }
    n = argc;
    if (flat_path_join_ctx(ctx->path, cparts, n, out, sizeof out, &why) != 0) {
        for (i = 0; i < n; i++) {
            free(strs[i]);
            flat_native_parse_rhs_release(ctx, &vals[i]);
        }
        return path_fail(ctx, why ? why : "path.join");
    }
    for (i = 0; i < n; i++) {
        free(strs[i]);
        flat_native_parse_rhs_release(ctx, &vals[i]);
    }
    return out_string(ctx, out, strlen(out));
}

int flat_native_path_base(FlatNativeCtx *ctx) {
    char *p = NULL;
    size_t len = 0;
    char out[FLAT_PATH_MAX];
    const char *why = NULL;
    if (unary_path_string(ctx, &p, &len, "path.base.args", "path.base.empty") != 0) return -1;
    if (flat_path_base_ctx(ctx->path, p, out, sizeof out, &why) != 0) {
        free(p);
        return path_fail(ctx, why ? why : "path.base");
    }
    free(p);
    return out_string(ctx, out, strlen(out));
}

int flat_native_path_dir(FlatNativeCtx *ctx) {
    char *p = NULL;
    size_t len = 0;
    char out[FLAT_PATH_MAX];
    const char *why = NULL;
    if (unary_path_string(ctx, &p, &len, "path.dir.args", "path.dir.empty") != 0) return -1;
    if (flat_path_dir_ctx(ctx->path, p, out, sizeof out, &why) != 0) {
        free(p);
        return path_fail(ctx, why ? why : "path.dir");
    }
    free(p);
    return out_string(ctx, out, strlen(out));
}

int flat_native_path_ext(FlatNativeCtx *ctx) {
    char *p = NULL;
    size_t len = 0;
    char out[FLAT_PATH_MAX];
    const char *why = NULL;
    if (unary_path_string(ctx, &p, &len, "path.ext.args", "path.ext.empty") != 0) return -1;
    if (flat_path_ext_ctx(ctx->path, p, out, sizeof out, &why) != 0) {
        free(p);
        return path_fail(ctx, why ? why : "path.ext");
    }
    free(p);
    return out_string(ctx, out, strlen(out));
}

int flat_native_path_normalize(FlatNativeCtx *ctx) {
    char *p = NULL;
    size_t len = 0;
    char out[FLAT_PATH_MAX];
    const char *why = NULL;
    if (unary_path_string(ctx, &p, &len, "path.normalize.args", "path.normalize.empty") != 0)
        return -1;
    if (flat_path_normalize_ctx(ctx->path, p, out, sizeof out, &why) != 0) {
        free(p);
        return path_fail(ctx, why ? why : "path.normalize");
    }
    free(p);
    return out_string(ctx, out, strlen(out));
}

int flat_native_path_is_abs(FlatNativeCtx *ctx) {
    char *p = NULL;
    size_t len = 0;
    int abs = 0;
    const char *why = NULL;
    if (unary_path_string(ctx, &p, &len, "path.isAbs.args", "path.isAbs.empty") != 0) return -1;
    if (flat_path_is_abs_ctx(ctx->path, p, &abs, &why) != 0) {
        free(p);
        return path_fail(ctx, why ? why : "path.isAbs");
    }
    free(p);
    path_ok(ctx);
    return flat_value_set_bool(ctx->out, abs ? 1 : 0);
}
