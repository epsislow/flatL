# FlatRuntime scheduler mutex integration — archive 300

## Scope

Archive 300 integrates the scheduler mutex and dispatch ownership state into the existing cooperative `FlatRuntime`. It does **not** create worker threads and does not connect the MPMC ready queue to runtime scheduling. The scheduler remains scan-based and has exactly one scheduler-driving host thread at a time.

## Runtime synchronization

- `FlatRuntime` now owns a `FlatMutex scheduler_mutex`; the previous C11 atomic spin gate is removed.
- The mutex protects task registry publication/removal, task IDs, task states/results, wait-graph edges, Channel registry membership/reference metadata, `active_runs`, and scheduler-driver admission.
- Public task create/start/wait/cancel/state/result/destroy operations use the same mutex for registry/state transitions. Host threads may query state and start CREATED tasks while the cooperative scheduler is running.
- `flat_runtime_schedule()` admits one scheduler driver. An overlapping call returns `-1`; it does not wait and does not run a second scheduler concurrently.
- `flat_vm_step()` always executes after releasing `scheduler_mutex`. The scheduler reacquires the mutex to publish the result/state and release dispatch ownership.
- `flat_runtime_destroy()` still requires exclusive host-side lifetime control. The mutex cannot make it safe for a caller to race a new API entry against destruction of the `FlatRuntime` allocation itself.

## Dispatch ownership integration

Each `FlatTaskRecord` now embeds `FlatTaskDispatchState`.

- A task entering RUNNABLE is marked queued.
- Scheduler selection and `flat_task_dispatch_claim()` occur under `scheduler_mutex`; the returned dispatch token owns that VM turn.
- After `flat_vm_step()` returns, the scheduler publishes the task state and calls `flat_task_dispatch_release()` under the mutex.
- A normal yielded RUNNABLE task is queued again after ownership release.
- A wakeup received while the task is still worker-owned sets `wake_pending`; release materializes one queued state and prevents duplicate ownership.
- The current runtime still scans the task registry to select a task. The standalone MPMC `FlatReadyQueue` remains unused by `FlatRuntime` until a later milestone.

## Lock order and lifetime rules

- The lock order is `scheduler_mutex` → individual Channel gate.
- Code must release a Channel gate before calling any helper that acquires `scheduler_mutex`; Channel unlock/wakeup follows this rule.
- `flat_vm_step()`, native dispatch, logging/output, and blocking host operations are never executed while holding `scheduler_mutex`.
- Channel acquisition pins the Channel under the registry mutex before releasing the registry mutex. Owner-aware acquisition validates the task and adds its Channel reference atomically with registry lookup.
- Automatic transaction abort/rollback now wakes Channel waiters for each Channel whose owner transaction was aborted; waiter state/dispatch publication occurs under `scheduler_mutex` after the Channel gate is released.
- Task destroy checks `dispatch.worker_owned` in addition to lifecycle state. A RUNNING task cannot be destroyed.
- Cooperative cancellation also rejects `worker_owned` tasks. This matters because a native `task.wait()`/Channel retry callback may publish WAITING before its enclosing `flat_vm_step()` has returned; that VM is still owned and must not be cancelled/destroyed concurrently.
- The existing synchronous `flat_runtime_task_cancel()` remains in place. The request-only cancellation API and DRAIN/CANCEL worker shutdown from archive 296 are not implemented.

## Current limitations

- No pthread worker pool is created.
- No runtime worker loop or condition-variable wait loop exists.
- The MPMC ready queue is not connected to runtime task transitions; scan-based selection remains.
- `FlatChannel` still uses its existing per-Channel atomic gate. Converting it to `FlatMutex` and finalizing lock-order integration is a separate step.
- Worker admission preflight is not yet called from a scheduler dispatch boundary.
- Cancellation request, shutdown modes, worker join, and runtime teardown after join remain unimplemented.
- The host must prevent `flat_runtime_destroy()` from racing any new API entry.
- This milestone does not establish complete-VM thread safety or worker-pool readiness.

## Tests

`test/cases/flvm/038_runtime_scheduler_mutex/` is registered as `flvm[38]` in `bin/run_tests`.

It creates 128 independent tasks, starts 64, then while the scheduler is running:

- queries task state concurrently from another host thread;
- starts the remaining 64 CREATED tasks;
- attempts a second scheduler call and verifies it is rejected;
- attempts to cancel and destroy the currently RUNNING task and verifies both operations are rejected;
- verifies all tasks complete and can be queried/destroyed afterward.

The test is repeated and run under ThreadSanitizer. It also holds the DOM invocation gate while another thread cancels a yielded DOM task and destroys a CREATED DOM task; concurrent task-state queries must continue while VM teardown waits for the gate. This proves the scheduler mutex is not held across VM cancellation/destruction. It validates the POSIX implementation in the current environment, not Windows or Emscripten.


## Archive 302 follow-up

Archive 302 keeps the `FlatRuntime` scheduler mutex and integrates the MPMC ready queue. The scheduler removes queue entries for wait/cancel transitions, claims queue entries while holding `scheduler_mutex`, and runs `flat_vm_step()` after releasing it. The Channel gate has also moved to `FlatMutex`. The updated lock-order contract and validation are in [`14_READY_QUEUE_CHANNEL_MUTEX_INTEGRATION.md`](14_READY_QUEUE_CHANNEL_MUTEX_INTEGRATION.md). Worker threads and request-only cancellation remain unimplemented.
