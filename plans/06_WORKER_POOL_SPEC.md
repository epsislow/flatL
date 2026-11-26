# FlatL v2.0 — Worker Pool Architecture Specification

**Checkpoint:** archive 291  
**Baseline inspected:** `flatL-main-290-cooperative-runtime-teardown-audit.zip`  
**Status:** architecture specification; no pthread worker implementation in this archive.

This document separates facts verified in the archive-290 source from the proposed worker-pool design. It is a design baseline for the next implementation phase, not a claim that the current runtime is thread-safe. Any change to the existing cooperative task, wait/retry, Channel transaction, or teardown contracts must still be recorded in `01_DECISIONS.md` and tested before implementation.

## 1. Goals and non-goals

### Goals

- Execute independent `FlatTask` VMs simultaneously on a bounded number of OS worker threads.
- Preserve the existing task IDs, task states, `task.wait()`, Channel wait/retry, transaction ownership, rollback, and `task.result()` semantics.
- Ensure one task's `FlatVm` is never stepped, cancelled, or destroyed concurrently by two threads.
- Make task registry, ready queue, wait graph, Channel registry, and resource lifetime transitions atomic and auditable.
- Define deterministic startup, cancellation, shutdown, and error behavior.
- Keep a working cooperative/single-thread mode for platforms without pthread support.

### Non-goals for the first worker-pool milestone

- Parallel execution of two quanta belonging to the same task.
- Preemption in the middle of an opcode or native handler.
- Automatic thread safety for arbitrary third-party native plugins.
- A UI/rendering thread or UI runtime. That remains the separate v2.5 phase.
- Forceful termination of a worker with `pthread_cancel()`.
- A guarantee that arbitrary process-global host APIs or user callbacks are safe to call concurrently.

## 2. Verified current ownership model (archive 290)

| Resource | Current owner / observed behavior | Worker-pool consequence |
|---|---|---|
| `FlatRuntime` | Owns host config, task list, Channel list, IDs, active-run count and scheduler cursor. | Runtime-wide mutable state needs one complete synchronization protocol. |
| `FlatTaskRecord` | Runtime-owned; stores ID, state, `FlatVm *`, result, `waiting_on`, `waiting_channel_id`. | A task record must remain alive while queued, running, waiting, or referenced by a dependency. |
| `FlatVm` / `VmState` | Each task owns a persistent VM. VM owns copied bytecode, host snapshot, VM pools, VCX, executor, IO/file state, RNG/date/env/path and continuation state. | Exclusive access by one worker while RUNNING; no concurrent step/cancel/destroy. |
| Spawn arguments | `flat_vm_set_initial_args()` deep-copies supported values into child VM pools; file handles are rejected. | Preserve deep-copy boundary; do not transfer parent VM pool pointers to another worker. |
| `FlatChannel` | Runtime-owned; has its own atomic gate, storage pools, transaction snapshot, logical transaction owner and task-user references. | Replace/encapsulate the current gate with a real mutex for parallel builds; logical transaction ownership remains separate. |
| Wait graph | One outgoing `waiting_on` edge per WAITING task; Channel wait additionally stores `waiting_channel_id`. | State transition, dependency registration/removal and ready-queue publication must be one synchronized operation. |
| Native bridge | `FlatTaskNativeBridge.user` points to the runtime; callbacks look up task/runtime state. | Every callback touching shared runtime state must use the same synchronization contract as host APIs. |
| Host configuration | Runtime config clones environment overrides and copies path separator; each VM snapshots it. | Treat runtime config as immutable after pool start; no worker reads mutable staging configuration. |
| Native registry | Process-wide handler table and module cache; registry/lifecycle spinlocks exist; handler invocation occurs after those locks are released. | Registry pointer publication is synchronized, but native handler internals are not thereby thread-safe. |
| Runtime logger | Process-wide logger with a spinlock around configuration, sequence, buffer and stdout write. | Configuration/shutdown must be frozen until workers join; shared output ordering is global. |
| Locale helper | Shared C numeric locale is initialized once and used thread-locally on POSIX. | Keep the immutable locale; do not reintroduce process-global `setlocale()`. |
| Environment staging | Process-global staging map is protected by a spinlock and copied into VM snapshots. | Snapshot before workers start; external mutation of process `environ` remains host responsibility. |
| Legacy path separator | Process-global atomic separator exists; per-VM `FlatPathContext` is also available. | Worker VM code must use its VM context; do not mutate legacy process staging during a pool run. |
| Memory stream fallback | Process-owned stream lookup list is protected by an atomic flag; stream contents/output pointers are caller-owned. | Registry lock protects lookup metadata only; each stream and its output targets remain single-owner. |
| WASM bridge | `flat_wasm.c` has process/instance-style globals for last result, limits and staging. | Do not enable pthread workers through this bridge until it has an explicit instance/context model and platform-specific design. |
| HTTP multipart boundary | `src/lib/mod_http.c::http_ensure_boundary()` increments a function-static `unsigned long seq` without synchronization. | Concrete process-global data-race candidate; fix or make request-local before concurrent native HTTP use. |

