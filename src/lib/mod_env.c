#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_env.h"
#include "flat_value.h"
#include "flat_number.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int env_fail(FlatNativeCtx *ctx, const char *why) {
    if (ctx && ctx->io_status) *ctx->io_status = FLAT_IO_ERR;
    if (ctx && why) snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", why);
    return -1;
}

static int env_ok(FlatNativeCtx *ctx) {
    if (ctx && ctx->io_status) *ctx->io_status = FLAT_IO_OK;
    return 0;
}

static void release_args(FlatNativeCtx *ctx, FlatValue *v, int n) {
    int i;
    for (i = 0; i < n; ++i) flat_native_parse_rhs_release(ctx, &v[i]);
}

static int split_args(char *s, char **a, char **b, char **c) {
    int depth = 0;
    char q = 0;
    char *p;
    int n = 0;
    char **dst[3] = {a, b, c};
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
            if (n < 3) *dst[n] = flat_native_trim(s);
            n++;
            s = p + 1;
        }
    }
    if (n < 3) *dst[n] = flat_native_trim(s);
    return n + 1;
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
    } else return -1;
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
        env_ok(ctx);
        return flat_value_set_str_inline(ctx->out, data ? data : "", len);
    }
    if (flat_pool_alloc_copy(ctx->pool, data ? data : "", len, &ref) != 0)
        return env_fail(ctx, "env.get.memory");
    env_ok(ctx);
    return flat_value_set_str_ref(ctx->out, ref);
}

int flat_native_env_has(FlatNativeCtx *ctx) {
    FlatValue v;
    memset(&v, 0, sizeof v);
    char *name = NULL;
    size_t nl;
    char buf[FLAT_ENV_VAL_MAX + 1];
    int found = 0;
    if (!ctx || !ctx->out || !ctx->inner) return -1;
    if (flat_native_parse_rhs(ctx, ctx->inner, &v) != 0 || value_string(ctx, &v, &name, &nl) != 0 ||
        !name || !nl) {
        free(name);
        flat_native_parse_rhs_release(ctx, &v);
        return env_fail(ctx, "env.has.args");
    }
    flat_native_parse_rhs_release(ctx, &v);
    if (flat_env_context_lookup(ctx->env, name, buf, sizeof buf, &found) != 0) {
        free(name);
        return env_fail(ctx, "env.has.memory");
    }
    free(name);
    env_ok(ctx);
    return flat_value_set_bool(ctx->out, found ? 1 : 0);
}

int flat_native_env_get(FlatNativeCtx *ctx) {
    char raw[4096], *a = NULL, *b = NULL, *c = NULL;
    int argc;
    FlatValue v0, v1;
    char *name = NULL, *def = NULL;
    size_t nl, dl = 0;
    char buf[FLAT_ENV_VAL_MAX + 1];
    int found = 0;
    int rc;
    memset(&v0, 0, sizeof v0);
    memset(&v1, 0, sizeof v1);
    if (!ctx || !ctx->out || !ctx->inner) return -1;
    snprintf(raw, sizeof raw, "%s", ctx->inner);
    argc = split_args(raw, &a, &b, &c);
    if (argc < 1 || argc > 2) return env_fail(ctx, "env.get.args");
    if (flat_native_parse_rhs(ctx, a, &v0) != 0 || value_string(ctx, &v0, &name, &nl) != 0 || !name ||
        !nl) {
        free(name);
        release_args(ctx, &v0, 1);
        return env_fail(ctx, "env.get.args");
    }
    if (argc == 2) {
        if (flat_native_parse_rhs(ctx, b, &v1) != 0 || value_string(ctx, &v1, &def, &dl) != 0) {
            free(name);
            free(def);
            release_args(ctx, &v0, 1);
            release_args(ctx, &v1, 1);
            return env_fail(ctx, "env.get.default");
        }
    }
    if (flat_env_context_lookup(ctx->env, name, buf, sizeof buf, &found) != 0) {
        free(name);
        free(def);
        release_args(ctx, &v0, 1);
        if (argc == 2) release_args(ctx, &v1, 1);
        return env_fail(ctx, "env.get.memory");
    }
    free(name);
    release_args(ctx, &v0, 1);
    if (argc == 2) release_args(ctx, &v1, 1);
    if (found) {
        rc = out_string(ctx, buf, strlen(buf));
        free(def);
        return rc;
    }
    if (argc == 2) {
        rc = out_string(ctx, def ? def : "", dl);
        free(def);
        return rc;
    }
    free(def);
    return env_fail(ctx, "env.get.missing");
}

int flat_native_env_argc(FlatNativeCtx *ctx) {
    if (!ctx || !ctx->out) return -1;
    env_ok(ctx);
    return flat_value_set_number(ctx->out, (double)flat_env_context_argc(ctx->env));
}

int flat_native_env_args(FlatNativeCtx *ctx) {
    size_t n, i;
    FlatValue *items = NULL;
    uint32_t list_id;
    if (!ctx || !ctx->out) return -1;
    if (ctx->inner && flat_native_trim((char *)ctx->inner)[0]) {
        return env_fail(ctx, "env.args.args");
    }
    n = flat_env_context_argc(ctx->env);
    if (n == 0) {
        if (flat_list_empty(ctx->coll, ctx->pool, &list_id) != 0)
            return env_fail(ctx, "env.args.memory");
        env_ok(ctx);
        return flat_value_set_list(ctx->out, list_id);
    }
    items = (FlatValue *)calloc(n, sizeof *items);
    if (!items) return env_fail(ctx, "env.args.memory");
    for (i = 0; i < n; ++i) {
        const char *a = flat_env_context_arg(ctx->env, i);
        size_t al = a ? strlen(a) : 0;
        if (al <= FLAT_INLINE_STR_MAX) {
            if (flat_value_set_str_inline(&items[i], a ? a : "", al) != 0) {
                free(items);
                return env_fail(ctx, "env.args.memory");
            }
        } else {
            uint32_t ref;
            if (flat_pool_alloc_copy(ctx->pool, a, al, &ref) != 0 ||
                flat_value_set_str_ref(&items[i], ref) != 0) {
                free(items);
                return env_fail(ctx, "env.args.memory");
            }
        }
    }
    if (flat_list_build(ctx->coll, ctx->pool, items, (int)n, &list_id) != 0) {
        free(items);
        return env_fail(ctx, "env.args.memory");
    }
    free(items);
    env_ok(ctx);
    return flat_value_set_list(ctx->out, list_id);
}
