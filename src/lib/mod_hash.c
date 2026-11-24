#include "flat_hash.h"
#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_number.h"
#include "flat_rng.h"
#include "flat_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLAT_HASH_RANDOM_BYTES_MAX 8192u

static int hash_fail(FlatNativeCtx *ctx, const char *why) {
    if (ctx && ctx->io_status) {
        *ctx->io_status = FLAT_IO_ERR;
    }
    if (ctx && why) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", why);
    }
    return -1;
}

static int hash_ok(FlatNativeCtx *ctx) {
    if (ctx && ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return 0;
}

static void release_args(FlatNativeCtx *ctx, FlatValue *v, int n) {
    int i;
    for (i = 0; i < n; i++) {
        flat_native_parse_rhs_release(ctx, &v[i]);
    }
}

static int split_two(char *s, char **a, char **b) {
    int depth = 0;
    char q = 0;
    char *p;
    for (p = s; *p; p++) {
        if (q) {
            if (*p == q) {
                q = 0;
            }
            continue;
        }
        if (*p == '\'' || *p == '"') {
            q = *p;
            continue;
        }
        if (*p == '[' || *p == '{' || *p == '(') {
            depth++;
        } else if ((*p == ']' || *p == '}' || *p == ')') && depth) {
            depth--;
        } else if (*p == ',' && depth == 0) {
            *p = '\0';
            *a = flat_native_trim(s);
            *b = flat_native_trim(p + 1);
            return 0;
        }
    }
    return -1;
}

static int value_bytes(FlatNativeCtx *ctx, const FlatValue *v, unsigned char **out, size_t *len) {
    const uint8_t *p;
    size_t n;
    if (!ctx || !v || !out || !len) {
        return -1;
    }
    if (v->tag == VAL_STR_INLINE) {
        n = v->u.ins.len;
        p = (const uint8_t *)v->u.ins.bytes;
    } else if (v->tag == VAL_STR_REF) {
        p = flat_pool_read(ctx->pool, v->u.ref_id, &n);
        if (!p) {
            return -1;
        }
    } else {
        return -1;
    }
    *out = (unsigned char *)malloc(n ? n : 1);
    if (!*out) {
        return -1;
    }
    if (n) {
        memcpy(*out, p, n);
    }
    *len = n;
    return 0;
}

static int value_cstr(FlatNativeCtx *ctx, const FlatValue *v, char **out, size_t *len) {
    unsigned char *buf = NULL;
    size_t n = 0;
    size_t i;
    unsigned char *nbuf;
    if (value_bytes(ctx, v, &buf, &n) != 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (buf[i] == 0) {
            free(buf);
            return -1;
        }
    }
    nbuf = (unsigned char *)realloc(buf, n + 1);
    if (!nbuf) {
        free(buf);
        return -1;
    }
    nbuf[n] = '\0';
    *out = (char *)nbuf;
    *len = n;
    return 0;
}

static int out_bytes(FlatNativeCtx *ctx, const void *data, size_t len) {
    uint32_t ref;
    if (!ctx || !ctx->out) {
        return -1;
    }
    if (len <= FLAT_INLINE_STR_MAX) {
        hash_ok(ctx);
        return flat_value_set_str_inline(ctx->out, data ? (const char *)data : "", len);
    }
    if (flat_pool_alloc_copy(ctx->pool, data ? data : "", len, &ref) != 0) {
        return hash_fail(ctx, "hash.memory");
    }
    hash_ok(ctx);
    return flat_value_set_str_ref(ctx->out, ref);
}

int flat_native_hash_digest(FlatNativeCtx *ctx) {
    char buf[4096];
    char *a;
    char *b;
    FlatValue vs[2];
    char *algo = NULL;
    size_t algo_len = 0;
    unsigned char *data = NULL;
    size_t data_len = 0;
    unsigned char dig[FLAT_HASH_DIGEST_MAX];
    size_t dig_len = 0;
    char hex[FLAT_HASH_HEX_MAX + 1];
    char why[64];
    int rc = -1;

    memset(vs, 0, sizeof vs);
    why[0] = '\0';
    if (!ctx || !ctx->out || !ctx->inner) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_two(buf, &a, &b) != 0) {
        return hash_fail(ctx, "hash.digest.args");
    }
    if (flat_native_parse_rhs(ctx, a, &vs[0]) != 0 || flat_native_parse_rhs(ctx, b, &vs[1]) != 0) {
        release_args(ctx, vs, 2);
        return hash_fail(ctx, "hash.digest.args");
    }
    if (value_cstr(ctx, &vs[0], &algo, &algo_len) != 0 || !algo || algo_len == 0) {
        free(algo);
        release_args(ctx, vs, 2);
        return hash_fail(ctx, "hash.digest.algo");
    }
    if (value_bytes(ctx, &vs[1], &data, &data_len) != 0) {
        free(algo);
        release_args(ctx, vs, 2);
        return hash_fail(ctx, "hash.digest.args");
    }
    if (flat_hash_digest(algo, data, data_len, dig, &dig_len, why, sizeof why) != 0) {
        free(algo);
        free(data);
        release_args(ctx, vs, 2);
        return hash_fail(ctx, why[0] ? why : "hash.digest");
    }
    flat_hash_hex(dig, dig_len, hex);
    rc = out_bytes(ctx, hex, dig_len * 2);
    free(algo);
    free(data);
    release_args(ctx, vs, 2);
    return rc;
}

int flat_native_hash_random_bytes(FlatNativeCtx *ctx) {
    FlatValue nval;
    double nd;
    uint64_t nu;
    unsigned char *buf = NULL;
    int rc = -1;

    memset(&nval, 0, sizeof nval);
    if (!ctx || !ctx->out || !ctx->inner) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    if (flat_native_parse_expr(ctx, ctx->inner, &nval) != 0 || nval.tag != VAL_NUMBER) {
        return hash_fail(ctx, "hash.randomBytes.args");
    }
    nd = flat_number_as_f64(&nval);
    if (!(nd >= 1.0) || !(nd <= (double)FLAT_HASH_RANDOM_BYTES_MAX) || nd != (double)(uint64_t)nd) {
        return hash_fail(ctx, "hash.randomBytes.args");
    }
    nu = (uint64_t)nd;
    buf = (unsigned char *)malloc((size_t)nu);
    if (!buf) {
        return hash_fail(ctx, "hash.memory");
    }
    flat_rng_fill(buf, (size_t)nu);
    rc = out_bytes(ctx, buf, (size_t)nu);
    free(buf);
    return rc;
}
