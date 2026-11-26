# Ready Queue Integration and Channel Mutex Conversion — archive 302

## Scope

Archive 302 connects the previously independent `FlatReadyQueue` to the cooperative `FlatRuntime` scheduler and replaces each Channel's atomic spin gate with `FlatMutex`. It does not create worker threads or change the public FlatL task/Channel language API.

## Runtime-owned ready queue

- `FlatRuntime` initializes and owns one `FlatReadyQueue`. Runnable task IDs are published when tasks are spawned or started, when a waiter wakes, and after a yielded quantum.
- `FlatTaskDispatchState` remains the duplicate-suppression and ownership authority. The queue contains IDs only; task records retain lifecycle state and dispatch tokens.
- The scheduler uses `flat_ready_queue_try_pop()` rather than blocking pop. If the queue is empty, it returns when no runnable task needs publication. The blocking `flat_ready_queue_pop()` remains the worker-loop primitive for a later archive.
- Queue pop and dispatch claim are performed under the same `scheduler_mutex` critical section as state transitions. This closes the stale-ID race where a queue item could be popped, the task could become WAITING and RUNNABLE again, and an old item could claim the new dispatch generation.
- Explicit Channel transaction owners keep priority. The scheduler scans for a runnable transaction owner, removes that ID from the FIFO, then claims it. Other runnable tasks follow FIFO order.
- Entering WAITING, cancellation cleanup and task destruction remove any queued ID. Wakeups while a worker owns a quantum are coalesced in `wake_pending`; release publishes exactly one queue entry. Terminal/WAITING outcomes discard pending wakeups.
- Queue publication failure clears the queued bit. A later scheduler pass searches for RUNNABLE, unowned, unpublished tasks and retries publication, avoiding silent loss of a runnable task.
- Queue close/drain/destroy is part of idle `flat_runtime_destroy()`. Host-side prevention of concurrent new API entries during runtime destruction remains required.

## Channel mutex conversion and lock order

`FlatChannel.gate` is now a `FlatMutex`. The mutex is initialized with the Channel and destroyed after storage/reference cleanup. The scheduler mutex is the outer lock for runtime-owned state. Under it, code may acquire either a per-Channel gate or the ready-queue mutex for one bounded operation; it must not hold a Channel gate and the queue mutex simultaneously. The runtime does not hold a Channel gate while acquiring the scheduler mutex. Channel unlock/commit releases the Channel gate before waking scheduler waiters. The cooperative scheduler holds `scheduler_mutex` while performing a nonblocking queue pop and claiming the task; the queue mutex is released by the pop operation before VM execution. `flat_vm_step()` runs outside the scheduler mutex and outside the ready-queue mutex.

A future worker may block in `flat_ready_queue_pop()` while holding only the queue's internal mutex. It must release that mutex before acquiring `scheduler_mutex`; it must not introduce a nested queue-to-scheduler lock order.

## Tests

- `flvm[37]` extends the ready-queue primitive regression with nonblocking empty pop and FIFO-preserving ID removal.
- `flvm[38]` exercises the runtime scheduler with concurrent task starts/queries and confirms single-driver exclusion and VM teardown outside the scheduler mutex.
- `flvm[39]` runs eight pthreads against one shared runtime Channel; each thread repeatedly sets and reads its own key (2,000 iterations per thread), validating serialized Channel access and independent VM pools.
- `flvm[15]`'s focused runtime lifecycle build now includes `flat_scheduler_sync.c`, because `FlatRuntime` owns the ready queue.

## Validation in archive 302

- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- `flvm[15]`, `[16-21]`, `[22-30]`, and `[31-39]` — PASS individually or in focused groups. The full `flvm` suite was not completed; grouped invocations hit the execution time limit, so no full-suite pass is claimed.
- `flvm[37]` ready-queue stress — PASS; ThreadSanitizer — PASS.
- `flvm[39]` Channel concurrency test — PASS; ThreadSanitizer — PASS (8 threads × 2,000 set/read iterations).
- `flvm[38]` runtime scheduler concurrency test under ThreadSanitizer — PASS on rerun (128 tasks, concurrent starts/queries, DOM teardown gate). An initial instrumented run ended at the test assertion; the diagnostic rerun satisfied every assertion and emitted no TSAN report.
- AddressSanitizer/UndefinedBehaviorSanitizer build of the Channel concurrency test exceeded the execution time limit; no ASan/UBSan result is claimed.
- The test groups `flvm[31-38]` passed before the final correction to task-spawn queue publication; `flvm[38]` was rerun after that correction and passed. Task/scheduler/Channel regressions `[22-30]` were also run individually after the correction.
- Only POSIX/Linux synchronization was exercised here. Windows and Emscripten remain unvalidated in this environment.

## Not implemented

- No pthread worker loop or worker pool.
- No worker shutdown/join, request-only cancellation, DRAIN/CANCEL implementation, or worker-owned cleanup queue.
- No claim that the complete VM/native runtime is ready for multi-worker execution.