## 3. Current blockers confirmed in source

1. `runtime_lock()` is an atomic spin gate, but it is not applied consistently to task registry operations. `flat_runtime_task_start()`, `flat_runtime_task_wait()`, `flat_runtime_task_cancel()`, scheduler selection/state updates, state/result queries, and task destruction traverse or mutate task records without one shared lock protocol.
2. `runtime_wake_waiters()` and `runtime_wake_channel_waiters()` scan and mutate the task list without a synchronization boundary that also publishes work to a worker queue.
3. `flat_runtime_schedule()` is currently a single-thread scheduler. It selects a task, changes its state, steps the VM, and performs terminal cleanup as one host-thread operation. The current public task API explicitly confines scheduler/registry operations to that host thread.
4. The Channel `atomic_flag` gate protects an individual Channel's internal data in the current cooperative implementation. It is not a pthread mutex, does not protect the runtime Channel list, and does not make task/wait graph transitions atomic.
5. The `FlatVm` handle rejects stepping/cancelling/destroying while RUNNING, but there is no worker ownership token or asynchronous cancellation request protocol yet.
6. The runtime destroy contract requires the host to prevent new API calls racing with `flat_runtime_destroy()`. `active_runs` is not a general lifetime-safe entry gate.
7. Native registry locks protect handler registration/lookup and lazy module preparation. They do not serialize concurrent calls into a handler, nor do they certify plugin-owned static state as thread-safe.
8. `http_ensure_boundary()` contains an unsynchronized mutable static counter. Other native modules and dynamically loaded plugins still require a full mutable-static audit.

Therefore, the existing atomic gates are not sufficient grounds for enabling pthread workers.

## 4. Proposed runtime synchronization model

### 4.1 Runtime scheduler mutex

Introduce one real runtime scheduler mutex (pthread build) that protects, as a single protocol:

- task registry membership and task-record lifetime;
- task public state and internal dispatch ownership flags;
- ready/cancel work queues and queue condition signaling;
- `waiting_on`, `waiting_channel_id`, and wait-cycle checks;
- task/channel ID allocation;
- Channel registry membership and transient Channel references;
- active worker/run counts, pool lifecycle and shutdown flags;
- task-to-Channel user-reference bookkeeping where it affects destruction.

The existing atomic `FlatRuntime.lock` must not simply be assumed to provide this coverage. Replace it with an abstraction whose pthread implementation uses `pthread_mutex_t` and whose cooperative implementation can remain a no-op or a small platform primitive under the single-thread contract.

### 4.2 Per-Channel mutex

Each Channel receives a real mutex protecting its entries, snapshot, logical transaction fields, and task-user reference list. The logical transaction (`transaction_locked` / `transaction_owner`) is not a mutex held across VM instructions or worker turns. It remains an ownership protocol implemented by short critical sections under the Channel mutex.

### 4.3 Lock ordering

The only permitted nested order is:

1. runtime scheduler mutex;
2. Channel mutex.

