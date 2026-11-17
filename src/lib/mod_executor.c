#include "flat_executor.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_value.h"
#include "flat_vcx.h"

#include <stdio.h>
#include <string.h>

static int exec_fail(FlatNativeCtx *ctx, const char *reason) {
    if (ctx && ctx->io_status) {
        *ctx->io_status = FLAT_IO_ERR;
    }
    if (ctx && reason && reason != ctx->panic_detail) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", reason);
    }
    return -1;
}

static void exec_bind_vm(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = ctx && ctx->vcx ? ctx->vcx : flat_vcx_runtime_active();

    if (!ctx || !rt || !ctx->bc) {
        flat_executor_bind_vm(NULL);
        return;
    }
    flat_executor_bind_vm(&(FlatExecutorVmBind){
        .vcx = rt,
        .coll = ctx->coll,
        .pool = ctx->pool,
        .files = ctx->file_tbl,
        .live = ctx->mem_live,
        .bc = ctx->bc,
    });
}

int flat_native_executor_unique_begin(FlatNativeCtx *ctx) {
    char panic[64];

    if (!ctx || !ctx->out || !ctx->io_status) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    exec_bind_vm(ctx);
    panic[0] = '\0';
    if (flat_executor_unique_begin(ctx->out, panic, sizeof panic) != 0) {
        return exec_fail(ctx, panic[0] ? panic : "executor.begin");
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_executor_unique_end(FlatNativeCtx *ctx) {
    char panic[64];

    if (!ctx || !ctx->io_status) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    exec_bind_vm(ctx);
    panic[0] = '\0';
    if (flat_executor_unique_end(panic, sizeof panic) != 0) {
        return exec_fail(ctx, panic[0] ? panic : "executor.end");
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_executor_global_begin(FlatNativeCtx *ctx) {
    char panic[64];

    if (!ctx || !ctx->out || !ctx->io_status) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    exec_bind_vm(ctx);
    panic[0] = '\0';
    if (flat_executor_global_begin(ctx->out, panic, sizeof panic) != 0) {
        return exec_fail(ctx, panic[0] ? panic : "executor.begin");
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}

int flat_native_executor_global_end(FlatNativeCtx *ctx) {
    char panic[64];

    if (!ctx || !ctx->io_status) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    exec_bind_vm(ctx);
    panic[0] = '\0';
    if (flat_executor_global_end(panic, sizeof panic) != 0) {
        return exec_fail(ctx, panic[0] ? panic : "executor.end");
    }
    *ctx->io_status = FLAT_IO_OK;
    return 0;
}