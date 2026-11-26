#include "flat_executor.h"

#include "fgir.h"
#include "flat_number.h"
#include "flat_vcx.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FLAT_EXEC_MAX_FRAMES 32

typedef struct FlatExecutorFrameStorage FlatExecutorFrame;


static int u32_from_number(double d, uint32_t *out) {
    if (!out || d != d || d < 0.0 || d > (double)UINT32_MAX || floor(d) != d) {
        return -1;
    }
    *out = (uint32_t)d;
    return 0;
}

static int read_ctx_var_u32(FlatExecutorContext *ctx, uint32_t ctx_id, const char *name, uint32_t *out, char *panic,
                            size_t panic_sz) {
    FlatValue v;

    if (!ctx || !ctx->bind_ok || !ctx->bind.vcx || !out) {
        return -1;
    }
    if (flat_vcx_get_var(ctx->bind.vcx, ctx_id, name, &v, ctx->bind.coll, ctx->bind.pool, ctx->bind.files, panic,
                         panic_sz) != 0) {
        return -1;
    }
    if (v.tag != VAL_NUMBER) {
        flat_value_release(ctx->bind.coll, ctx->bind.pool, &v);
        return -1;
    }
    if (u32_from_number(flat_number_as_f64(&v), out) != 0) {
        flat_value_release(ctx->bind.coll, ctx->bind.pool, &v);
        return -1;
    }
    flat_value_release(ctx->bind.coll, ctx->bind.pool, &v);
    return 0;
}

int flat_executor_block_label_for_id(const FlatBytecode *bc, uint32_t block_id, char *out,
                                     size_t out_sz) {
    size_t i;

    if (!bc || !out || out_sz == 0) {
        return -1;
    }
    for (i = 0; i < bc->block_count; i++) {
        if (bc->blocks[i].block_id == block_id) {
            if (bc->block_labels && bc->block_labels[i][0] != '\0') {
                snprintf(out, out_sz, "%s", bc->block_labels[i]);
                return 0;
            }
            snprintf(out, out_sz, "#%08x", (unsigned)block_id);
            return 0;
        }
    }
    return -1;
}

static int read_target_block_id(FlatExecutorContext *ctx, uint32_t instr_id, uint32_t *out_bid, char *panic, size_t panic_sz) {
    FlatValue v;

    if (!ctx || !ctx->bind_ok || !out_bid) {
        return -1;
    }
    if (flat_vcx_get_var(ctx->bind.vcx, instr_id, "targetBlock", &v, ctx->bind.coll, ctx->bind.pool,
                         ctx->bind.files, panic, panic_sz) != 0) {
        return -1;
    }
    if (v.tag != VAL_NUMBER) {
        flat_value_release(ctx->bind.coll, ctx->bind.pool, &v);
        snprintf(panic, panic_sz, "executor.begin.target");
        return -1;
    }
    if (u32_from_number(flat_number_as_f64(&v), out_bid) != 0) {
        flat_value_release(ctx->bind.coll, ctx->bind.pool, &v);
        snprintf(panic, panic_sz, "executor.begin.target");
        return -1;
    }
    flat_value_release(ctx->bind.coll, ctx->bind.pool, &v);
    return 0;
}

static int set_out_block_id(FlatValue *out, uint32_t block_id) {
    if (!out) {
        return -1;
    }
    return flat_value_set_number(out, (double)block_id);
}

static int frame_push(FlatExecutorContext *ctx, FlatExecutorScope scope, uint32_t instr_id, uint32_t calle_vcx,
                      uint32_t func_vcx) {
    if (!ctx || ctx->frame_sp >= FLAT_EXEC_MAX_FRAMES) {
        return -1;
    }
    ctx->frames[ctx->frame_sp].scope = scope;
    ctx->frames[ctx->frame_sp].instr_id = instr_id;
    ctx->frames[ctx->frame_sp].calle_vcx = calle_vcx;
    ctx->frames[ctx->frame_sp].func_vcx = func_vcx;
    ctx->frame_sp++;
    return 0;
}

static int frame_pop(FlatExecutorContext *ctx, FlatExecutorFrame *out) {
    if (!ctx || ctx->frame_sp <= 0) {
        return -1;
    }
    ctx->frame_sp--;
    if (out) {
        *out = ctx->frames[ctx->frame_sp];
    }
    return 0;
}

