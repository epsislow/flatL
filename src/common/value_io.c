#include "flat_native.h"
#include "flat_native_util.h"
#include "flat_value.h"

#include <stdio.h>
#include <string.h>

int flat_native_eval_call(FlatNativeCtx *ctx, const char *line, FlatValue *out) {
    char trimmed[4096];
    uint16_t id;
    char inner[4096];
    FlatNativeCtx sub;
    FlatIoStatus st;

    if (!ctx || !line || !out || !ctx->io_status) {
        return -1;
    }
    snprintf(trimmed, sizeof trimmed, "%s", line);
    flat_native_trim(trimmed);
    if (!flat_native_is_call_line(trimmed)) {
        return -1;
    }
    if (flat_native_split_call(trimmed, &id, inner, sizeof inner) != 0) {
        return -1;
    }
    sub = *ctx;
    sub.inner = inner;
    sub.out = out;
    sub.io_status = &st;
    st = FLAT_IO_ERR;
    if (flat_native_invoke(id, &sub) != 0) {
        return -1;
    }
    if (st != FLAT_IO_OK) {
        return -1;
    }
    return 0;
}

int flat_io_exec_line(const char *line, FlatStringPool *pool, FlatCollectionPool *coll,
                      FlatValue *vars, int var_count, const char (*var_names)[64],
                      FlatValue *out_result, FlatIoStatus *out_status) {
    return flat_io_exec_line_manifest(line, pool, coll, vars, var_count, var_names, out_result,
                                      out_status, NULL, 0, NULL, 0);
}

int flat_io_exec_line_manifest(const char *line, FlatStringPool *pool, FlatCollectionPool *coll,
                               FlatValue *vars, int var_count, const char (*var_names)[64],
                               FlatValue *out_result, FlatIoStatus *out_status,
                               const FlatOptionalNativeDecl *decls, int decl_count,
                               FlatMemLiveStats *mem_live, uint32_t vars_max) {
    uint16_t id;
    char inner[4096];
    FlatNativeCtx ctx;

    if (out_status) {
        *out_status = FLAT_IO_ERR;
    }
    if (!line || !pool) {
        return -1;
    }
    if (decls && decl_count > 0) {
        if (flat_native_split_call_manifest(line, decls, decl_count, &id, inner, sizeof inner) !=
            0) {
            return -1;
        }
    } else if (flat_native_split_call(line, &id, inner, sizeof inner) != 0) {
        return -1;
    }
    ctx.pool = pool;
    ctx.coll = coll;
    ctx.vars = vars;
    ctx.var_count = var_count;
    ctx.var_names = var_names;
    ctx.inner = inner;
    ctx.out = out_result;
    ctx.io_status = out_status;
    ctx.mem_live = mem_live;
    ctx.vars_max = vars_max;
    return flat_native_invoke(id, &ctx);
}
