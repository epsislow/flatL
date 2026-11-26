#ifndef FLAT_SCHEDULER_SYNC_H
#define FLAT_SCHEDULER_SYNC_H

#include "flat_sync.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Internal MPMC ready-queue primitive. FlatRuntime publishes and removes IDs
 * while holding scheduler_mutex and coordinates duplicate suppression through
 * FlatTaskDispatchState. Queue consumers release the queue mutex before taking
 * scheduler_mutex.
 *
 * pop(): 1 = item, 0 = closed and drained, -1 = synchronization/error.
 * push(): 0 = queued, -1 = closed/allocation/synchronization error.
 */
typedef struct FlatReadyQueue {
    FlatMutex mutex;
    FlatCond available;
    uint64_t *items;
    size_t capacity;
    size_t head;
    size_t count;
    int closed;
    uint64_t wake_epoch;
    int initialized;
} FlatReadyQueue;

int flat_ready_queue_init(FlatReadyQueue *queue, size_t initial_capacity);
int flat_ready_queue_push(FlatReadyQueue *queue, uint64_t task_id);
int flat_ready_queue_pop(FlatReadyQueue *queue, uint64_t *out_task_id);
/* Wait until an item is available, the queue closes, or wake_all interrupts the wait.
 * Returns 1 for available work, 0 for closed, 2 for an explicit wake, -1 on error. */
uint64_t flat_ready_queue_wake_epoch(FlatReadyQueue *queue);
int flat_ready_queue_wait_since(FlatReadyQueue *queue, uint64_t observed_epoch);
int flat_ready_queue_wait(FlatReadyQueue *queue);
int flat_ready_queue_wake_all(FlatReadyQueue *queue);
/* Nonblocking pop: 1 = item, 0 = currently empty, -1 = error. */
int flat_ready_queue_try_pop(FlatReadyQueue *queue, uint64_t *out_task_id);
/* Remove one queued occurrence: 1 = removed, 0 = absent, -1 = error. */
int flat_ready_queue_remove(FlatReadyQueue *queue, uint64_t task_id);
int flat_ready_queue_close(FlatReadyQueue *queue);
int flat_ready_queue_destroy(FlatReadyQueue *queue);

/*
 * Task dispatch ownership state. All calls must be made while holding the
 * runtime scheduler mutex; this type deliberately contains no internal lock.
 * A dispatch token prevents a stale worker completion from releasing a newer
 * ownership claim. Wakeups received during a worker turn are coalesced and
 * materialized as exactly one requeue at release.
 */
typedef struct FlatTaskDispatchState {
    uint64_t dispatch_token;
    int queued;
    int worker_owned;
    int wake_pending;
} FlatTaskDispatchState;

void flat_task_dispatch_init(FlatTaskDispatchState *state);
/* Returns 1 if caller must publish a queue item, 0 if already represented. */
int flat_task_dispatch_enqueue(FlatTaskDispatchState *state);
/* Claims a queued task and returns its new nonzero token. */
int flat_task_dispatch_claim(FlatTaskDispatchState *state, uint64_t *out_token);
/* Records a wakeup; returns 1 if caller must publish a queue item now. */
int flat_task_dispatch_wake(FlatTaskDispatchState *state);
/* Releases matching worker ownership; out_requeue is 1 for one deferred wake. */
int flat_task_dispatch_release(FlatTaskDispatchState *state, uint64_t token,
                               int *out_requeue);

#ifdef __cplusplus
}
#endif
#endif