static int panic_end_one(FlatExecutorContext *ctx, const FlatExecutorFrame *fr, int on_panic, char *panic, size_t panic_sz) {
    if (!ctx || !ctx->bind_ok || !ctx->bind.vcx) {
        return -1;
    }
    if (flat_vcx_set(ctx->bind.vcx, fr->calle_vcx, panic, panic_sz) != 0) {
        return -1;
    }
    if (fr->scope == FLAT_EXEC_SCOPE_UNIQUE) {
        if (!on_panic) {
            if (flat_vcx_copy_call_return(ctx->bind.vcx, fr->func_vcx, fr->calle_vcx, fr->instr_id,
                                          ctx->bind.bc, ctx->bind.coll, ctx->bind.pool, ctx->bind.files,
                                          ctx->bind.live, panic, panic_sz) != 0) {
                return -1;
            }
        }
        if (flat_vcx_destroy_ctx(ctx->bind.vcx, fr->func_vcx, ctx->bind.coll, ctx->bind.pool, ctx->bind.files,
                                 panic, panic_sz) != 0) {
            return -1;
        }
    }
    if (flat_vcx_destroy_ctx(ctx->bind.vcx, fr->instr_id, ctx->bind.coll, ctx->bind.pool, ctx->bind.files, panic,
                             panic_sz) != 0) {
        return -1;
    }
    return 0;
}

void flat_executor_context_reset(FlatExecutorContext *ctx) {
    if (!ctx) {
        return;
    }
    memset(ctx, 0, sizeof *ctx);
}

void flat_executor_instr_stack_push(FlatExecutorContext *ctx, uint32_t id, int call_sp) {
    if (!ctx || id < 1u || ctx->instr_stack_sp >= FLAT_EXEC_INSTR_STACK_MAX) return;
    ctx->instr_push_call_sp[ctx->instr_stack_sp] = call_sp;
    ctx->instr_stack[ctx->instr_stack_sp++] = id;
}

int flat_executor_instr_stack_pop(FlatExecutorContext *ctx, uint32_t id) {
    if (!ctx || ctx->instr_stack_sp <= 0 || ctx->instr_stack[ctx->instr_stack_sp - 1] != id) return 0;
    ctx->instr_stack_sp--;
    return 1;
}

uint32_t flat_executor_instr_stack_peek(const FlatExecutorContext *ctx) {
    return ctx && ctx->instr_stack_sp > 0 ? ctx->instr_stack[ctx->instr_stack_sp - 1] : 0u;
}

void flat_executor_instr_stack_clear(FlatExecutorContext *ctx) {
    if (!ctx) return;
    ctx->instr_stack_sp = 0;
    ctx->last_call_function_sp = 0;
    ctx->call_instr_id = 0u;
}

void flat_executor_context_bind_vm(FlatExecutorContext *ctx, const FlatExecutorVmBind *bind) {
    if (!ctx) {
        return;
    }
    if (!bind || !bind->vcx || !bind->bc) {
        ctx->bind_ok = 0;
        memset(&ctx->bind, 0, sizeof ctx->bind);
        return;
    }
    ctx->bind = *bind;
    ctx->bind_ok = 1;
}

void flat_executor_panic_drain(FlatExecutorContext *ctx, char *panic, size_t panic_sz) {
    FlatExecutorFrame fr;

    if (!ctx) return;
    while (ctx->frame_sp > 0) {
        if (frame_pop(ctx, &fr) != 0) {
            break;
        }
        if (panic_end_one(ctx, &fr, 1, panic, panic_sz) != 0) {
            if (panic && panic_sz > 0) {
                panic[0] = '\0';
            }
        }
    }
}

static FlatExecutorScope scope_from_executor_block_id(const FlatBytecode *bc,
                                                      uint32_t executor_block_id) {
    char lbl[FGIR_MAX_LABEL];

    if (!bc) {
        return FLAT_EXEC_SCOPE_UNIQUE;
    }
    if (flat_executor_block_label_for_id(bc, executor_block_id, lbl, sizeof lbl) != 0) {
        return FLAT_EXEC_SCOPE_UNIQUE;
    }
    if (strstr(lbl, "global.scope") != NULL) {
        return FLAT_EXEC_SCOPE_GLOBAL;
    }
    return FLAT_EXEC_SCOPE_UNIQUE;
}

static int infer_unique_child_vcx(FlatExecutorContext *ctx, uint32_t instr_id, uint32_t calle_vcx, uint32_t *func_vcx) {
    uint32_t cur;

    if (!func_vcx) {
        return -1;
    }
    *func_vcx = 0u;
    if (!ctx || !ctx->bind_ok || !ctx->bind.vcx) {
        return -1;
    }
    cur = flat_vcx_current_id(ctx->bind.vcx);
    if (cur == 0u || cur == 1u || cur == instr_id || cur == calle_vcx) {
        return 0;
    }
    *func_vcx = cur;
    return 0;
}

