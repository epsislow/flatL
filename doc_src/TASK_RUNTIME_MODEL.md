# Task Runtime — persistent VM and cooperative task model

**Status:** normative design and implementation record. The opaque `FlatVm` supports instruction-budget yield/resume; archives 276–279 add the cooperative scheduler and FlatL task natives; archives 280–281 add runtime Channels and FlatL Channel natives; archive 282 adds wait-cycle rejection, task-held Channel handle references, and Channel event logging; archive 283 adds scheduler-aware Channel contention, task result summaries, and runtime stress coverage; archive 284 adds explicit owner-failure/cancellation wakeup regression coverage and records the retry contract; archive 285 audits task destruction against wait dependencies and Channel references; archives 305–327 add the runtime-owned worker pool, platform synchronization and focused worker stress coverage. Archive 351 adds the one-shot cooperative runner used by FLEditor WASM so Task/Channel natives execute through a runtime-owned root task in the browser.

## 1. Runtime, VM, and task are different objects

- `FlatRuntime` is a host-owned lifetime boundary. It owns its immutable `FlatHostConfig` snapshot, task registry, monotonic task-ID allocator, and scheduler cursor.
- `FlatVm` is one persistent bytecode execution instance. It owns the mutable execution state required to continue at the next instruction: `VmState`, instruction pointer, guard scratch storage, jump counters, loop/call frames, and VM-owned native resources.
- `FlatTask` is the scheduler-visible unit. It owns one `FlatVm`, a runtime-local numeric task ID, lifecycle state, result/diagnostic metadata, and scheduling/wait metadata. A task is not a FlatL `call`, `func`, or `def`.
- Cooperative scheduling runs on one host thread. Supported native hosts may instead enable the runtime-owned worker pool; that is a separate mode with platform synchronization and native-module capability gates. FLEditor WASM deliberately uses cooperative mode and does not start OS threads or browser Web Workers.

The ownership direction is `FlatRuntime -> FlatTask -> FlatVm -> VmState/resources`. Bytecode is copied into the persistent VM at creation; the caller may free or modify its original `FlatBytecode` after successful VM creation. Process services (native plugin registry, loaded shared libraries, logger, locale objects, libcurl global initialization, and the fallback memory-stream registry) remain process-owned.

## 2. Lifecycle state machines

### 2.1 Task states

The scheduler-visible task states are:

| State | Meaning |
|---|---|
| `CREATED` | Task record and VM have been allocated but execution has not started. |
| `RUNNABLE` | Task may be selected by the scheduler. |
| `RUNNING` | The scheduler has claimed this task for the current VM turn. In the current runtime only one scheduler-driving thread is allowed. |
| `WAITING` | Task is suspended on a scheduler dependency, initially another task's completion. |
| `COMPLETED` | VM terminated with `OK` or normal end-of-program completion. |
| `FAILED` | VM terminated with `FAIL`, unhandled panic, malformed bytecode, or runtime error. The exact termination code remains queryable. |
| `CANCELLED` | A cancellation request was observed at a safe point and task-owned resources were cleaned up. |

Allowed transitions:

```text
CREATED -> RUNNABLE -> RUNNING
RUNNING -> RUNNABLE       (instruction budget exhausted / cooperative yield)
RUNNING -> WAITING        (task.wait dependency not yet complete)
WAITING -> RUNNABLE       (dependency completed or failed)
RUNNING -> COMPLETED
RUNNING -> FAILED
CREATED -> CANCELLED
RUNNABLE -> CANCELLED
RUNNING -> CANCELLED     (request observed at a safe point)
WAITING -> CANCELLED
```

Terminal states (`COMPLETED`, `FAILED`, `CANCELLED`) never transition back to runnable. A task ID is not reused during one runtime's lifetime. ID zero is invalid.

`OK` and normal bytecode exhaustion map to `COMPLETED`. Logical `FAIL` maps to `FAILED` (with `FLAT_TERM_FAIL` retained as the result); unhandled `PANIC` maps to `FAILED` with `FLAT_TERM_PANIC`; malformed bytecode and VM errors map to `FAILED` with `FLAT_TERM_ERROR`. A caught panic that successfully transfers control to a panic handler does not itself fail the task.

### 2.2 VM internal states

The VM's internal execution states are `NEW`, `READY`, `RUNNING`, `YIELDED`, and `TERMINAL`. These are not exposed as task states. `WAITING` belongs to the scheduler/task layer; the VM does not wait on OS primitives.

A VM can be stepped repeatedly while `READY` or `YIELDED`. A terminal VM cannot resume. A VM is destroyed exactly once, either after task termination or explicit destruction before it has started.

## 3. Cooperative yield and resume

A normal quantum yield or `task.wait()` suspension occurs only after a completed bytecode-instruction boundary. The saved instruction pointer then identifies the **next** instruction to execute, never the instruction that has already completed. Archive 283 adds one narrowly-scoped exception for retryable Channel natives: when `channel.lock`, `channel.set`, or `channel.var` is blocked by another task's transaction, the native operation has not completed, so the VM keeps IP on the current `OP_NATIVE` and retries it after wakeup. Such a native must not commit side effects or bind a provisional output before it reports retry.

At a yield boundary, all of the following must remain intact:

- instruction pointer and bytecode identity;
- call stack and return positions;
- `ForeachFrame` / `for` loop stack and loop indices;
- executor frames, unique/global scope bookkeeping, and call-instruction stack;
- VCX and variable bindings;
- RNG and date run state;
- IO/file handles, CSV/DOM/HTTP runtime state, collection/string pools, and reference counts;
- caught-panic metadata and the current panic-handler control-flow state;
- jump/step counters and per-block jump budgets.

A resume starts at the saved next instruction and must not re-run the previous opcode or duplicate its side effects.

The instruction budget is a cooperative scheduling quantum, not a hard wall-clock limit. One opcode may execute nested work or a native function before reaching the next safe point. Therefore a long-running or blocking native call can delay every other cooperative task. The first scheduler does not preempt native calls. Native operations that need to become asynchronously waitable require a later continuation/polling contract; they must not pretend that a C stack frame can be suspended.

The initial implementation should support budget-based yield first. Explicit `task.yield()` and task waiting should be added only after the bytecode-boundary mechanism is proven. No OS thread is created by yield/resume.

## 4. Bytecode and options ownership

A persistent VM must not retain a pointer to caller-owned bytecode. Creation makes an owned bytecode copy using the canonical FlatBytecode loader/parser path (or an equivalent deep clone that reconstructs every derived section pointer). The VM frees that copy during destruction.

A persistent VM must not retain `FlatVmOptions *` or caller output-buffer pointers. It copies scalar execution options (`trace`, `trace_vars`, `max_steps`, `max_jumps`, `no_jump_limit`) into VM-owned configuration. Result/termination/diagnostic data is returned through a value result or queried from the VM/task; callers may not supply a buffer whose lifetime extends implicitly across yields.

The runtime must outlive all VMs/tasks created from it. `flat_runtime_destroy()` returns busy while any VM/task remains registered, not merely while a synchronous `flat_runtime_run()` call is on the C stack. The host must prevent new API calls from racing with runtime destruction, as in the existing runtime API contract.