Code holding a Channel mutex must never call back into code that acquires the runtime scheduler mutex. In particular, do not perform wakeup scans, task registry traversal, logging callbacks, or host callbacks while holding only a Channel mutex.

Where an operation can be performed without nested locks, prefer that. If a Channel pointer is used after releasing the scheduler mutex, acquire a transient reference while the registry lock is held and release it after the Channel mutex is released. Channel destruction must reject while transient references are nonzero.

Never hold the runtime scheduler mutex or a Channel mutex while:

- executing `flat_vm_step()`;
- invoking a native handler or plugin initializer;
- performing blocking file/network IO;
- calling user/host callbacks;
- waiting on a condition variable (the condition wait must atomically release the scheduler mutex).

The process-wide native plugin lifecycle lock is not to be nested with the runtime scheduler mutex. VM/native preparation happens outside the scheduler critical section.

## 5. Worker-pool scheduling model

### 5.1 Ownership

- The runtime owns the pool, worker records, queues, tasks and Channels.
- A worker owns a task's execution turn from dequeue/claim until it publishes the result of that VM step.
- A RUNNING task has exactly one worker owner.
- A task may move between workers between quanta; migration is allowed only after the previous worker has fully returned from `flat_vm_step()` and released its dispatch ownership.
- A task VM is never concurrently stepped, cancelled, or destroyed.
- Workers do not own task records; task records remain runtime-owned until explicit host destruction after terminal cleanup.

### 5.2 Queue and dispatch

Use a bounded or dynamically growing ready queue guarded by the scheduler mutex and a condition variable. Initial implementation should preserve round-robin fairness by carrying the existing monotone task ID/scheduling cursor semantics into queue selection; transaction-owner priority must remain intact so a multi-instruction Channel transaction is not starved by the quantum.

A worker performs:

1. lock scheduler mutex;
2. wait while queue empty and shutdown policy says continue waiting;
3. dequeue a task and atomically mark it RUNNING + worker-owned;
4. unlock scheduler mutex;
5. execute one `flat_vm_step()` quantum with no runtime/Channel lock held;
6. lock scheduler mutex;
7. reconcile the VM result with any WAITING/wakeup/cancellation request recorded by native callbacks or other workers;
8. perform or enqueue terminal cleanup and publish at most one next work item;
9. clear worker ownership, signal relevant condition variables, unlock.

A task must never be present twice in the ready queue. Queue membership and dispatch ownership are explicit internal state, not inferred only from the public `FlatTaskState` enum.

### 5.3 Wakeup racing with the end of a worker turn

A native `task.wait()` or Channel contention callback can mark the currently executing task WAITING before `flat_vm_step()` has returned. Another worker may resolve that dependency immediately. To prevent a lost or duplicate enqueue, add an internal `worker_owned`/`wake_pending` (or equivalent dispatch token) protocol:

- wakeup changes the logical dependency/state under the scheduler mutex;
- if the task is still worker-owned, record wake-pending and do not enqueue it yet;
- when the executing worker returns from the VM step, it observes the pending wake, relinquishes ownership and enqueues exactly once;
- if the worker has already relinquished ownership, wakeup enqueues it directly.

The test suite must force this interleaving with barriers/hooks in a test-only harness; sleeps are not an acceptable race test.

## 6. Task wait, Channel contention and cancellation

### 6.1 Wait graph

Preserve the current public dependency model:

- `waiting_on` remains the single outgoing dependency edge;
- `waiting_channel_id` remains Channel-specific metadata, not a second graph edge;
- wait-cycle detection, dependency registration, task state change and queue exclusion are atomic under the scheduler mutex;
- Channel unlock/terminal cleanup must update Channel ownership and wake eligible waiters in a way that cannot lose a wakeup between checking the owner and registering the waiter.

A Channel waiter still retries the original VM instruction. Wakeup does not imply that the retried operation will succeed. `revert` still does not wake waiters; commit/unlock and terminal rollback do.

### 6.2 Cancellation

