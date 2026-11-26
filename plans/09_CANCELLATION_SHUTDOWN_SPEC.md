# FlatL v2.0 — Cancellation Request and Worker Shutdown Specification

**Checkpoint:** archive 296  
**Baseline:** archive 295 (`lexbor-independent-context-concurrency`)  
**Status:** architecture contract for the future worker pool; no asynchronous cancellation API, pthread worker pool, or shutdown implementation is added by this archive.

This document separates the cooperative cancellation API that exists today from the request/cleanup protocol required by a multi-worker runtime. The names and exact C signatures below are proposed API shapes; the behavioral rules are the design baseline. Any change to these rules must be recorded in `01_DECISIONS.md` and `02_ROADMAP.md` before implementation.

## 1. Why cancellation must be split into request and execution

The current `flat_runtime_task_cancel()` accepts `CREATED`, `RUNNABLE`, or `WAITING` tasks and directly calls `flat_vm_cancel()`. `flat_vm_cancel()` destroys the VM execution state immediately and rejects a VM that is currently `RUNNING`. This is valid under the current cooperative, host-thread-confined scheduler API.

It is not a valid implementation for worker mode: an arbitrary host thread must never destroy a VM while another worker is executing it. Worker mode therefore separates three events:

1. **Request:** record that cancellation has been requested.
2. **Execution:** the scheduler assigns cancellation cleanup to the worker that currently owns the VM, or to a worker that claims an unowned task cleanup job.
3. **Completion:** only after rollback and all task-owned cleanup have finished does the task transition to `CANCELLED` and wake its dependents.

A request is not a terminal state and does not imply that cleanup has happened.

## 2. Cancellation API and request semantics

### 2.1 Host API

Implemented API:

```c
int flat_runtime_task_cancel_request(FlatRuntime *runtime, FlatTaskId id);
```

The implemented return contract is:

- `0`: accepted, including an already-pending request (idempotent);
- `1`: task already terminal (completion/failure/cancellation has won);
- `-1`: invalid runtime or unknown task ID.

The call is a request-only operation. It only sets a task-record flag under `scheduler_mutex`; it does not call `flat_vm_cancel()`, destroy `VmState`, rollback a Channel transaction, release VM resources, or publish `CANCELLED` from the caller's thread.

The existing `flat_runtime_task_cancel()` remains the synchronous cooperative API for tasks that are not dispatch-owned. It is not repurposed as an asynchronous cross-thread operation. `flat_runtime_task_cancel_request()` is safe to call from another host thread while the scheduler is active.

### 2.2 Request state

Each task has a cancellation-request flag distinct from `FlatTaskState`, protected by `scheduler_mutex`. In archive 303, the single cooperative scheduler scans for pending requests on unowned tasks before selecting runnable work; a RUNNING task's current dispatch owner checks the flag after `flat_vm_step()` returns. This is an intermediate implementation, not yet a dedicated worker cleanup queue.

- Repeated requests are idempotent; they must not enqueue duplicate cancellation work.
- A request against an unknown ID is rejected.
- A request against a terminal task reports already-terminal and does not alter its result.
- A task is not reported as `CANCELLED` merely because the request flag was set.
- The cancellation reason/diagnostic is stored separately from the final `FlatVmResult` until cleanup commits the terminal result.

### 2.3 Safe-point and VM ownership rules

- A worker that is executing a task is the sole owner allowed to step, cancel, or destroy that task's VM.
- A cancellation request observed while `RUNNING` is acted on at the next scheduler-visible safe boundary, after `flat_vm_step()` has returned (normally at the configured quantum boundary). The current VM step may execute multiple opcodes before returning; a long-running native call delays the boundary. The first worker-pool implementation does not asynchronously unwind C frames or interrupt arbitrary native handlers.
- A long-running or blocking native call can therefore delay cancellation and shutdown. Native-specific cancellation/polling is a separate future contract.
- A `CREATED`, `RUNNABLE`, or `WAITING` task that is not currently dispatch-owned is claimed as a cancellation-cleanup job. The current scheduler performs the cleanup without executing another user bytecode instruction. The future worker pool will move this claim to its worker cleanup queue.
- The dispatch ownership token (`worker_owned` or equivalent) prevents another worker from claiming cleanup while the task is still executing. A request arriving while worker-owned sets pending intent; it does not mutate the VM.
- If a wakeup races with cancellation, the scheduler mutex linearizes the state/queue change. A task may be in at most one of RUNNING, READY-queue, WAITING, or cancellation-cleanup ownership at a time. `wake_pending` or an equivalent dispatch token must prevent lost wakeups and duplicate queue entries.

The cancellation request is best-effort with respect to a task already finishing: if the VM reaches a terminal result before the owning worker observes and commits the request, the terminal VM outcome wins. The request API must not rewrite a completed or failed result to `CANCELLED`.

### 2.4 Cancellation commit and cleanup order

The worker-owned cleanup path must perform the following operations exactly once:

1. Stop further bytecode dispatch for the task.
2. Preserve the primary cancellation reason and prepare the cancellation result.
3. Abort/rollback every Channel transaction logically owned by the task.
4. Remove the task's outgoing `waiting_on` and `waiting_channel_id` dependency.
5. Release every task-held Channel reference, including references retained across wakeup/retry.
6. Destroy the VM execution state and task-owned VM resources exactly once.
7. Publish `FLAT_TASK_CANCELLED` only after cleanup has completed.
8. Wake tasks whose `waiting_on` target is now terminal; publish those wakeups through the ready-queue protocol.

A cleanup error is supplemental diagnostic information. It must not replace the original cancellation reason. Dependents are not woken before rollback, reference release, and terminal publication are complete.

No user-level `finally`/`defer` execution is introduced by this contract. Cancellation remains a runtime cleanup outcome, not a language-level unwind.

## 3. Worker-pool lifecycle

### 3.1 Pool states

The worker pool has a lifecycle separate from individual task states:

```text
UNINITIALIZED -> RUNNING -> DRAINING -> STOPPED
                         \-> CANCELLING -> STOPPED
```

- `RUNNING`: accepts task creation/start and normal scheduling.
- `DRAINING`: rejects new task submissions; continues accepted work to terminal outcomes.
- `CANCELLING`: rejects new task submissions and requests cancellation of all nonterminal tasks.
- `STOPPED`: all worker threads have exited and have been joined. No worker may access the runtime after this state.

`CANCELLING` is an escalation from `RUNNING` or `DRAINING`; it cannot be downgraded to `DRAINING`. Repeated shutdown requests with the same or stronger mode are idempotent.

### 3.2 DRAIN semantics

`DRAIN` means finish accepted work without requesting cancellation:

- Atomically transition `RUNNING -> DRAINING` and reject new external task creation/start and new `task.spawn` submissions.
- Continue dispatching tasks already started (`RUNNABLE`, `RUNNING`, and `WAITING`) until they reach terminal outcomes.
- A task already executing may finish its current quantum and any existing wait dependency. No new task is accepted after the transition.
- `CREATED` tasks have not been started/submitted. They are not auto-started by DRAIN and do not keep worker threads busy; their records remain runtime-owned and must be explicitly started before shutdown begins, cancelled, or destroyed by the host before runtime destruction.
- If the accepted task graph becomes quiescent while nonterminal `WAITING` tasks remain and no task can make progress, DRAIN reports a blocked/deadlocked shutdown condition. It must not silently mark the pool STOPPED or discard waiters. The host may resolve the dependency or escalate to `CANCEL`.
- Once every started task is terminal and no worker owns a task, workers are signaled to exit.

DRAIN is not a promise that arbitrary blocking native I/O can be interrupted. A worker remains occupied until that native call returns.

### 3.3 CANCEL semantics

`CANCEL` means stop accepting work and request cleanup for every task:

- Atomically transition `RUNNING` or `DRAINING` to `CANCELLING`.
- Reject task creation/start and `task.spawn` submissions.
- Set cancellation requests for `CREATED`, `RUNNABLE`, `RUNNING`, and `WAITING` tasks.
- Running tasks observe the request at their next safe boundary; non-running tasks are claimed as cleanup jobs.
- Workers remain alive until every accepted cancellation cleanup job has completed and no worker owns a task.
- Only after task cleanup is complete may workers exit.

CANCEL is cooperative, not forceful. It must not use `pthread_cancel()`, terminate a worker asynchronously, or destroy a VM from the shutdown caller. A blocking native call can delay CANCEL until it returns.

### 3.4 Worker join and runtime destruction

The API shape is proposed as two phases:

```c
int flat_runtime_shutdown_request(FlatRuntime *runtime,
                                  FlatRuntimeShutdownMode mode);
int flat_runtime_join(FlatRuntime *runtime);
```

- `shutdown_request` changes pool state, closes submission, queues cancellation if requested, signals workers, and returns without joining them.
- `flat_runtime_join` waits until all workers have exited and joins every created worker exactly once. It returns success only when the pool is `STOPPED`.
- If DRAIN is blocked by unresolved WAITING tasks, join must report the blocked condition (or use a separately specified timed/status API); it must not wait forever without diagnostics. The implementation must settle the exact return/status mechanism before exposing the public API.
- No worker may destroy the runtime or join itself.
- `flat_runtime_destroy()` remains a separate final operation. It is valid only after the pool is STOPPED/joined, all task records and Channels have been explicitly destroyed, and no API call can still enter. The host must still obey the existing external lifetime rule and prevent new calls racing with destroy.
- A stopped runtime may still contain `CREATED` tasks after DRAIN; those records must be destroyed before `flat_runtime_destroy()` succeeds.

## 4. Locking and queue invariants