## 5. Panic, failure, cancellation, and cleanup

- Existing block panic handlers run inside the same VM and task. A caught panic may change the instruction pointer and continue normally.
- An unhandled panic terminates only its task as `FAILED`; it does not terminate the runtime or other tasks.
- `FAIL` is a logical task failure, not a scheduler-wide failure.
- `flat_runtime_task_cancel()` remains synchronous and accepts only `CREATED`, `RUNNABLE`, or `WAITING` tasks that are no longer dispatch-owned. `flat_runtime_task_cancel_request()` is request-only: it sets an idempotent flag under `scheduler_mutex` and returns without touching VM state. It accepts CREATED/RUNNABLE/RUNNING/WAITING tasks; returns 1 for an already-terminal task and -1 for an invalid/unknown ID or a nonterminal task whose cleanup is already owned by a non-cancellation operation. A scheduler-owned cancellation already in progress remains idempotent. The scheduler claims cleanup for unowned requests and the current dispatch owner handles a RUNNING request after `flat_vm_step()` returns.
- Cancellation performs deterministic VM/resource teardown and marks the task `CANCELLED` only after cleanup. VM teardown runs outside the runtime scheduler mutex because it may acquire serialized native-module gates. There is no implicit user-level `finally`/defer execution in this milestone.
- Cleanup preserves the primary termination reason. A secondary cleanup error is attached as supplemental diagnostic and must not overwrite the original panic/error.
- Destroying a running VM/task is rejected as busy. A never-started or terminal task may be destroyed; destruction releases its VM, bytecode copy, VCX, executor scopes, handles, pools, and task metadata exactly once.
- A task with incoming wait dependencies cannot be destroyed. The host must allow those dependencies to resolve or cancel the waiters first. A WAITING task itself must be cancelled before it can be destroyed.
- Cancelling a waiter clears its outgoing task/Channel dependency and releases its task-held Channel references. A Channel waiter that has been woken is RUNNABLE, not destroyable; its Channel reference remains until it completes, fails, or is cancelled.
- Destroying a CREATED task that owns an open Channel transaction aborts/rolls back that transaction before freeing the task. This is allowed only after all waiters targeting that owner have been resolved or cancelled.

When Channels/transactions are added, task termination, failure, and cancellation must automatically revert and release any transaction owned by that task before its resources are freed.

## 6. Wait semantics

`task.wait(id)` is scheduler-level waiting, not a blocking OS wait. If the target is terminal, the call returns immediately with its result available. If the target is not terminal, the current task transitions to `WAITING`, records the target task ID, and yields to the scheduler. When the target reaches any terminal state, the waiter becomes `RUNNABLE`.

Waiting on an unknown ID, self-wait, or a task owned by another runtime is an error. Waiting on an already completed/failed/cancelled task is valid and does not suspend. Before adding a wait edge, the runtime follows the target's existing wait chain; if it reaches the waiter, the new edge is rejected. Each task has at most one outgoing wait edge, so this detection requires no temporary allocation.

At the initial C-scheduler specification stage, the exact FlatL native syntax and return-value convention were still open. Archive 278 now defines the first native surface; see `natives/task.md`.

## 7. Public C API and implementation status

The first public VM-owner API is now present in `include/vm.h` (also reachable through `include/flat_vm.h`):

```c
typedef struct FlatVm FlatVm;
FlatVm *flat_vm_create(const FlatBytecode *bc, const FlatVmOptions *opts);
int flat_vm_step(FlatVm *vm, uint64_t instruction_budget, FlatVmResult *result);
int flat_vm_execute(FlatVm *vm, FlatVmResult *result);
int flat_vm_destroy(FlatVm *vm);
FlatVmState flat_vm_state(const FlatVm *vm);
int flat_vm_is_terminal(const FlatVm *vm);
```

`flat_vm_create()` deep-copies bytecode through `flat_fb_load_buffer()`, copies scalar execution options, snapshots the host configuration, and creates the initialized `VmState` owned by the handle. The caller may therefore free the original bytecode, options, and host config after successful creation. `flat_vm_step()` executes at most `instruction_budget` bytecode dispatches. A zero budget means run until terminal. When the budget is exhausted, it returns with state `FLAT_VM_YIELDED`; the VM retains all resources and its IP already points at the next instruction. Call `flat_vm_step()` again to resume. The step call returns `0` when it yields or completes successfully and a non-zero VM exit code on terminal failure; inspect `FlatVmResult.state` and `.termination` to distinguish these outcomes. Invalid, running, or terminal handles are rejected with `-1`.

`flat_vm_execute()` remains the compatibility run-to-completion helper and is equivalent to an unbounded step from `READY`. `flat_vm_state()` distinguishes `READY`, `RUNNING`, `YIELDED`, `COMPLETED`, and `FAILED`; `flat_vm_is_terminal()` recognizes either terminal outcome. `FLAT_VM_TERMINAL` remains as a compatibility alias for successful completion. `flat_vm_destroy()` releases the owned execution state, bytecode, and configuration and rejects a running handle. `flat_vm_run_opts()` continues to use the same VM state machinery and preserves its historical one-shot behavior.

The opcode budget is checked at the top of the dispatch loop, after the preceding opcode has completed and updated the saved IP. Thus a yield never suspends a C stack frame inside an opcode or native call. A long-running native call is not preempted. The existing `max_steps` option remains a cumulative VM execution limit and is independent of the per-call instruction budget.

The initial C scheduler owns task records and persistent `FlatVm` handles. Archive 277 adds wait/wakeup and cancellation; archive 278 adds the first FlatL task natives. Continue to avoid exposing internal `VmState`, `ip`, VCX pointers, or frame structures.

## 8. Initial C task and scheduler API

Archive 276 adds `include/flat_task.h`. The first implementation deliberately exposes task IDs and query functions rather than a mutable public task structure:

```c
typedef uint64_t FlatTaskId;

typedef enum FlatTaskState {
    FLAT_TASK_CREATED,
    FLAT_TASK_RUNNABLE,
    FLAT_TASK_RUNNING,
    FLAT_TASK_WAITING,
    FLAT_TASK_COMPLETED,
    FLAT_TASK_FAILED,
    FLAT_TASK_CANCELLED
} FlatTaskState;

int flat_runtime_task_create(FlatRuntime *, const FlatBytecode *,
                             const FlatVmOptions *, FlatTaskId *out_id);
int flat_runtime_task_start(FlatRuntime *, FlatTaskId);
int flat_runtime_schedule(FlatRuntime *, uint64_t quantum,
                          uint64_t max_quanta, uint64_t *out_quanta);
FlatTaskState flat_runtime_task_state(const FlatRuntime *, FlatTaskId);
int flat_runtime_task_result(const FlatRuntime *, FlatTaskId, FlatVmResult *);
int flat_runtime_task_destroy(FlatRuntime *, FlatTaskId);
```

A newly created task is `CREATED`; `flat_runtime_task_start()` makes it `RUNNABLE`. The scheduler selects runnable tasks in round-robin task-ID order and calls `flat_vm_step()` with the supplied instruction quantum. A yielded VM returns to `RUNNABLE`; a successful terminal VM becomes `COMPLETED`; a non-zero VM result becomes `FAILED`. One task's failure does not stop the scheduler from running other runnable tasks.

