#include "flat_native_mod.h"
#include "flat_native_util.h"

#include <string.h>

static int string_from_inner(FlatNativeCtx *ctx, char *token, size_t token_size) {
    FlatValue src;
    char inner_buf[4096];

    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (flat_value_parse_assign_rhs(flat_native_trim(inner_buf), &src, ctx->pool, ctx->coll,
                                    ctx->vars, ctx->var_count, ctx->var_names) != 0) {
        return -1;
    }
    if (src.tag != VAL_STR_INLINE && src.tag != VAL_STR_REF) {
        return -1;
    }
    if (src.tag == VAL_STR_INLINE) {
        snprintf(token, token_size, "%s", src.u.ins.bytes);
    } else {
        size_t len = 0;
        const uint8_t *bp = flat_pool_read(ctx->pool, src.u.ref_id, &len);
        if (!bp || len >= token_size) {
            return -1;
        }
        memcpy(token, bp, len);
        token[len] = '\0';
    }
    return 0;
}

int flat_native_str_to_num(FlatNativeCtx *ctx) {
    char token[512];
    double n;

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (string_from_inner(ctx, token, sizeof token) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (flat_parse_number_token(flat_native_trim(token), &n) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_number(ctx->out, n);
}

int flat_native_str_to_bool(FlatNativeCtx *ctx) {
    char token[512];
    int b;

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    if (string_from_inner(ctx, token, sizeof token) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (flat_parse_bool_token(flat_native_trim(token), &b) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return flat_value_set_bool(ctx->out, b);
}