The current cooperative API cancels only CREATED/RUNNABLE/WAITING tasks at safe points and rejects RUNNING tasks. A worker pool needs an explicit cancellation-request protocol rather than calling `flat_vm_cancel()` on a VM that may be executing.

Proposed behavior:

- cancellation request is recorded under the scheduler mutex;
- a RUNNING task observes it after `flat_vm_step()` returns at the next scheduler-visible safe boundary, normally the configured quantum boundary; a long native call delays it;
- a WAITING or queued task is scheduled for cleanup by a worker (or a dedicated serialized cleanup path) so no other thread destroys its VM concurrently;
- cancellation becomes publicly `CANCELLED` only after VM cancellation/teardown, Channel transaction rollback, Channel reference release and waiter wakeup have completed;
- cancellation request and terminal completion race through one state transition winner; the losing operation receives a stable already-terminal/already-cancelled result.

Whether the public C API remains synchronous or becomes request/await is an API decision to settle before implementation. Do not silently change current `flat_runtime_task_cancel()` semantics.

### 6.3 Task destruction

A task record can be destroyed only when terminal or CREATED, not worker-owned, not queued, and with no incoming waiters or outstanding native callback references. Destruction removes the record under the scheduler mutex, then destroys the VM outside the mutex only after exclusive ownership is established. Runtime destruction remains refusal-based; it does not force-stop workers or tasks.

## 7. Channel operations and transaction semantics

- Channel IDs and logical transaction owners remain runtime-local and task-ID-based; never use `pthread_t` as the logical owner.
- `channel.lock` establishes logical ownership while holding the Channel mutex briefly; it does not retain the pthread mutex across task turns.
- `set`, `var`, `revert`, `unlock`, terminal rollback and user-reference updates must be synchronized against Channel destruction.
- A task may own multiple logical Channel transactions, but it may not cooperatively suspend while owning any transaction. Preserve the archive-286 invariant.
- Commit releases logical ownership and wakes only waiters for that Channel. Terminal cleanup aborts every transaction owned by the task before resolving task waiters.
- Deep-copy boundaries between VM pools and Channel pools remain mandatory. File handles remain rejected for Channel/spawn transfer unless a separate explicit ownership-transfer design is approved.
- Host calls with owner ID zero retain their existing non-waiting behavior; contention returns busy/error rather than parking an OS thread.

## 8. Native modules and process-owned services

### 8.1 Native registry

The current native plugin host has process-wide runtime and registry spinlocks. Lazy preparation is serialized; handler lookup/publication is protected; native handlers are invoked after registry/lifecycle locks are released. This protects the registry data structures, not handler internals.

Archive 292 completes a source-level inventory of embedded native handlers and built-in optional plugin entry points; see `07_NATIVE_THREAD_SAFETY_AUDIT.md`. It also fixes the unsynchronized HTTP multipart sequence with an atomic process-wide allocator and adds a pthread regression for that allocator.

The worker-mode capability contract is now represented by `include/flat_native_capability.h` and `src/lib/native_capability.c` (archive 297):

- `THREAD_SAFE`: explicitly declared module; may execute concurrently in independent VM contexts only when all dependencies and host resources satisfy the same guarantee.
- `SERIALIZED`: module calls must be serialized by one process-wide per-module invocation gate held only around the handler call.
- `UNSUPPORTED`: module is rejected in worker mode.
- `PENDING_THREAD_SAFETY_VALIDATION`: audit status, not a capability value; the effective worker policy is `SERIALIZED`.
- Unknown third-party plugins default to `UNSUPPORTED`. Existing `flat_native_plugin_init(reg)` remains compatible; plugins may additionally export `flat_native_plugin_thread_capability()`.
- `flat_native_runtime_worker_policy()` performs module preparation and admission preflight, rejects unsupported modules, and returns the effective `THREAD_SAFE`/`SERIALIZED` policy.
- Capability declarations are process-wide setup state and must be frozen before worker creation.

Remaining requirements:

- implement per-module mutex gates and integrate preflight into the worker dispatcher;
- ensure DOM serialization covers Lexbor-backed VM teardown as well as native handlers;
- verify external dependency concurrency guarantees (Lexbor, PCRE2, libcurl and supported platform C runtimes);
- never hold registry/runtime/scheduler/Channel locks while invoking plugin code;
- freeze plugin search path and prohibit plugin load/unload while any VM can call a handler.

### 8.2 Environment, path and locale

- Use immutable `FlatHostConfig` snapshots for worker VMs.
- Capture process environment and staging settings before pool start; do not mutate process `environ` or legacy staging while workers execute.
- Use per-VM `FlatEnvContext` and `FlatPathContext`; legacy process staging setters are setup-only.
- Keep numeric locale handling thread-scoped and the shared locale immutable. Do not call process-global `setlocale()` from VM execution.

### 8.3 Logging and output

The runtime logger currently serializes its global sequence, buffer and stdout writes with a process-wide spinlock. Keep logger configuration and shutdown outside the active-pool lifetime; flush only after workers stop or through a synchronized logger API.

Normal VM stdout/stderr and trace output are not a per-task ordered event stream. Concurrent writes may interleave. Before claiming deterministic output, add a per-runtime/per-task output sink or document that terminal output is byte-safe but interleaving is unspecified. Never invoke a user output callback while holding scheduler/Channel locks.

### 8.4 Other process globals

- The fallback memory-stream registry protects its lookup list, not individual `FILE *` streams or caller-owned output pointers.
- Legacy path separator is atomic, but workers should use VM path snapshots.
- WASM bridge globals (`g_last_termination`, panic buffer, run limits and staging) remain a separate singleton limitation; no worker-pool support is implied for WASM.
- The source-level mutable-state inventory is in `07_NATIVE_THREAD_SAFETY_AUDIT.md`. The HTTP multipart sequence is now atomic. External library internals and arbitrary third-party plugin state remain outside the source inventory and require explicit validation/capability declarations.

## 9. Worker lifecycle and shutdown

### Startup

1. Host creates/configures the runtime and immutable host snapshot.
2. Native plugin search path and logger/output policy are frozen/configured.
3. Worker pool initializes all mutexes/condition variables and creates the requested workers.
4. If any required worker creation fails, startup rolls back by signaling already-created workers, joining them, destroying synchronization objects, and returning failure. Do not publish a partially initialized pool unless partial capacity is an explicitly documented policy.
5. Only after successful startup may tasks be dispatched.

### Shutdown

Define two explicit modes before implementation:

- **Drain:** stop accepting new tasks, continue scheduling queued/runnable tasks, wait for active turns and waiting dependencies to resolve; return busy/timeout if unresolved WAITING tasks remain.
- **Cancel:** stop accepting new tasks, request cancellation for CREATED/RUNNABLE/WAITING/RUNNING tasks, let workers reach safe points, perform rollback/reference cleanup, then join all workers.

In both modes:

- set shutdown state under scheduler mutex and broadcast the worker condition;
- never call `pthread_cancel()`;
- join every created worker before destroying queues, mutexes, Channels, task records, logger or runtime host config;
- do not destroy the runtime while a worker is active or a task is still registered;
- repeated shutdown/destroy calls must have documented idempotence/error behavior.

## 10. Worker and task failure model

- A VM logical `FAIL`, panic, native error or step error affects that task only; it transitions to FAILED after its VM step returns and cleanup is complete.
- A worker infrastructure error is distinct from a task VM failure and must be recorded at runtime/pool level.
- If a worker cannot continue its loop, it must not silently abandon a RUNNING task. The implementation must either transfer a task only from a known safe point or mark the affected task FAILED with a diagnostic after exclusive ownership is recovered.
- Do not promise recovery from process-fatal native plugin faults, memory corruption, segmentation faults, or forced OS thread termination.
- A native handler is non-preemptible for the duration of the call. The instruction quantum bounds VM dispatch between opcodes, not the wall-clock duration of a blocking native call.

## 11. Platform strategy