`max_quanta` bounds the number of scheduler turns in one call. Zero means continue until no task is runnable. The scheduler itself remains synchronous and single-driver. `quantum` must be non-zero. Runtime task registry/state operations are protected by the runtime scheduler mutex, and host threads may query task state or start CREATED tasks while the scheduler is running. Only one thread may call `flat_runtime_schedule()` at a time; an overlapping call is rejected. `flat_vm_step()` always runs outside the scheduler mutex. No worker thread is created by this API.

Task IDs are runtime-local, start at one, and are never reused. The runtime owns every task record and its `FlatVm`; a task may be destroyed only while `CREATED` or after a terminal state and when it is not dispatch-owned. `flat_runtime_destroy()` returns busy while any task record remains, including terminal tasks, so the host must explicitly destroy tasks before destroying their runtime. Task registry/state APIs use the runtime scheduler mutex. The host must still prevent new API calls from racing with `flat_runtime_destroy()` itself; this mutex does not protect the lifetime of a `FlatRuntime` allocation after it is freed.

`flat_runtime_task_result()` is available only after a terminal state. It copies the VM result by value, including exit code, termination, VM state, and diagnostic. A failed task is a task-local result, not a scheduler-wide error.

## 9. Implementation sequence and acceptance tests

1. Keep `VmState` at a stable heap address for an invocation; implemented in archive 268.
2. Keep instruction pointer, entry-block index, and guard scratch arrays inside `VmState`; implemented in archive 269. Step/jump counters and call/loop frames were already members of `VmState`.
3. [x] Move the complete initialized `VmState` and its owned resources into the heap-stable opaque `FlatVm` object, without changing one-shot behavior. The handle must not be `realloc()`-moved after VCX/executor pointers are bound (archive 272).
4. [x] Refactor the opcode loop to yield at the next dispatch boundary when the per-call instruction budget is exhausted; preserve `flat_vm_run_opts()` as an unbounded compatibility wrapper (archive 273).
5. [x] Add a basic opcode-budget test proving repeated step calls reach terminal completion (archive 273).
6. [x] Add a combined resume-equivalence test covering nested `call`, `for`, `foreach`, unique executor scopes with nested VM calls, and a caught panic-handler jump (archive 274). The test compares terminal exit/termination against run-to-completion for the same bytecode.
7. [x] Verify creation-time `out_termination` and `out_panic_msg` pointers are not retained by a stepped VM; archive 274 mutates both caller buffers after `flat_vm_create()` and confirms they remain unchanged through yield/resume and completion.
8. [x] Add the initial runtime-owned task registry, monotonic task IDs, CREATED/RUNNABLE/RUNNING/COMPLETED/FAILED transitions, round-robin scheduling, result queries, and explicit task destruction (archive 276).
9. [x] Add an integration test with two successful tasks and one failing task, quantum-one interleaving, failure isolation, runtime-busy behavior, and non-reused IDs (archive 276).
10. [x] Add C-level wait/wakeup and cancellation-at-safe-point semantics, including terminal-target immediate wake, cancellation wakeup, and dependency-safe destruction (archive 277).
11. Add destroy-busy during active execution and cleanup fault-path tests.
12. Only after the single-thread cooperative VM/task lifecycle passes should `task.spawn`, `task.wait`, and Channel work be implemented. pthread worker pool remains a later major version.

## 10. Current implementation status

Archives 267–272 provide the opaque `FlatVm` ownership handle and VM-owned bytecode/configuration snapshots. Archive 273 adds instruction-budget stepping and resume; archives 274–275 cover nested control flow, output side-effect equivalence, failure, and destruction after yield. Archive 276 adds a runtime-owned `FlatTask` registry and a synchronous round-robin scheduler. Archive 277 adds C-level wait/wakeup and safe-point cancellation. Archive 278 adds `task.spawn`, `task.wait`, and `task.state` for runtime-owned task VMs. Archive 279 adds named block-parameter metadata to `.fb` v14 and deep-copied argument binding for `task.spawn`. Archive 280 adds runtime-owned Channel storage, C1 per-operation serialization, C2 explicit owner transactions, cross-VM value copies, and task-terminal rollback. Archive 281 adds FlatL Channel natives and direct numeric handle transfer through task.spawn. Archive 282 adds wait-cycle rejection, direct-argument Channel references held until child termination, and Channel event producers for --show-channel-log. Archive 283 adds retryable Channel contention for task-native lock/set/var, terminal task result objects, and stress coverage. Archives 305–327 implement and stress the runtime-owned worker pool on supported native hosts, with Windows and broader platform validation tracked separately. Archive 351 adds the FLEditor WASM cooperative root-task runner.

## 11. Stepping regression coverage (archive 275)

The `FlatVm` step/resume path is now covered by two complementary integration cases in the existing `bin/run_tests` framework:

- `flvm[20]` runs the same compiled bytecode through `flat_vm_run_opts()` and through repeated `flat_vm_step()` calls. It compares exit code, termination, and captured stdout byte-for-byte. The program includes nested calls, `for`, `foreach`, nested unique executor scopes, and a caught panic handler. It also verifies that caller-owned output pointers supplied at creation are not retained or modified.
- `flvm[21]` verifies a logical `FAIL` after a yield, an unhandled native panic, rejection of further stepping after either terminal failure, and destruction of a VM immediately after a yield. The destroy-after-yield case confirms that only effects from already completed instructions are visible; later instructions do not execute after destruction.

These tests validate single-threaded continuation and cleanup behavior. They do not establish general thread safety, native-call preemption, cancellation of a running native, Channels, or worker-pool readiness.


## 12. Archive 276 — first cooperative FlatTask scheduler

Archive 276 adds `include/flat_task.h` and extends `FlatRuntime` with an owned task registry. Each task owns one persistent `FlatVm`, receives a monotonically increasing runtime-local `FlatTaskId`, and exposes state/result through query APIs. Tasks begin in `CREATED` and require an explicit start transition to `RUNNABLE`.

`flat_runtime_schedule(runtime, quantum, max_quanta, &out_quanta)` performs synchronous round-robin scheduling by task ID. Each turn calls `flat_vm_step()` with the configured instruction quantum. Yielded tasks return to `RUNNABLE`; successful VMs become `COMPLETED`; VM failures become `FAILED` without stopping other runnable tasks. `max_quanta == 0` runs until no runnable task remains.

Task records and VMs remain runtime-owned until `flat_runtime_task_destroy()` is called. Only `CREATED` or terminal tasks may be destroyed. `flat_runtime_destroy()` now returns busy while the registry is non-empty, including when all remaining tasks are terminal. IDs are never reused. Task registry and scheduler operations are single-host-thread APIs; no cross-thread task API safety is claimed.

`flvm[22]` validates two successful tasks interleaving their output with quantum one, a third task failing independently, result/state queries, explicit task destruction, runtime busy behavior, and monotonic non-reused IDs. `flvm[23]` covers C-level wait/wakeup and cancellation. `flvm[24]` covers language-level spawn/wait and wakeup after both successful and failed child completion. Channels and spawn argument binding were added in later milestones; wait-cycle detection is added in archive 282. pthread workers remain out of scope.