int flat_executor_panic_unwind_instr(FlatExecutorContext *ctx, uint32_t instr_id, FlatExecutorScope scope, char *panic,
                                     size_t panic_sz) {
    FlatExecutorFrame fr;
    uint32_t calle_vcx = 0;

    if (!ctx || !ctx->bind_ok || instr_id < 1u) {
        return -1;
    }
    if (read_ctx_var_u32(ctx, instr_id, "calleVcx", &calle_vcx, panic, panic_sz) != 0) {
        return -1;
    }
    memset(&fr, 0, sizeof fr);
    fr.scope = scope;
    fr.instr_id = instr_id;
    fr.calle_vcx = calle_vcx;
    fr.func_vcx = 0u;
    if (scope == FLAT_EXEC_SCOPE_UNIQUE &&
        infer_unique_child_vcx(ctx, instr_id, calle_vcx, &fr.func_vcx) != 0) {
        return -1;
    }
    (void)flat_executor_instr_stack_pop(ctx, instr_id);
    return panic_end_one(ctx, &fr, 1, panic, panic_sz);
}

static FlatExecutorScope scope_for_instr(FlatExecutorContext *ctx, const FlatBytecode *bc, uint32_t instr_id, char *panic,
                                         size_t panic_sz) {
    uint32_t fi;

    if (!bc) {
        return FLAT_EXEC_SCOPE_UNIQUE;
    }
    if (read_ctx_var_u32(ctx, instr_id, "__fl_funcIndex", &fi, panic, panic_sz) != 0) {
        return FLAT_EXEC_SCOPE_UNIQUE;
    }
    if (fi >= bc->function_count) {
        return FLAT_EXEC_SCOPE_UNIQUE;
    }
    return scope_from_executor_block_id(bc, bc->functions[fi].executor_block_id);
}

void flat_executor_panic_drain_calls(FlatExecutorContext *ctx, const FlatBytecode *bc, const uint32_t *func_instr_ctx,
                                     int call_sp, char *panic, size_t panic_sz) {
    int i;

    flat_executor_panic_drain(ctx, panic, panic_sz);
    if (!func_instr_ctx || call_sp <= 0) {
        return;
    }
    for (i = call_sp - 1; i >= 0; i--) {
        uint32_t instr = func_instr_ctx[i];
        FlatExecutorScope scope;

        if (instr == 0u) {
            continue;
        }
        scope = scope_for_instr(ctx, bc, instr, panic, panic_sz);
        if (flat_executor_panic_unwind_instr(ctx, instr, scope, panic, panic_sz) != 0) {
            if (panic && panic_sz > 0) {
                panic[0] = '\0';
            }
        }
    }
}

