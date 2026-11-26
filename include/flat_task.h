#ifndef FLAT_TASK_H
#define FLAT_TASK_H

#include "flat_runtime.h"
#include "vm.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t FlatTaskId;

typedef enum FlatTaskState {
    FLAT_TASK_CREATED = 1,
    FLAT_TASK_RUNNABLE = 2,
    FLAT_TASK_RUNNING = 3,
    FLAT_TASK_WAITING = 4,
    FLAT_TASK_COMPLETED = 5,
    FLAT_TASK_FAILED = 6,
    FLAT_TASK_CANCELLED = 7
} FlatTaskState;

/* Create a task owned by runtime. The task starts in CREATED state. */
int flat_runtime_task_create(FlatRuntime *runtime, const FlatBytecode *bc,
                             const FlatVmOptions *opts, FlatTaskId *out_id);
/* CREATED -> RUNNABLE. Starting an already-started task is an error. */
int flat_runtime_task_start(FlatRuntime *runtime, FlatTaskId id);
/* Put a RUNNABLE task to sleep until target reaches a terminal state.
 * Self-wait, unknown IDs, non-RUNNABLE waiters, and any dependency that would
 * create a wait cycle are rejected. If target is already terminal, waiter
 * remains RUNNABLE. */
int flat_runtime_task_wait(FlatRuntime *runtime, FlatTaskId waiter, FlatTaskId target);
/* Cancel a CREATED, RUNNABLE, or WAITING task at a scheduler safe point.
 * RUNNING and terminal tasks cannot be cancelled. */
int flat_runtime_task_cancel(FlatRuntime *runtime, FlatTaskId id);
/* Run the cooperative scheduler. quantum is the VM instruction budget per turn.
 * max_quanta == 0 runs until no task is RUNNABLE. Returns 0 on success and
 * writes the number of executed turns to out_quanta when it is non-NULL.
 * Task registry/scheduler APIs are confined to the scheduler-driving host thread. */
int flat_runtime_schedule(FlatRuntime *runtime, uint64_t quantum,
                          uint64_t max_quanta, uint64_t *out_quanta);
/* Query state/result. Results are available only after a terminal transition. */
FlatTaskState flat_runtime_task_state(const FlatRuntime *runtime, FlatTaskId id);
int flat_runtime_task_result(const FlatRuntime *runtime, FlatTaskId id,
                             FlatVmResult *out_result);
/* Destroy a CREATED or terminal task and release its VM. Rejected while
 * another WAITING task depends on this task; cancel/resolve waiters first. */
int flat_runtime_task_destroy(FlatRuntime *runtime, FlatTaskId id);

#ifdef __cplusplus
}
#endif

#endif