## 13. Archive 277 — task wait/wakeup and cancellation

Archive 277 adds two C-level scheduler operations:

```c
int flat_runtime_task_wait(FlatRuntime *, FlatTaskId waiter, FlatTaskId target);
int flat_runtime_task_cancel(FlatRuntime *, FlatTaskId id);
int flat_runtime_task_cancel_request(FlatRuntime *, FlatTaskId id);
```

`flat_runtime_task_wait()` accepts only a `RUNNABLE` waiter and a valid, distinct target. If the target is already `COMPLETED`, `FAILED`, or `CANCELLED`, the call succeeds without suspending the waiter. Otherwise the waiter transitions to `WAITING` and records the target ID. When the target reaches any terminal state, the scheduler changes all tasks waiting on that ID back to `RUNNABLE`. A target with registered waiters cannot be destroyed; this prevents dangling dependencies. Self-wait, unknown IDs, and a second wait by a non-runnable waiter are rejected. Waiting cycles are rejected by the runtime wait-chain check added in archive 282.

`flat_runtime_task_cancel()` accepts `CREATED`, `RUNNABLE`, and `WAITING` tasks that are not dispatch-owned, but rejects `RUNNING` and terminal tasks. It remains a synchronous compatibility API. `flat_runtime_task_cancel_request()` publishes intent only. Repeated requests return 0, terminal tasks return 1, and invalid/unknown IDs return -1. It also returns -1 when a nonterminal task is already owned by synchronous cancellation or destruction; when scheduler-owned cancellation is already in progress (`cancel_requested` is set), repeated requests remain idempotent and return 0. For unowned CREATED/RUNNABLE/WAITING tasks, the scheduler claims cleanup before selecting more bytecode. For RUNNING tasks, the current dispatch owner checks the request after `flat_vm_step()` returns. A long native call therefore delays cancellation; the caller never destroys a running VM. VM resources and task-owned Channel state are cleaned before `CANCELLED` is published and dependants are woken. If the VM completed or failed first, that terminal result wins.

`flvm[23]` covers self/unknown wait rejection, WAITING transition, wakeup after successful completion and failure, immediate wait against an already-terminal target, prevention of destroying a task with waiters, cancellation of a never-started task and a WAITING task, cancellation of a waiting target, wakeup after cancellation, cancellation after a yielded quantum, terminal result queries, and rejection of repeated/unknown cancellation. The test uses the existing `bin/run_tests` framework.

The initial FlatL native integration is documented in `natives/task.md`. `task.spawn(#block, ...)` creates a child VM from the same bytecode and starts it at the named block. The target block's declared parameter names are retained in the v14 BPAR bytecode section; the positional argument count must match exactly. Values are evaluated in the parent and deep-copied into the child's private VCX and pools. Scalars, strings, LISTs, and OBJECTs are transferable; file handles are not. `task.wait(taskId)` records a scheduler dependency and yields after the native instruction completes; it does not block the OS thread or repeat the native call after wakeup. `task.state(taskId)` returns the numeric lifecycle state so code can distinguish successful, failed, and cancelled targets. These natives require a runtime-owned task context; the non-task one-shot VM APIs do not provide one. Channels, waiting-cycle detection, and pthread workers remain out of scope.


## Archive 278 — FlatL task native integration

`use native task` is now recognized as a built-in native module. The initial surface is `task.spawn(#block)`, `task.wait(taskId)`, and `task.state(taskId)`. Spawn creates a new VM from the current task's bytecode, resolves the requested block label as its entry point, and registers the child as RUNNABLE. It currently accepts only the block label; no parent variables or arguments are copied into the child.

`task.wait()` is implemented as a scheduler-aware native suspension. The runtime transitions the currently RUNNING task to WAITING; the VM completes the native instruction, advances IP, and returns a dedicated internal wait outcome. The scheduler preserves WAITING rather than treating this as an ordinary quantum yield. When the target reaches COMPLETED, FAILED, or CANCELLED, the existing wakeup path makes the waiter RUNNABLE. The native call is not replayed after resume. `task.wait()` returns BOOL true when the dependency is accepted, and `task.state()` returns the numeric public lifecycle enum.

`flvm[24]` verifies that a root task spawns a child block, suspends in `task.wait`, resumes after child completion, observes the child's COMPLETED state, then waits for a second child that terminates with FAIL and observes FAILED. The test also checks the C task result records. Task natives require a runtime-owned task bridge and therefore fail outside a task scheduler. Argument passing and Channels were added in later milestones; wait-cycle detection was added in archive 282. pthread workers remain future work.


## 14. Archive 279 — spawn argument binding

Archive 279 extends the `.fb` format to v14 with a `BPAR` section immediately following `JBMX`. `BPAR` stores each block's parameter count and parameter names in block-directory order. The section is mandatory for v14 output; the loader continues to accept older v3–v13 bytecode, where block parameter metadata is absent.

`task.spawn(#worker, value1, value2, ...)` evaluates its expressions in the parent and requires an exact match with the target block's declared parameter count. The names are taken from the target block header, e.g. `#worker(price, label, items)`. The child receives only these named values in its initial global VCX context. Scalars and strings are copied; LIST and OBJECT values are recursively deep-copied between independent VM pools. Child-side mutation therefore does not affect the parent's value. `VAL_FILE` is deliberately rejected because file handles belong to a VM-local file table.

The new `flvm[25]` integration test transfers a number, a string containing a comma, a LIST, and an OBJECT. The child mutates its LIST and OBJECT and the parent verifies its originals remain unchanged. This validates nested argument parsing, block parameter metadata round-trip, exact value isolation, and task wait/resume around a parameterized child.


## 12. Runtime Channels C API (archive 280)

`include/flat_channel.h` exposes runtime-owned `FlatChannelId` handles and create/set/var/lock/unlock/revert/destroy operations. Each `FlatRuntime` owns its Channel registry; Channel IDs are monotone and are not reused. A runtime remains busy while any Channel is registered.

Each Channel owns independent string and collection pools. `set` recursively deep-copies supported values into those pools; `var` returns a separately owned deep copy into caller-provided pools. Numbers, booleans, null, strings, LIST, and OBJECT are supported. File handles are rejected. Failed writes leave the prior key value intact.

C1 serializes each operation with a per-Channel C11 atomic gate. This is the portable cooperative milestone and does not imply general VM/runtime thread safety. C2 transactions take a deep snapshot and are owned by a nonzero `flat_task_id`: `unlock` commits, `revert` restores the original snapshot while retaining the lock, and non-owner operations return a busy result. A Channel cannot be destroyed while locked.

Transactions are automatically rolled back and unlocked when their owner task completes, fails, is cancelled, or is destroyed. Task cleanup uses the retained snapshot without allocating. The current API does not yet make a non-owner wait; it receives a busy result.

The new `flvm[26]` test covers Channel registry ownership, independent handles, scalar/string/LIST/OBJECT copies, rejection of file handles, failed-write atomicity, transaction ownership/revert/commit, busy destroy, task-cancellation rollback, and explicit Channel cleanup.

FlatL Channel natives and handle transfer are added in archive 281. Archive 282 adds Channel log events, wait-cycle rejection, and task-held direct Channel argument references. pthread workers remain future work.