- pthread-capable native builds: worker pool behind an explicit build/runtime capability.
- builds without pthread: preserve the cooperative scheduler and same task/Channel semantics; worker-count greater than one must fail clearly or be unavailable, not silently pretend to parallelize.
- WASM: remain cooperative until the bridge is instance-scoped and browser worker/shared-memory constraints are separately specified and tested.
- Windows support needs a platform abstraction equivalent to mutex, condition variable, thread create/join; do not make pthread types leak into public headers.

## 12. Acceptance gates before implementation

1. [x] Source-level mutable-state inventory for embedded native modules and built-in optional plugin entry points (`07_NATIVE_THREAD_SAFETY_AUDIT.md`). External dependency internals remain to be verified.
2. [x] Replace the HTTP multipart static sequence with an atomic allocator and add a pthread uniqueness regression (`flvm[31]`).
3. [ ] Complete the runtime lock/refcount/queue protocol. Archive 300 replaces the runtime atomic gate and synchronizes task registry/wait graph, but the MPMC queue is not connected and Channel still has its atomic gate.
4. [x] Add task `worker_owned`/dispatch token and wake-pending semantics; prove no duplicate queue entries or lost wakeups in the dispatch state machine. Archive 300 embeds the state in runtime task records and exercises cooperative claim/release; real multi-worker proof remains pending.
5. [ ] Define cancellation request and cancellation/completion race semantics without violating safe VM ownership.
6. [ ] Implement and test runtime/Channel lock order; no lock is held across VM step, native handler, blocking IO or callback.
7. [x] Define plugin capability states and the default rejection policy for unknown third-party plugins. [ ] Implement metadata/enforcement and output interleaving policy.
8. [ ] Define drain/cancel shutdown behavior, startup rollback, join and teardown ordering.
9. [ ] Add deterministic multi-worker tests using barriers, not timing sleeps: independent CPU tasks, same-task exclusivity, spawn/wait, Channel writers, commit/revert, cancellation, failure, worker startup failure, shutdown and runtime teardown.
10. [ ] Run the complete existing suite plus new stress tests on supported native platforms; run sanitizer builds (at minimum ThreadSanitizer where supported, plus AddressSanitizer/UndefinedBehaviorSanitizer).

**Exit rule:** no pthread implementation is considered ready until all acceptance gates are either implemented and tested or explicitly deferred with a documented limitation. Archive 292 completes only the native source inventory and HTTP sequence race fix; it does not authorize worker execution.


## 13. External dependency preflight (archive 293)

Before worker creation, native runtime startup must call `flat_http_prepare()` on the host thread. It initializes libcurl global state before any FlatL worker can enter the HTTP module. Do not defer this to the first HTTP request. Each request continues to own a distinct easy handle and request-local callback data; every handle sets `CURLOPT_NOSIGNAL=1`. The supported deployment must validate libcurl thread-safety features and resolver backend.

The bundled PCRE2 10.49 build has JIT disabled; FlatL creates independent pattern/match-data objects per call. This is a strong worker-safe candidate, not an enablement declaration until full VM tests pass.

The bundled Lexbor 3.0.0 DOM implementation uses per-VM document runtime state, but upstream documentation reviewed does not state a general thread-safety guarantee. DOM's audit status is `PENDING_THREAD_SAFETY_VALIDATION`, not `UNSUPPORTED`. First attempt to qualify `THREAD_SAFE` with concurrent complete-VM independent-document tests and sanitizer runs. If this cannot be certified, prefer `SERIALIZED`: one process-wide DOM gate must cover every Lexbor entry point, including `flat_dom_runtime_destroy()` during VM teardown; a handler-only gate is insufficient. Reserve `UNSUPPORTED` for a demonstrated uncontainable constraint. `lexbor_memory_setup()` is process-global and must not be called while workers run.

See `07_NATIVE_THREAD_SAFETY_AUDIT.md` and `08_EXTERNAL_DEPENDENCY_AUDIT.md` for source findings and references.


### 13.1 Lexbor/DOM capability and fallback (archive 294)

