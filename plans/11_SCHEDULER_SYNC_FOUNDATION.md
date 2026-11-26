# Scheduler synchronization foundation — archive 299

## Scope and status

Archive 299 adds reusable synchronization primitives and a tested ready-queue/dispatch-ownership foundation. It does **not** connect these structures to `FlatRuntime`, change the public task API, or start worker threads for VM execution. The current cooperative scheduler remains the only runtime scheduler.

## Cross-platform synchronization abstraction

`include/flat_sync.h` now exposes:

- `FlatMutex`: init/destroy/lock/unlock;
- `FlatCond`: init/destroy/wait/signal/broadcast;
- `FlatThread`: create/join using a `void *(*)(void *)` entry point.

Platform mapping:

| Platform | Mutex / condition | Thread |
|---|---|---|
| POSIX | pthread mutex / condition variable | pthread create / join |
| Windows | SRWLOCK / CONDITION_VARIABLE | `_beginthreadex` / wait / close handle |
| Emscripten without pthreads | no-op mutex; blocking condition wait is rejected | create/join return unsupported |
| Emscripten with pthreads | pthread implementation | pthread create / join |

The wrapper deliberately has no detach, cancellation, timed wait, recursive mutex, or priority API. A created thread must be joined. A single-thread Emscripten build cannot use the blocking queue as a worker queue; it retains cooperative runtime support.

## Ready queue primitive

`include/flat_scheduler_sync.h` and `src/common/flat_scheduler_sync.c` add an internal MPMC FIFO queue of nonzero `uint64_t` task IDs.

- `push` appends and signals one consumer.
- `pop` waits in a predicate loop; it returns an item, or returns drained when the queue is closed and empty.
- `close` rejects later pushes and broadcasts to all blocked consumers.
- Consumers drain already-published items after close.
- `destroy` rejects an open or non-drained queue; the caller must close it, drain it, and join all producers/consumers first.
- Capacity grows dynamically; queue entries are IDs, never `FlatTaskRecord *` pointers. A future worker must revalidate the ID in the task registry after pop.

The queue does not deduplicate IDs. Duplicate suppression belongs to the task dispatch state under the future runtime scheduler mutex. The queue mutex must not be nested with the scheduler mutex: publish/pop queue items outside scheduler critical sections, and validate/claim the corresponding task state separately. Stale IDs caused by cancellation are expected to be discarded by registry/state validation in the future integration.

## Task dispatch ownership state

`FlatTaskDispatchState` is a small state machine whose functions must all be called while holding the future runtime scheduler mutex:

- `queued`: one queue publication is pending or present;
- `worker_owned`: exactly one worker owns the VM turn;
- `wake_pending`: a wakeup arrived while that worker still owned the VM.
- `dispatch_token`: monotonically advances for each ownership claim; stale worker completion tokens are rejected.

Rules:

1. A task cannot be both `queued` and `worker_owned`.
2. Repeated enqueue/wakeup while queued is coalesced.
3. Wakeup while worker-owned sets `wake_pending`; it does not publish a second queue item.
4. Worker release validates the dispatch token. If `wake_pending` is set, release clears it and requests exactly one requeue.
5. The caller publishes that requeue after releasing the scheduler mutex.

This captures the wake-before-current-turn-release race identified in the worker-pool specification. It is not yet wired into `FlatTaskRecord`; no claim is made that the current runtime can execute multiple task VMs concurrently.

## Locking constraints for future integration

- Scheduler mutex protects task registry/state, wait graph, dispatch state, IDs, queue publication decisions, and worker/pool lifecycle.
- Never hold scheduler mutex while calling `flat_vm_step`, invoking native handlers, doing IO, or waiting on a condition variable except through the condition wait that atomically releases it.
- Queue operations use their own mutex and are not nested with the scheduler mutex.
- Worker pops an ID, then acquires scheduler mutex to find/validate/claim the task. It releases scheduler mutex before VM execution.
- On return from `flat_vm_step`, worker reacquires scheduler mutex to publish the new task state and release dispatch ownership. A wakeup arriving during the turn is coalesced in `wake_pending` and materialized after release.
- Task record lifetime, Channel registry lifetime, and runtime API entry lifetime still need a complete integration protocol; this archive does not solve them.

## Tests

`test/cases/flvm/037_scheduler_sync_primitives/` is registered in the existing `bin/run_tests` framework as `flvm[37]`.

It verifies:

- MPMC queue stress with four producers, four consumers, and 20,000 unique IDs;
- no loss or duplicate delivery;
- dynamic queue growth from a small initial capacity and FIFO preservation;
- close rejects new pushes while consumers drain existing entries;
- close broadcasts to all consumers blocked on an empty queue;
- duplicate enqueue/wakeup coalescing;
- wakeup while worker-owned is deferred;
- release materializes exactly one requeue;
- stale dispatch tokens cannot release a newer worker ownership claim.

This is a primitive/state-machine test, not a complete runtime or VM concurrency test.

## Not implemented / next gates

- No scheduler mutex has replaced the current `FlatRuntime` atomic gate.
- No task registry or wait-graph API has been made safe for concurrent callers.
- No `worker_owned` fields have been added to runtime task records.
- No worker pool is started by `FlatRuntime`.
- No cancellation-request or DRAIN/CANCEL API is implemented.
- No Channel mutex conversion or runtime/Channel lock-order integration is implemented.
- No runtime teardown/join protocol is implemented.

Next milestone: integrate the scheduler mutex and dispatch state into `FlatRuntime` while preserving the rule that `flat_vm_step()` and native handlers execute outside the scheduler critical section. Only after that should worker threads consume the ready queue.

## Superseded integration status after archive 300

The statements above describe the exact state at archive 299. Archive 300 now replaces the runtime atomic spin gate with `FlatMutex`, embeds `FlatTaskDispatchState` in each `FlatTaskRecord`, and synchronizes task registry/state/wait-graph operations. The cooperative scheduler claims/releases dispatch ownership while continuing to execute `flat_vm_step()` outside the scheduler mutex.

The MPMC ready queue itself is still not connected to `FlatRuntime`; runtime selection remains a registry scan. No worker threads are started. See [`12_RUNTIME_SCHEDULER_MUTEX.md`](12_RUNTIME_SCHEDULER_MUTEX.md) for the current integrated state and remaining gates.


## Archive 302 — Runtime integration update

The queue is no longer standalone: `FlatRuntime` now owns it and uses FIFO publication/pop for cooperative task scheduling. `FlatTaskDispatchState` remains protected by `scheduler_mutex`; queue pop and dispatch claim are performed in the same critical section. Transaction-owning tasks retain their existing priority and are removed from the FIFO before claim. The queue also exposes nonblocking pop and task-ID removal. The blocking pop remains available for the future worker loop. See [`14_READY_QUEUE_CHANNEL_MUTEX_INTEGRATION.md`](14_READY_QUEUE_CHANNEL_MUTEX_INTEGRATION.md).