## 14. Archive 282 — wait cycles, Channel references, and logging

The task wait graph has at most one outgoing edge per WAITING task. Before adding `waiter -> target`, the runtime follows `target` and each subsequent `waiting_on` edge. If the chain reaches `waiter`, the request is rejected. This catches two-task and longer cycles without allocating or changing task states. `flvm[23]` now includes a three-task cycle case.

Channel lifetime now tracks task references. During `task.spawn`, each direct numeric argument that exactly matches a currently registered Channel ID adds a task reference before the child is registered. Task-level Channel operations also register the calling task. References are released on completion, failure, cancellation, and task destruction. A Channel destroy from a task is allowed when all remaining references belong to that caller; host-level destroy is rejected while any task reference remains. Since IDs are plain NUMBER values, a coincidental numeric argument can conservatively pin a Channel; nested LIST/OBJECT values are not scanned.

The existing `--show-channel-log` category now receives `create`, `set`, `var`, `lock`, `unlock`, `revert`, and `destroy` events. Events report IDs, key names, value types, action/outcome, and rejection reason, but never log stored values. `flvm[28]` checks a logged Channel lifecycle and proves that a spawned child prevents early destruction, then releases its reference so the parent can destroy the Channel after `task.wait`.


## 15. Archive 283 — Channel contention, task result summary, and stress coverage

Archive 283 extends scheduler-aware waiting from task completion to Channel transaction contention. A task-native `channel.lock`, `channel.set`, or `channel.var` that encounters a transaction owned by another task records a dependency on that transaction owner and enters `WAITING`. The VM preserves the current `OP_NATIVE` instruction rather than advancing its IP; after the owner commits or aborts, the task resumes by retrying that same native instruction. The native argument/value parsing is therefore repeated from the unchanged VM state, and the operation's side effect occurs only when it finally succeeds.

A waiting task registers a task-held reference to the Channel so the Channel cannot be destroyed while the waiter is suspended. `channel.unlock` wakes waiters for that Channel. Owner completion, failure, cancellation, or destruction automatically aborts its transaction and the existing terminal-task wakeup releases dependencies. `channel.revert` does not wake waiters because the transaction remains locked. The C API still reports a busy result (`-2`) to host callers; scheduler waiting is a FlatL task-native behavior with an active task bridge.

The task bridge now has a distinct `retry_requested` continuation flag. This is different from `task.wait()`: task wait completes its native instruction before yielding, whereas a contended Channel native has not completed and must be retried. The VM returns a scheduler wait outcome without advancing IP when this flag is set and skips binding the native's provisional output. The `flvm[29]` test verifies contention and retry for `channel.set`, `channel.var`, and `channel.lock` under a one-instruction quantum.

`task.result(taskId)` is added as a FlatL native returning an OBJECT with numeric `state`, `exitCode`, and `termination` fields plus a `diagnostic` string. It is available only for terminal tasks. The diagnostic in this FlatL object is truncated to `FLAT_INLINE_STR_MAX`; the C API `flat_runtime_task_result()` continues to return the complete `FlatVmResult`. This object is execution metadata, not a user-defined block return value.

`flvm[24]` now exercises `task.result()` for successful and failed children. `flvm[30]` stresses 48 cooperative tasks and 24 independent Channels with 288 stored/read values. These tests use the existing `bin/run_tests` framework. The scheduler remains single-threaded; these changes do not establish pthread worker safety or general runtime thread safety.


## 15. Channel contention retry and wakeup contract (archives 283–284)

Channel operations executed by a FlatL task can suspend when another task owns the Channel transaction. The retry contract is distinct from `task.wait()`:

- `task.wait(target)` completes its native instruction before suspension. On resume, the VM continues at the next bytecode instruction.
- A contended `channel.lock`, `channel.set`, or `channel.var` has not completed. The VM retains the current instruction pointer and invokes that same native instruction again after wakeup. It must not bind a provisional native result.
- A native that requests retry must not leave partial externally visible effects before returning the retry indication.

A Channel waiter records the transaction owner as its wait-graph target and pins the Channel for the duration of the wait. Registration rechecks the transaction state after the initial busy result; if the owner has already released the transaction, the operation is retried immediately instead of entering WAITING. The Channel reference is released by terminal task cleanup, not merely by wakeup.

Commit/unlock wakes Channel waiters. `revert` keeps the transaction lock and does not wake them. Completion, failure, or cancellation of the owner rolls back and releases its transaction, then wakes task dependants. A woken waiter retries the original operation and must re-check the Channel state; wakeup does not reserve ownership or guarantee FIFO order.

This suspension mechanism exists only for task-native calls with a scheduler continuation. Direct host C API calls have no VM waiter and continue to report busy. The implementation remains cooperative and single-threaded; the wakeup lists, task registry, runtime lock, and Channel gate are not a worker-pool synchronization contract.

`task.result(taskId)` is available only after a task becomes terminal and returns an OBJECT containing numeric `state`, `exitCode`, numeric `termination`, and `diagnostic`. It is execution metadata, not a user-defined return value from the task block. The FlatL diagnostic string is capped at the inline-string limit; the C result API retains the full `FlatVmResult`.

Archive 284 extends `flvm[29]` to verify that both cancellation and VM failure of a transaction owner roll back/release the Channel and wake a waiting task, which then retries the same `channel.lock` instruction and completes.


## 16. Archive 285 — task destruction, wait graph, and Channel reference audit

The task-destruction audit confirms that `flat_runtime_task_destroy()` rejects RUNNING/RUNNABLE/WAITING tasks and rejects any target with incoming waiters. This prevents freeing a task record while another task still stores its ID in `waiting_on`. A WAITING task must first be cancelled; cancellation clears both dependency fields and releases the task's Channel references.

The Channel contention path uses both `waiting_on` (transaction owner) and `waiting_channel_id` (Channel lifetime/wakeup association). On cancellation, task cleanup removes the Channel user reference. On commit/unlock, wakeup clears the dependency fields and moves the waiter to RUNNABLE, but deliberately does not release the Channel reference: the waiter may still retry the native operation. The reference is released only on terminal cleanup or explicit cancellation.

`flvm[29]` now adds two lifecycle sequences: (1) an owner with a Channel waiter cannot be destroyed; after waiter cancellation/destruction, destroying the CREATED owner aborts its transaction and allows Channel destruction; (2) after commit wakes a Channel waiter, the RUNNABLE waiter cannot be destroyed, but can be cancelled and destroyed, after which the owner and Channel can be destroyed. These checks cover both WAITING and wake-before-retry windows.

No runtime implementation change was required by this focused audit: the existing guards and cleanup paths satisfy the tested cooperative contract. This conclusion is limited to the scheduler-driving host thread. The public task registry and scheduler APIs are not safe for concurrent calls from multiple host threads; the runtime's atomic gate does not protect task records or wait-graph traversal. Worker-pool work remains blocked on a separate synchronization/ownership design.


## 16. No cooperative suspension while owning a Channel transaction (archive 286)