DOM remains `PENDING_THREAD_SAFETY_VALIDATION`; pending is not a fourth capability value and does not mean the module is permanently excluded. Acceptance first attempts `THREAD_SAFE` using complete-VM tests with independent `FlatDomRuntime`/document ownership, supported bundled/system Lexbor variants, and sanitizer runs.

If independent concurrent use cannot be certified, use `SERIALIZED` with one process-wide DOM gate. The gate covers all Lexbor calls, not just native `dom.*` handlers: `flat_dom_runtime_destroy()` iterates documents and calls `lxb_html_document_destroy()` during VM teardown, so this path must acquire the same gate. `flat_dom_runtime_create()` currently only allocates the FlatL wrapper, but any future Lexbor initialization added there must also be covered. The gate is acquired only around a bounded DOM handler or DOM teardown operation; never hold it across VM execution, task suspension, blocking I/O or host callbacks. Scheduler and Channel locks must be released before acquiring it.

Use `UNSUPPORTED` only if a specific Lexbor/build/host constraint cannot be made safe through independent contexts or the serialized gate. Archive 294 does not implement the gate or enable DOM in worker mode.


## Lexbor evidence and worker-mode gate (archive 295)

The bundled Lexbor 3.0.0 amalgamation passed a focused independent-context pthread test: eight threads each created, parsed, selected, mutated, serialized and destroyed their own documents and parser/selector objects (2,000 document lifecycles total). The same test passed ThreadSanitizer and AddressSanitizer/UndefinedBehaviorSanitizer in the validation environment. `mod_dom.c` has no mutable file-scope data declarations, and FlatL does not call `lexbor_memory_setup()`.

This evidence is limited to direct bundled-Lexbor context isolation. It is not proof that concurrent `FlatRuntime`/`FlatVm` invocations are safe, and does not cover the system-Lexbor build. Therefore DOM remains `PENDING_THREAD_SAFETY_VALIDATION` for worker mode. After the runtime scheduler and lifetime protocol are implemented, add complete-VM tests before selecting `THREAD_SAFE`. If those tests or a supported build variant cannot be certified, use the previously specified process-wide `SERIALIZED` gate around every Lexbor call, including DOM teardown. No gate is implemented in archive 295.

## 13. Cancellation request and shutdown amendment (archive 296)

The detailed contract and future acceptance matrix are in [`09_CANCELLATION_SHUTDOWN_SPEC.md`](09_CANCELLATION_SHUTDOWN_SPEC.md). Key rules:

- Worker mode uses a request-only cancellation API. The caller never destroys a running VM or performs task cleanup.
- The task-owning worker observes the request at the next scheduler-visible safe boundary after `flat_vm_step()` returns (normally the configured quantum boundary); a long-running native call delays that boundary. Unowned CREATED/RUNNABLE/WAITING tasks are claimed as cleanup jobs.
- `CANCELLED` is published only after Channel rollback, wait/reference cleanup, VM cleanup, and dependent wakeup publication.
- DRAIN rejects new submissions but completes already-started work; CREATED tasks are not auto-started. An unresolved quiescent wait graph is a blocked drain.
- CANCEL rejects submissions, requests cleanup for all nonterminal tasks, and keeps workers alive until cleanup jobs complete. It may escalate DRAIN but cannot be downgraded.
- Worker join precedes all runtime/VM destruction. `flat_runtime_destroy()` remains the final host-owned step after join and explicit task/Channel destruction.

These rules are specification only in archive 296. The current cooperative `flat_runtime_task_cancel()` is unchanged; no worker or asynchronous cancellation implementation exists.


## 14. Native module capability metadata (archive 297)

Capability declarations are module-level. The plugin initializer ABI remains unchanged and the optional `flat_native_plugin_thread_capability()` symbol carries the declaration. Legacy or invalid plugin metadata is denied in worker mode. `flat_native_runtime_worker_policy()` is the future scheduler's preflight and returns the effective policy. `PENDING` maps to `SERIALIZED`; DOM's eventual serialized gate must include VM teardown. Archive 297 does not yet implement module gates or worker-dispatch integration.


## Archive 298 implementation update

