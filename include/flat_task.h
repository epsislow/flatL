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
/* Cancel a CREATED, RUNNABLE, or WAITING task when it is not currently owned
 * by a scheduler turn. A task whose native wait callback has published WAITING
 * but whose flat_vm_step() has not returned is still worker-owned and cannot be
 * cancelled/destroyed yet. RUNNING and terminal tasks cannot be cancelled. */
int flat_runtime_task_cancel(FlatRuntime *runtime, FlatTaskId id);
/* Request-only cancellation. Returns 0 when accepted (including repeated
 * requests), 1 when the task is already terminal, and -1 for an invalid or
 * unknown task. Cleanup is performed by the scheduler on its next turn; this
 * function never cancels or destroys a VM on the caller's thread. */
int flat_runtime_task_cancel_request(FlatRuntime *runtime, FlatTaskId id);
/* Run the cooperative scheduler. quantum is the VM instruction budget per turn.
 * max_quanta == 0 runs until no task is RUNNABLE. Returns 0 on success and
 * writes the number of executed turns to out_quanta when it is non-NULL.
 * Runtime task registry/state APIs are synchronized and may be queried or updated
 * by other host threads while the runtime remains alive. Only one thread may drive
 * flat_runtime_schedule() at a time; overlapping scheduler calls are rejected.
 * Runtime destruction still requires exclusive host-side lifetime control. */
int flat_runtime_schedule(FlatRuntime *runtime, uint64_t quantum,
                          uint64_t max_quanta, uint64_t *out_quanta);
/* Query state/result. Results are available only after a terminal transition. */
FlatTaskState flat_runtime_task_state(const FlatRuntime *runtime, FlatTaskId id);
int flat_runtime_task_result(const FlatRuntime *runtime, FlatTaskId id,
                             FlatVmResult *out_result);
/* Destroy a CREATED or terminal task and release its VM. Rejected while
 * another WAITING task depends on this task or while a scheduler turn owns it.
 * VM teardown runs outside scheduler_mutex while the task record remains pinned. */
int flat_runtime_task_destroy(FlatRuntime *runtime, FlatTaskId id);

#ifdef __cplusplus
}
#endif

#endif