A task that owns any Channel transaction must not suspend on another Channel. This rule applies to all task-native contention paths: `channel.lock`, `channel.set`, and `channel.var`. If the operation would need to register a Channel waiter while the caller owns a transaction on any Channel, the operation is rejected and the task follows its normal failure cleanup. That cleanup aborts all transactions owned by the task and releases its Channel references.

The ownership check is performed before acquiring the runtime and target-Channel gates because the ownership scan itself traverses and locks Channels. This avoids recursive acquisition of a Channel gate in the current cooperative implementation.

This rule matches the existing prohibition on `task.wait()` while owning a transaction. It is a cooperative scheduler invariant only; it does not provide pthread lock ordering, cross-thread deadlock prevention, or general runtime thread safety. `flvm[29]` verifies the rule separately for `channel.lock(2)`, `channel.set(2, ...)`, and `channel.var(2, ...)` after the task has locked Channel 1.


## 17. Multiple Channel transactions and waiter wakeup across Channels (archive 287)

A task may own transactions on more than one Channel at the same time. Each Channel transaction remains independently locked and is owned by the same task ID. Other tasks may wait on that owner through different Channels; each waiter still has one outgoing wait dependency and one associated `waiting_channel_id`.

Committing Channel A wakes only the waiters registered for Channel A. A waiter for Channel B remains WAITING if the same owner still holds B. When the owner reaches a terminal state, runtime cleanup aborts every remaining transaction owned by that task, then wakes the task dependants. The Channel waiters become RUNNABLE and retry their original native operations; the retry path checks the Channel state again.

The `flvm[29]` regression covers this ordering and checks the intermediate RUNNABLE states before allowing the waiters to execute. It also corrects the archive-286 no-suspend test setup: after Channel 1 had been destroyed, those programs still tried to lock Channel 1 and failed before reaching the intended contention. The corrected test locks live Channel 3 and contends on live Channel 4.

This remains a cooperative single-thread guarantee. The current wakeup scans and task/Channel registries are not an atomic protocol for concurrent host threads; pthread worker safety is not implied.


## 18. Archive 288 — chained task.wait and Channel dependencies

`task.wait()` and Channel contention share one wait-graph edge (`waiting_on`). Channel waiters additionally record `waiting_channel_id`, which is used for Channel-specific wakeup and reference lifetime; it is not a second graph edge.

A Channel waiter may itself be the target of another task's `task.wait()`. A separate task may also wait directly for the Channel transaction owner. The resulting chain can be `downstream task -> Channel waiter -> transaction owner`, with another independent incoming edge to the owner.

Wakeup is intentionally differentiated: Channel unlock wakes only waiters associated with that Channel. Completion/failure/cancellation of the Channel waiter wakes tasks waiting for that waiter. Completion/failure/cancellation of the transaction owner wakes tasks waiting directly for the owner. The `flvm[29]` regression verifies these transitions in order.

Under the cooperative no-suspend-while-owning-transaction rule, an active Channel transaction owner cannot itself be WAITING on another task or Channel. Thus a valid Channel-owner edge terminates at that owner; a mixed cycle continuing through an active owner is excluded by this invariant. This is not a general deadlock-freedom claim and does not cover future synchronization primitives or host threads.

## 19. Cancellation and failure in chained wait graphs (archive 289)

Cancellation and failure resolve only the outgoing dependency of the affected task and the incoming dependencies that target that task. They do not recursively complete an entire chain.

For a chain `D --task.wait--> C --Channel wait--> B`:

- Cancelling B releases its Channel transaction and wakes C. A separate task waiting directly on B is also woken.
- Cancelling C removes C's Channel wait metadata/reference and wakes D because C is now terminal. D is only RUNNABLE; its next instruction may independently contend on B's still-active Channel transaction and become WAITING on B.
- If B is subsequently cancelled/failed/released, D can be woken through its own Channel dependency.
- If C is woken by B, retries its original Channel operation, and then fails, C's terminal cleanup releases its transaction and wakes D through the `task.wait(C)` edge.

Wakeup is a state transition, not a promise that the dependent's next operation completes without another wait or failure. `flvm[29]` covers owner cancellation, intermediate waiter cancellation, and failure after a successful Channel retry. These are cooperative single-thread guarantees only.


## 20. Runtime teardown and resource ownership (archive 290)

`flat_runtime_destroy()` is deliberately not a force-cleanup operation. It refuses to destroy a runtime while an execution is active, any task record remains registered, or any Channel remains registered. COMPLETED, FAILED, and CANCELLED task states do not remove the task record: the host must explicitly call `flat_runtime_task_destroy()`.

A RUNNABLE or WAITING task must first be resolved or cancelled. A Channel waiter that becomes RUNNABLE after owner cancellation still holds its Channel reference until terminal cleanup/cancellation. Likewise, `flat_runtime_channel_destroy()` rejects active transactions and references held by other tasks. The host teardown order is: resolve/cancel tasks and dependencies; destroy CREATED/terminal task records; destroy unlocked and unreferenced Channels; then destroy the idle runtime.

The `flvm[29]` teardown regression verifies CREATED, RUNNABLE-with-transaction, WAITING, woken-RUNNABLE, and terminal-task registry states; it also verifies Channel pinning and fresh-runtime ID/registry independence. Existing `flvm[15]` separately verifies rejection while a VM run is active.

The host must prevent new API calls from racing with `flat_runtime_destroy()`. The active-run counter rejects destruction while an already-entered VM/scheduler call is in flight, but the runtime lock is not a general concurrent API lifetime gate. Task registry/scheduler APIs remain confined to the scheduler-driving host thread. No pthread or worker-pool safety is implied.

## 12. Worker-mode cancellation request and shutdown (design contract, archive 296)

The current `flat_runtime_task_cancel()` is a cooperative host-thread operation: it directly calls `flat_vm_cancel()` for CREATED/RUNNABLE/WAITING tasks, then rolls back Channel transactions, releases Channel references, marks the task CANCELLED and wakes dependents. `flat_vm_cancel()` destroys the VM execution state and rejects a RUNNING VM. This remains valid only under the current scheduler-driving host-thread confinement.

Archive 303 implements `flat_runtime_task_cancel_request()` before worker creation. The request caller only publishes intent. The cooperative scheduler claims unowned CREATED/RUNNABLE/WAITING requests and the current dispatch owner observes RUNNING requests after `flat_vm_step()` returns; a long-running native call delays that boundary. A future worker pool will use a dedicated cleanup queue for unowned tasks. `CANCELLED` is published only after rollback, outgoing wait-edge removal, Channel-reference release, VM cleanup, and dependent wakeup publication. Repeated requests are idempotent. If the VM reaches a terminal result before the owner observes the request, that terminal outcome wins. Cancellation does not run user-level finally/defer code and does not use `pthread_cancel()`.

The worker pool has separate `RUNNING`, `DRAINING`, `CANCELLING`, and `STOPPED` lifecycle states. DRAIN rejects new task submissions and `task.spawn`, lets already-started tasks finish, and does not auto-start CREATED tasks. A quiescent unresolved wait graph is a blocked drain, not successful shutdown. CANCEL requests cleanup for all nonterminal tasks and keeps workers alive until cleanup completes. DRAIN may escalate to CANCEL; downgrade is forbidden. Shutdown request and worker join are separate phases; every worker must be joined before runtime destruction. The proposed API names and blocked-DRAIN status/timeout details remain provisional until implementation review.