The common native invocation boundary now applies the module policy before returning to VM execution. `SERIALIZED`/effective `PENDING` calls use an independent per-module gate; `THREAD_SAFE` calls are ungated. The gate implementation is backed by POSIX pthread mutexes, Windows SRWLOCK, and a no-op single-thread Emscripten implementation. DOM VM teardown acquires the same DOM gate as DOM handlers. This closes the native invocation serialization boundary but does not implement worker admission in a scheduler, scheduler/Channel locks, condition variables, task ownership, cancellation, shutdown or worker creation. See `10_NATIVE_SYNC_GATES.md`.

## 11. Scheduler primitive implementation checkpoint (archive 299)

The following low-level pieces now exist independently of `FlatRuntime`:

- `FlatMutex`, `FlatCond`, and `FlatThread` wrappers in `include/flat_sync.h`;
- `FlatReadyQueue`, an MPMC FIFO of task IDs with close/drain semantics;
- `FlatTaskDispatchState`, which coalesces duplicate enqueue/wakeup events and prevents a task from being queued while worker-owned.

The dispatch state must be protected by the future scheduler mutex. Queue operations use their own mutex and must not be nested with scheduler locking. Worker pop returns an ID, not a pointer; the runtime must revalidate/claim it under scheduler lock. Wakeup while worker-owned sets `wake_pending`; owner release validates its dispatch token and asks the caller to publish one requeue outside the scheduler critical section.

These primitives are tested in `flvm[37]`, but they are not yet connected to the runtime. The current `FlatRuntime` atomic gate, task registry, wait graph, Channel registry, cancellation API and teardown remain unchanged and are not thread-safe for concurrent host calls. No VM worker threads are started in archive 299.


## 15. Runtime scheduler mutex integration (archive 300)

Archive 300 replaces the `FlatRuntime` atomic spin gate with `FlatMutex scheduler_mutex`, protects task registry/state/wait-graph transitions, and embeds `FlatTaskDispatchState` in each `FlatTaskRecord`. The scheduler claims/releases dispatch ownership under the mutex and always executes `flat_vm_step()` outside it. Concurrent task queries and starts are tested while the one cooperative scheduler driver is active; a second scheduler call is rejected. Cancellation/destruction is rejected while dispatch ownership remains active, including a transient WAITING state before the current VM step returns.

This does not yet satisfy the full worker acceptance gate: the runtime remains single-driver and scan-based, the MPMC ready queue is not connected, Channel still uses its existing atomic gate, and no worker thread/condition wait, request-only cancellation, shutdown/join, or complete-VM stress exists. See [`12_RUNTIME_SCHEDULER_MUTEX.md`](12_RUNTIME_SCHEDULER_MUTEX.md).

## Archive 302 runtime synchronization update

Archive 302 closes the earlier queue/Channel gaps: `FlatRuntime` now owns and consumes the MPMC ready queue, and `FlatChannel` uses the cross-platform `FlatMutex`. Queue pop plus dispatch claim is serialized under `scheduler_mutex`; VM stepping remains outside it. The blocking queue pop is retained for future workers, while the current cooperative scheduler uses nonblocking pop. These changes improve the runtime's synchronization foundation but do not satisfy worker admission: there is still one scheduler driver, no worker lifecycle/shutdown/join, no request-only cancellation, and no full-VM multi-worker validation. See [`14_READY_QUEUE_CHANNEL_MUTEX_INTEGRATION.md`](14_READY_QUEUE_CHANNEL_MUTEX_INTEGRATION.md).


## 8. Archive 305 implementation status

Archive 305 implements the first fixed runtime-owned worker pool. It uses the existing ready queue and task dispatch ownership, has a startup barrier/rollback, waits on the queue condition variable with wake epochs, integrates DRAIN/CANCEL exit and exposes explicit join. This closes the initial worker creation/loop/join milestone but not the full acceptance matrix above. Task.wait/Channel contention, HTTP/DOM/plugin dependency stress, startup failure injection, Windows validation and full-VM sanitizer coverage remain open. Native capability enforcement currently occurs at invocation time rather than scanning all task bytecode before dispatch.
