#ifndef FLAT_EXECUTOR_H
#define FLAT_EXECUTOR_H

#include "fb_format.h"
#include "flat_vcx.h"
#include "flat_value.h"

#include <stddef.h>
#include <stdint.h>

typedef struct FlatFileTable FlatFileTable;
typedef struct FlatMemLiveStats FlatMemLiveStats;

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

void flat_executor_reset_session(void);
void flat_executor_bind_vm(const FlatExecutorVmBind *bind);

/* Returns body block id NUMBER (targetBlock) in out_val; pushes executor frame. */
int flat_executor_unique_begin(FlatValue *out_val, char *panic, size_t panic_sz);
int flat_executor_unique_end(char *panic, size_t panic_sz);
int flat_executor_global_begin(FlatValue *out_val, char *panic, size_t panic_sz);
int flat_executor_global_end(char *panic, size_t panic_sz);

/* Before VM routes to a panic handler: LIFO panic_end all open frames. */
void flat_executor_panic_drain(char *panic, size_t panic_sz);

/* FlatL executor (#fl_*): unwind one pending instr context (no copyCallReturn). */
int flat_executor_panic_unwind_instr(uint32_t instr_id, FlatExecutorScope scope, char *panic,
                                     size_t panic_sz);

/* After native frame drain: LIFO unwind pending instr VCX from VM call stack (#fl_). */
void flat_executor_panic_drain_calls(const FlatBytecode *bc, const uint32_t *func_instr_ctx,
                                     int call_sp, char *panic, size_t panic_sz);

int flat_executor_block_label_for_id(const FlatBytecode *bc, uint32_t block_id, char *out,
                                     size_t out_sz);

#endif