Request-only cancellation is now implemented, but this is not a worker-pool implementation. There is still no dedicated cleanup queue, shutdown request API, DRAIN/CANCEL state machine, join implementation, pthread worker loop, or complete-VM multi-worker concurrency guarantee. See [`../plans/09_CANCELLATION_SHUTDOWN_SPEC.md`](../plans/09_CANCELLATION_SHUTDOWN_SPEC.md) for the full state rules and acceptance matrix.


## 21. Scheduler synchronization foundation (archive 299)

The implementation now includes cross-platform `FlatMutex`, `FlatCond`, and `FlatThread` primitives, an internal MPMC ready queue of task IDs, and a `FlatTaskDispatchState` state machine. The dispatch state distinguishes `queued`, `worker_owned`, and `wake_pending`, and uses a dispatch token to reject stale worker release. A wakeup received while the worker owns the VM is coalesced and requests one requeue after ownership is released.

These primitives are tested independently in `flvm[37]`; they are not connected to `FlatRuntime` or `FlatTaskRecord`. The cooperative scheduler, task registry, wait graph, Channel registry, cancellation API, and runtime teardown contract remain unchanged. No worker threads execute FlatL VMs in archive 299, and no concurrent runtime API or worker-pool safety is claimed. See [`../plans/11_SCHEDULER_SYNC_FOUNDATION.md`](../plans/11_SCHEDULER_SYNC_FOUNDATION.md).


## 13. Runtime scheduler mutex and dispatch ownership (archive 300)

`FlatRuntime` now owns a `FlatMutex scheduler_mutex`. It protects task registry/state and wait-graph transitions, task ID allocation, runtime scheduling counters, Channel registry membership and Channel reference publication. The lock order is scheduler mutex → individual Channel gate. A Channel gate must be released before code tries to acquire the scheduler mutex.

Each `FlatTaskRecord` embeds `FlatTaskDispatchState`. Scheduler selection claims one dispatch token while holding the mutex. The scheduler releases the mutex before calling `flat_vm_step()`, then reacquires it to publish the result/state and release dispatch ownership. A normal yield queues the task again; a wakeup arriving while the task remains dispatch-owned is coalesced through `wake_pending` and materialized once on release. The current cooperative scheduler still scans the task registry; the standalone MPMC ready queue is not yet connected.

A native wait/retry callback may publish `WAITING` before its enclosing `flat_vm_step()` has returned. During that interval the task is still dispatch-owned. `flat_runtime_task_cancel()` and `flat_runtime_task_destroy()` therefore reject worker-owned records even if their visible task state is temporarily WAITING. This prevents an external host thread from cancelling or destroying a VM while it is still executing.

For an unowned task, cancellation/destruction marks an internal `cleanup_owned` guard under the scheduler mutex, then releases that mutex before calling `flat_vm_cancel()` / `flat_vm_destroy()`. VM teardown can acquire serialized native gates (for example, DOM/Lexbor) and must not run while holding the scheduler mutex. The task record remains registered until cleanup completes; other mutating operations and scheduler selection reject/skip it. The synchronous compatibility API remains available, and archive 303 adds the separate request-only cancellation API. Only one thread drives the cooperative scheduler; request publication and task-state queries are synchronized for other host threads. Worker creation and shutdown remain unimplemented.

Only one host thread may drive the public `flat_runtime_schedule()` at a time. A concurrent second call returns `-1`. Archive 305 adds a separate internal multi-worker dispatch path; public host-driven scheduling is rejected while a worker pool is active. Task registry/state APIs are synchronized while the runtime remains alive. This does not make `flat_runtime_destroy()` safe against concurrent new API entry: the host must retain exclusive lifetime control for destruction.


## 13. Request-only cancellation (archive 303)

The C API now exposes `flat_runtime_task_cancel_request()`. It returns `0` when the request is accepted or already pending, `1` when the task is terminal, and `-1` for an invalid/unknown task. The call publishes only `cancel_requested` under `scheduler_mutex`; it never touches VM state. The cooperative scheduler claims unowned requests before normal dispatch, while the current dispatch owner observes a RUNNING task's request after `flat_vm_step()` returns. The caller cannot interrupt a native call. Cancellation commit performs VM cancellation, wait-edge removal, Channel transaction rollback/reference release, terminal publication, and dependent wakeup in that order. Completion/failure wins if already reached.

`flvm[40]` validates CREATED, RUNNABLE, WAITING and RUNNING requests, repeated requests, unknown and terminal IDs, completion precedence, dependent wakeup, and the running-VM safe-point boundary using a held DOM invocation gate. The current implementation scans the task registry for pending unowned requests; a dedicated worker cleanup queue, DRAIN/CANCEL shutdown, join, and pthread worker loop remain future work.


## 22. Runtime shutdown lifecycle (archive 304)

The runtime lifecycle is separate from individual task states: `RUNNING`, `DRAINING`, `CANCELLING`, and `STOPPED`. The host requests shutdown with `flat_runtime_shutdown_request()` and inspects `flat_runtime_shutdown_status()` / `flat_runtime_lifecycle()`.

`DRAIN` closes admission for new task creation/start, native `task.spawn`, and direct `flat_runtime_run()` calls. Already-started tasks continue when the host drives `flat_runtime_schedule()`. CREATED tasks are not auto-started or cancelled; they remain host-owned records. If all runnable work is exhausted while a task remains WAITING, DRAIN reports `BLOCKED`.

`CANCEL` escalates DRAIN and requests cancellation for all nonterminal tasks, including CREATED. Existing safe-point cancellation cleanup performs the actual VM/Channel cleanup. DRAIN cannot downgrade CANCELLING. Repeated requests are idempotent.

Archive 304 initially implemented the lifecycle cooperatively. Archive 305 adds `flat_runtime_workers_start()` and `flat_runtime_workers_join()`. Workers wait on the ready-queue condition variable, execute at most one VM quantum per turn, and exit after lifecycle reaches STOPPED. Shutdown COMPLETE still does not itself mean the threads have been joined; the host must call `flat_runtime_workers_join()` before destroying the runtime. Runtime destruction also requires explicit destruction of all task and Channel records. `flvm[41]` covers lifecycle semantics and `flvm[42]` covers worker execution and join.


## 23. Runtime-owned worker pool (archive 305)

`flat_runtime_workers_start(runtime, count, quantum)` creates a fixed worker pool. Startup is guarded by a barrier: no worker executes a VM quantum until every requested thread has been created. If creation fails partway through, the runtime broadcasts stop and joins every thread already created. The pool is one-shot per runtime and accepts 1–256 workers with a nonzero quantum.

Each worker calls the internal scheduler for at most one quantum at a time. The scheduler mutex protects task selection/claim and result/state publication; `flat_vm_step()` and native handlers remain outside it. Multiple workers can own different tasks concurrently. The dispatch state and monotonically increasing token prevent duplicate task ownership and stale release. The public `flat_runtime_schedule()` remains an exclusive cooperative mode and is rejected while the worker pool is active.

