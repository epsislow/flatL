#include "flat_native.h"
#include "flat_value.h"

int flat_io_exec_line(const char *line, FlatStringPool *pool, FlatCollectionPool *coll,
                      FlatValue *vars, int var_count, const char (*var_names)[64],
                      FlatValue *out_result, FlatIoStatus *out_status) {
    return flat_io_exec_line_manifest(line, pool, coll, vars, var_count, var_names, out_result,
                                      out_status, NULL, 0);
}

int flat_io_exec_line_manifest(const char *line, FlatStringPool *pool, FlatCollectionPool *coll,
                               FlatValue *vars, int var_count, const char (*var_names)[64],
                               FlatValue *out_result, FlatIoStatus *out_status,
                               const FlatOptionalNativeDecl *decls, int decl_count) {
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
    return flat_native_invoke(id, &ctx);
}