int flat_executor_unique_begin(FlatExecutorContext *ctx, FlatValue *out_val, char *panic, size_t panic_sz) {
    uint32_t instr_id;
    uint32_t calle_vcx;
    uint32_t func_vcx;
    uint32_t seed = 4u;
    uint32_t body_block_id;
    FlatValue nv;
    double dcalle;

    if (!ctx || !ctx->bind_ok || !out_val) {
        snprintf(panic, panic_sz, "executor.begin.nobind");
        return -1;
    }
    instr_id = ctx->call_instr_id;
    if (instr_id < 1u) {
        snprintf(panic, panic_sz, "executor.begin.callInstr");
        return -1;
    }
    if (read_ctx_var_u32(ctx, instr_id, "calleVcx", &calle_vcx, panic, panic_sz) != 0) {
        snprintf(panic, panic_sz, "executor.begin.calleVcx");
        return -1;
    }
    if (read_ctx_var_u32(ctx, instr_id, "funcVcxSlots", &seed, panic, panic_sz) != 0) {
        seed = 4u;
    }
    if (flat_vcx_create_auto(ctx->bind.vcx, seed, &func_vcx, panic, panic_sz) != 0) {
        if (!panic[0]) {
            snprintf(panic, panic_sz, "executor.begin.createAuto");
        }
        return -1;
    }
    if (flat_vcx_bind_call_params(ctx->bind.vcx, instr_id, func_vcx, ctx->bind.bc, ctx->bind.coll,
                                  ctx->bind.pool, ctx->bind.files, ctx->bind.live, panic, panic_sz) != 0) {
        if (!panic[0]) {
            snprintf(panic, panic_sz, "executor.begin.bindCallParams");
        }
        flat_vcx_destroy_ctx(ctx->bind.vcx, func_vcx, ctx->bind.coll, ctx->bind.pool, ctx->bind.files, panic,
                             panic_sz);
        return -1;
    }
    if (flat_vcx_get_var(ctx->bind.vcx, instr_id, "calleVcx", &nv, ctx->bind.coll, ctx->bind.pool, ctx->bind.files,
                         panic, panic_sz) == 0 && nv.tag == VAL_NUMBER) {
        dcalle = flat_number_as_f64(&nv);
        flat_value_release(ctx->bind.coll, ctx->bind.pool, &nv);
        flat_value_set_number(&nv, dcalle);
        flat_vcx_set_named_var(ctx->bind.vcx, func_vcx, "__calle_id", &nv, ctx->bind.coll, ctx->bind.pool,
                               ctx->bind.files, ctx->bind.live, panic, panic_sz);
        flat_value_release(ctx->bind.coll, ctx->bind.pool, &nv);
    }
    if (read_target_block_id(ctx, instr_id, &body_block_id, panic, panic_sz) != 0) {
        flat_vcx_destroy_ctx(ctx->bind.vcx, func_vcx, ctx->bind.coll, ctx->bind.pool, ctx->bind.files, panic,
                             panic_sz);
        if (!panic[0]) {
            snprintf(panic, panic_sz, "executor.begin.target");
        }
        return -1;
    }
    if (flat_vcx_set(ctx->bind.vcx, func_vcx, panic, panic_sz) != 0) {
        flat_vcx_destroy_ctx(ctx->bind.vcx, func_vcx, ctx->bind.coll, ctx->bind.pool, ctx->bind.files, panic,
                             panic_sz);
        return -1;
    }
    if (frame_push(ctx, FLAT_EXEC_SCOPE_UNIQUE, instr_id, calle_vcx, func_vcx) != 0) {
        snprintf(panic, panic_sz, "executor.begin.frame");
        return -1;
    }
    return set_out_block_id(out_val, body_block_id);
}

int flat_executor_unique_end(FlatExecutorContext *ctx, char *panic, size_t panic_sz) {
    FlatExecutorFrame fr;

    if (!ctx || frame_pop(ctx, &fr) != 0 || fr.scope != FLAT_EXEC_SCOPE_UNIQUE) {
        snprintf(panic, panic_sz, "executor.end.noframe");
        return -1;
    }
    return panic_end_one(ctx, &fr, 0, panic, panic_sz);
}

int flat_executor_global_begin(FlatExecutorContext *ctx, FlatValue *out_val, char *panic, size_t panic_sz) {
    uint32_t instr_id;
    uint32_t calle_vcx;
    uint32_t body_block_id;

    if (!ctx || !ctx->bind_ok || !out_val) {
        snprintf(panic, panic_sz, "executor.begin.nobind");
        return -1;
    }
    instr_id = ctx->call_instr_id;
    if (instr_id < 1u) {
        snprintf(panic, panic_sz, "executor.begin.callInstr");
        return -1;
    }
    if (read_ctx_var_u32(ctx, instr_id, "calleVcx", &calle_vcx, panic, panic_sz) != 0) {
        snprintf(panic, panic_sz, "executor.begin.calleVcx");
        return -1;
    }
    if (flat_vcx_bind_call_params(ctx->bind.vcx, instr_id, calle_vcx, ctx->bind.bc, ctx->bind.coll,
                                  ctx->bind.pool, ctx->bind.files, ctx->bind.live, panic, panic_sz) != 0) {
        if (!panic[0]) {
            snprintf(panic, panic_sz, "executor.begin.bindCallParams");
        }
        return -1;
    }
    if (read_target_block_id(ctx, instr_id, &body_block_id, panic, panic_sz) != 0) {
        if (!panic[0]) {
            snprintf(panic, panic_sz, "executor.begin.target");
        }
        return -1;
    }
    if (frame_push(ctx, FLAT_EXEC_SCOPE_GLOBAL, instr_id, calle_vcx, 0u) != 0) {
        snprintf(panic, panic_sz, "executor.begin.frame");
        return -1;
    }
    return set_out_block_id(out_val, body_block_id);
}

int flat_executor_global_end(FlatExecutorContext *ctx, char *panic, size_t panic_sz) {
    FlatExecutorFrame fr;

    if (!ctx || frame_pop(ctx, &fr) != 0 || fr.scope != FLAT_EXEC_SCOPE_GLOBAL) {
        snprintf(panic, panic_sz, "executor.end.noframe");
        return -1;
    }
    return panic_end_one(ctx, &fr, 0, panic, panic_sz);
}