Idle workers use a non-consuming condition-variable wait on the ready queue. A wake epoch is captured before the worker attempts a quantum, so a request-only cancellation or shutdown broadcast cannot be lost between checking runtime state and entering the wait. Queue pushes signal work. STOPPED broadcasts to idle workers.

`flat_runtime_workers_join()` is separate from shutdown request/status. Normally it succeeds only after lifecycle reaches STOPPED and no scheduler turn remains active. A blocked DRAIN cannot be joined; the host must resolve its waits or escalate to CANCEL. If a worker reports an internal error, join is allowed after its cancellation request and all worker handles are joined; the host may then use the cooperative scheduler to finish pending cleanup. Runtime destruction refuses while workers remain started or unjoined.

Only one host caller may execute `flat_runtime_workers_join()` for a given runtime at a time. The runtime publishes a `workers_joining` ownership claim under `scheduler_mutex`; concurrent join callers for that same runtime are rejected. If a native join fails, unjoined handles remain owned by the runtime and a later join call may retry them. POSIX `pthread_join()` errors are returned unchanged and are not all guaranteed to be transient. On Windows, a successful thread wait is followed by `CloseHandle()`; the stored handle is cleared only if the close succeeds. The host must still prevent `flat_runtime_destroy()` from racing any API call, including join.

Worker startup freezes process-wide native capability metadata and activates a process-wide worker-mode guard. During that interval `flat_native_invoke()` rejects UNSUPPORTED/UNKNOWN modules, while THREAD_SAFE calls proceed and SERIALIZED/PENDING calls use their module gates. `task` and `channel` are now SERIALIZED because their callbacks enter mutex-protected runtime/Channel state. Legacy plugins without capability declarations remain unsupported. This is dispatch-time enforcement: an unsupported native operation fails its VM when reached; the implementation does not yet scan all bytecode and reject the task before it starts. The guard also applies to other runtimes while any worker pool is active.

`flvm[42]` verifies four workers, 96 tasks, DRAIN/join, CANCEL/join, and rejection of a deliberately unsupported math module. The worker test passed 100 consecutive plain runs and five consecutive ThreadSanitizer runs. This remains a focused scheduler/worker validation, not a complete-VM concurrency certification. Windows and Emscripten worker execution were not validated; single-threaded Emscripten rejects worker creation through its `FlatThread` stub. Runtime destruction still requires the host to prevent new API calls from starting concurrently.

## 24. Task cleanup ownership and concurrent cancellation (archive 315)

`flat_runtime_task_destroy()` and synchronous cancellation publish `cleanup_owned` while holding `scheduler_mutex`, then perform VM teardown outside that mutex. During this interval, a second destructive operation is rejected. `flat_runtime_task_cancel_request()` follows the same ownership boundary: it rejects a nonterminal task when cleanup is already owned and no cancellation request is pending. A scheduler-owned cancellation that already has `cancel_requested` set remains idempotent and returns 0; terminal tasks continue to return 1.

`flvm[46]` exercises this race with the real worker pool. One worker is held inside a serialized DOM call while a host thread destroys a separate CREATED task whose VM teardown waits on the same DOM gate. A concurrent cancellation request for that task must return -1; after releasing the gate, destruction must complete, the running task must finish, DRAIN must reach STOPPED, and workers must join. This validates cleanup ownership publication and unlocked teardown under worker mode. It does not make `flat_runtime_destroy()` safe against concurrent API entry; the host still owns runtime lifetime exclusion.


## 14. FLEditor WASM Task/Channel runner — archive 351

FLEditor's browser-facing `flat_wasm_run()` path now calls `flat_runtime_run_scheduled()` instead of `flat_vm_run_opts()` directly. The adapter creates an independent `FlatRuntime`, creates and starts the root task, installs the existing Task/Channel native bridge through the normal runtime task-creation path, and drives `flat_runtime_schedule()` with a small cooperative quantum. It returns the root task's `FlatVmResult` semantics (exit code, termination and diagnostic).

The adapter is one-shot: runnable child tasks are allowed to finish; tasks still waiting after the runnable queue drains are cancelled through the runtime shutdown path; task records are destroyed before Channel records; and the runtime is destroyed before returning. This prevents child VMs, task-held Channel references, or open transactions from leaking between FLEditor runs. The adapter does not start the native worker pool and does not use browser Web Workers.

Three standalone examples are shipped in both `examples/task_channel/` and `docs/fleditor_site/examples/task_channel/`: `parallel_inventory.fl`, `transaction_rollback.fl`, and `task_results.fl`. They are listed in the FLEditor example manifest and exercised by `test/fleditor/wasm_task_channel_smoke.mjs`. `flvm[56]` runs the same examples through the C one-shot adapter repeatedly to validate result propagation and cleanup.

### Runtime event-source broker (archive 359)

Archive 359 introduces the first host-side event-source broker API in `include/flat_runtime_event.h`. A subscription is identified by a runtime-local numeric subscription ID and matches the explicit tuple `(sourceId, sourceKind, eventName)`. `sourceId` remains an ordinary NUMBER value; `sourceKind` is the namespace discriminator (for example, all socket transports use `"socket"`). No resource objects or OO handles are introduced.

The host can register/unregister subscriptions, publish a `FlatValue` to all matching subscriptions, inspect queue depth with `flat_runtime_event_pending()`, and retrieve queued values with `flat_runtime_event_next()`. Published values are deep-copied into runtime-owned queue nodes, so the producer may release its VM pools after publishing. Each matching subscription receives its own copy. File handles are not transferable. Each subscription has a bounded queue of 256 events; publishing returns `-2` if any matching queue is full, without partially delivering the event. An unmatched event returns 0. Successful publication wakes the runtime ready-queue wait primitive after releasing the scheduler mutex. This is only a wake notification: producers still never execute VM code, and an external consumer must drain/dispatch the queued event.

Archive 360 adds the queue-depth query and worker/event-loop wake notification on successful publication. It does not yet create handler tasks.

This is the broker foundation only. Archive 359 does **not** yet implement FlatL `task.spawnFor()`, handler task creation, serial/parallel policies, or persistent FLEditor sessions. Those are subsequent milestones. Existing `task.spawn()` semantics are unchanged.


## Archive 364 — FlatL `task.spawnFor()` native integration

`task.spawnFor(sourceId, sourceKind, eventName, #handler, ...args)` and `task.stopFor(subscriptionId)` are exposed through `use native task`. Source IDs remain ordinary numeric values and source kinds are explicit strings; socket transports share the `socket` kind. A subscription ID is separate from task IDs assigned to individual handler executions.

The runtime subscription owns a parsed bytecode snapshot and deep-copied bound arguments. This allows the registering task and its VM to be destroyed without invalidating the handler. Every matching event is dispatched by the scheduler into a fresh normal task/VM/VCX; the event payload is parameter zero and bound arguments follow. `task.stopFor()` unregisters the subscription and discards queued events, but rejects removal while dispatch is in progress.

`flvm[62]` validates native registration, native unregistration, event-first argument binding, automatic scheduler dispatch, and handler execution after the registering root task has been destroyed. This increment still uses the broker's fixed 256-event queue and parallel task creation. Serial mode, `maxActive`, configurable overflow/backpressure and real source adapters remain future work.
