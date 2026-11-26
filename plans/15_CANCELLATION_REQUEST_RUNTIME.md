# Request-only Cancellation Runtime — archive 303

**Baseline:** archive 302 (`flatL-main-302-runtime-ready-queue-channel-mutex.zip`)

**Scope:** implement request-only cancellation and scheduler-owned safe-point cleanup before adding pthread workers. This archive does not implement pool shutdown or worker threads.

## 1. Public API

```c
int flat_runtime_task_cancel_request(FlatRuntime *runtime, FlatTaskId id);
```

Return values:

- `0`: request accepted; repeated requests are idempotently accepted.
- `1`: task is already terminal; its result remains unchanged.
- `-1`: invalid runtime/ID or unknown task.

The API only sets `FlatTaskRecord.cancel_requested` under `scheduler_mutex`. It never calls `flat_vm_cancel()`, performs Channel rollback, releases VM resources, or changes the visible task state on the caller's thread.

The existing `flat_runtime_task_cancel()` remains a synchronous compatibility API for CREATED/RUNNABLE/WAITING tasks that are not dispatch-owned.

## 2. Ownership and safe points

- CREATED/RUNNABLE/WAITING requests are claimed by the cooperative scheduler before it selects ordinary runnable work. The scheduler sets `cleanup_owned`, removes any ready-queue entry, releases `scheduler_mutex`, and calls `flat_vm_cancel()`.
- A RUNNING task remains owned by the current dispatch turn. The request caller only sets the flag. After `flat_vm_step()` returns, the owner checks the flag and performs cancellation before releasing its dispatch token.
- A native handler is not interrupted. If it blocks, the request remains pending until the native call returns and the VM step reaches the scheduler boundary.
- The current implementation scans the task registry for unowned requests. This deliberately avoids introducing a second queue before the worker loop exists. A dedicated worker cancellation-cleanup queue remains a future worker-pool task.

## 3. Commit order

Only after `flat_vm_cancel()` succeeds does the runtime:

1. clear `waiting_on` and `waiting_channel_id`;
2. remove any ready-queue entry;
3. abort/rollback task-owned Channel transactions;
4. release task-held Channel references;
5. publish `FLAT_TASK_CANCELLED` and the cancellation result;
6. clear request/cleanup ownership flags;
7. wake tasks waiting on the cancelled task.

If the VM reaches COMPLETED or FAILED before the request is observed, that terminal outcome wins and the request flag is cleared without rewriting the result.

## 4. Validation

`flvm[40]` covers:

- CREATED: request is idempotent, visible state remains CREATED until scheduler cleanup, and start is rejected while cancellation is pending;
- RUNNABLE: scheduler claims cleanup before another bytecode step;
- WAITING: cleanup removes the outgoing wait edge and permits target destruction;
- RUNNING: a held DOM invocation gate blocks the VM inside a native call while the request API returns; after releasing the gate, the dispatch owner cancels at the safe boundary;
- unknown ID and already-terminal return semantics;
- terminal completion takes precedence over a later cancellation request.

Related regressions: `flvm[23]` (legacy synchronous cancellation), `flvm[38]` (dispatch ownership), and `flvm[39]` (Channel mutex concurrency).

## 5. Explicitly not implemented

- dedicated cancellation cleanup queue;
- worker-pool lifecycle (`RUNNING`, `DRAINING`, `CANCELLING`, `STOPPED`);
- DRAIN/CANCEL shutdown request and blocked-DRAIN reporting;
- worker condition wait, pthread worker creation or join;
- runtime destruction after worker join;
- complete-VM multi-worker concurrency.

The next milestone should implement the pool lifecycle and shutdown signaling, while preserving the no-lock-across-VM/native-call/join rule. Actual pthread workers remain gated on lifecycle and join tests.
