#ifndef FLAT_EXECUTOR_H
#define FLAT_EXECUTOR_H

#include "fb_format.h"
#include "flat_vcx.h"
#include "flat_value.h"

#include <stddef.h>
#include <stdint.h>

typedef struct FlatFileTable FlatFileTable;
typedef struct FlatMemLiveStats FlatMemLiveStats;

#define FLAT_EXEC_INSTR_STACK_MAX 64

typedef enum {
    FLAT_EXEC_SCOPE_UNIQUE = 0,
    FLAT_EXEC_SCOPE_GLOBAL = 1
} FlatExecutorScope;

typedef struct {
    FlatVcxRuntime *vcx;
    FlatCollectionPool *coll;
    FlatStringPool *pool;
    FlatFileTable *files;
    FlatMemLiveStats *live;
    const FlatBytecode *bc;
} FlatExecutorVmBind;

typedef struct FlatExecutorContext {
    FlatExecutorVmBind bind;
    int bind_ok;
    struct FlatExecutorFrameStorage {
        FlatExecutorScope scope;
        uint32_t instr_id;
        uint32_t calle_vcx;
        uint32_t func_vcx;
    } frames[32];
    int frame_sp;
    uint32_t instr_stack[FLAT_EXEC_INSTR_STACK_MAX];
    int instr_push_call_sp[FLAT_EXEC_INSTR_STACK_MAX];
    int instr_stack_sp;
    int last_call_function_sp;
    uint32_t call_instr_id;
} FlatExecutorContext;

void flat_executor_context_reset(FlatExecutorContext *ctx);
void flat_executor_context_bind_vm(FlatExecutorContext *ctx, const FlatExecutorVmBind *bind);
void flat_executor_instr_stack_push(FlatExecutorContext *ctx, uint32_t id, int call_sp);
int flat_executor_instr_stack_pop(FlatExecutorContext *ctx, uint32_t id);
uint32_t flat_executor_instr_stack_peek(const FlatExecutorContext *ctx);
void flat_executor_instr_stack_clear(FlatExecutorContext *ctx);

/* Returns body block id NUMBER (targetBlock) in out_val; pushes executor frame. */
int flat_executor_unique_begin(FlatExecutorContext *ctx, FlatValue *out_val, char *panic, size_t panic_sz);
int flat_executor_unique_end(FlatExecutorContext *ctx, char *panic, size_t panic_sz);
int flat_executor_global_begin(FlatExecutorContext *ctx, FlatValue *out_val, char *panic, size_t panic_sz);
int flat_executor_global_end(FlatExecutorContext *ctx, char *panic, size_t panic_sz);

/* Before VM routes to a panic handler: LIFO panic_end all open frames. */
void flat_executor_panic_drain(FlatExecutorContext *ctx, char *panic, size_t panic_sz);

/* FlatL executor (#fl_*): unwind one pending instr context (no copyCallReturn). */
int flat_executor_panic_unwind_instr(FlatExecutorContext *ctx, uint32_t instr_id, FlatExecutorScope scope, char *panic,
                                     size_t panic_sz);

/* After native frame drain: LIFO unwind pending instr VCX from VM call stack (#fl_). */
void flat_executor_panic_drain_calls(FlatExecutorContext *ctx, const FlatBytecode *bc, const uint32_t *func_instr_ctx,
                                     int call_sp, char *panic, size_t panic_sz);

int flat_executor_block_label_for_id(const FlatBytecode *bc, uint32_t block_id, char *out,
                                     size_t out_sz);

#endif