The runtime scheduler mutex protects pool lifecycle, task registry/state, cancellation flags/queue membership, ready queue, wait graph, worker ownership tokens, active-worker counts and shutdown signaling. Per-Channel mutexes continue to protect Channel storage/transaction metadata. The existing lock order remains scheduler mutex then Channel mutex; no VM execution, native handler, blocking I/O, user callback, worker join, or condition wait may occur while holding either lock (condition wait atomically releases the scheduler mutex).

Required invariants:

- A task is never simultaneously present in the ready queue and cancellation-cleanup queue.
- A task is never owned by two workers.
- A RUNNING task's VM is accessed only by its owner worker; the request caller only publishes cancellation intent.
- A task becomes terminal once; terminal state/result publication is atomic with removal from scheduling/wait queues.
- A waiter is woken only after its target's terminal cleanup/commit is complete.
- Worker exit is published only after no task is RUNNING/worker-owned and no cancellation cleanup remains.
- Runtime destruction cannot begin until all workers are joined and all runtime-owned records/resources have been released.

## 5. Proposed acceptance matrix for worker implementation

The following tests are future requirements. They are not claimed as implemented by archive 296.

### Cancellation request

- [x] Request cancellation for CREATED, RUNNABLE, RUNNING, and WAITING tasks.
- [x] Repeated requests are idempotent and do not duplicate cleanup work.
- [x] Unknown task IDs are rejected; terminal task requests do not rewrite results.
- [x] A RUNNING task is not destroyed by the requesting thread; it is cleaned by its current dispatch owner after `flat_vm_step()` returns at the next scheduler-visible safe boundary.
- Cancellation racing with completion/failure has one deterministic winner and never double-destroys VM state.
- Cancelling a WAITING Channel waiter removes its outgoing wait edge and releases Channel references before dependents are woken.
- Cancellation of an owner rolls back every owned Channel transaction and wakes all dependent tasks after cleanup.
- No task is inserted twice into ready or cancellation queues.
- Cleanup failure is reported without losing the primary cancellation diagnostic.

### DRAIN

- Reject new host task submissions and `task.spawn` after the transition.
- Complete all already-started runnable/running/waiting tasks without converting them to CANCELLED.
- Do not auto-start CREATED tasks; retain their records for explicit host cleanup.
- Detect a quiescent unresolved wait graph and report blocked shutdown rather than silently stopping.
- Do not report join success before every worker exits and is joined.

### CANCEL

- Request cancellation for every task state, including CREATED and WAITING.
- Running native calls are not asynchronously interrupted; cleanup begins after return to a safe point.
- Rollback Channel transactions, remove wait edges, release references and publish CANCELLED exactly once.
- Do not exit workers while cleanup jobs remain.
- Allow escalation from DRAIN to CANCEL; reject downgrade.

### Join and destroy

- Join is idempotent or rejects a second join deterministically without double-joining.
- Runtime destroy is rejected before join, while workers exist, while task records/Channels remain, or while API calls are active.
- Runtime destroy succeeds after join and explicit resource/task/Channel destruction.
- Recreate a fresh runtime after teardown and verify IDs/queues/worker state are initialized independently.

### Stress and sanitizer coverage

- Repeat cancel-vs-completion and wakeup-vs-cancel races with randomized scheduling; assert invariants, not a fixed worker order.
- Run ThreadSanitizer and AddressSanitizer/UndefinedBehaviorSanitizer on the focused worker lifecycle tests.
- Include a deliberately slow native handler to prove cancellation is delayed rather than performed concurrently inside the handler.

## 6. Current implementation versus this specification

Implemented today (archive 303):

- Cooperative `flat_runtime_task_cancel()` for CREATED/RUNNABLE/WAITING tasks.
- Request-only `flat_runtime_task_cancel_request()` with idempotent publication and return values 0 (accepted), 1 (already terminal), -1 (invalid/unknown).
- Scheduler-owned cancellation cleanup for pending CREATED/RUNNABLE/WAITING requests and owner-safe cancellation of RUNNING tasks after `flat_vm_step()` returns.
- Completion/failure wins if the VM reaches a terminal outcome before the request is observed.
- VM cancellation through `flat_vm_cancel()` for READY/YIELDED VM handles.
- Cleanup of Channel transactions and task-held Channel references on cancellation.
- Wakeup of tasks waiting on a cancelled target.
- Runtime destruction guards while tasks/Channels/active runs remain.
- Only one thread may drive `flat_runtime_schedule()`; the request-only cancellation API and synchronized task-state queries may be called by other host threads while the runtime remains alive.

Still not implemented after archive 303:

- A dedicated worker-owned cancellation cleanup queue; the current single scheduler scans the task registry for pending requests.
- Pool lifecycle states, DRAIN/CANCEL shutdown, shutdown request API, or worker join.
- Worker condition-variable wait loop and pthread worker creation.
- Complete-VM multi-worker concurrency.

Archive 296 is specification and regression-baseline work only. It does not claim worker-mode cancellation or shutdown is implemented.
