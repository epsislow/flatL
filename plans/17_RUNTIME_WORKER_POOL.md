# Runtime Worker Pool — archive 305

## Scope

Archive 305 introduces the first runtime-owned background worker pool. It does not add a separate scheduler implementation: workers use the same task registry, ready queue, dispatch ownership, cancellation safe points and DRAIN/CANCEL lifecycle as the cooperative scheduler.

## API

```c
int flat_runtime_workers_start(FlatRuntime *runtime, size_t worker_count,
                               uint64_t quantum);
int flat_runtime_workers_join(FlatRuntime *runtime);
```

The worker count is limited to 1–256 and quantum must be nonzero. `flat_runtime_schedule()` remains the cooperative host-driven API and is rejected while workers are active. A runtime may start one pool; after it is joined, it is not restartable.

## Ownership and synchronization

- A worker calls the internal scheduler for at most one VM quantum per iteration.
- Multiple workers may own different tasks concurrently; dispatch state/token prevents duplicate ownership of one task.
- `scheduler_mutex` protects task selection/claim and state/result publication.
- `flat_vm_step()` and native handlers execute outside `scheduler_mutex`.
- Idle workers wait on the ready-queue condition variable. They do not dequeue a task merely to wait for an event.
- Wake epochs close the race between a worker's last state check and entering the condition wait. Queue pushes signal work; cancellation/shutdown broadcasts.
- Startup uses a barrier. No worker enters the scheduler until every requested OS thread was created. Partial failure sets the stop flag, broadcasts, joins started threads, and frees pool state.

## Shutdown and join

Workers continue to run while lifecycle is RUNNING, DRAINING or CANCELLING. They exit after lifecycle reaches STOPPED. DRAIN with unresolved WAITING tasks remains BLOCKED; join rejects it. CANCEL wakes idle workers so they can claim and perform request-only cleanup. `flat_runtime_workers_join()` normally requires STOPPED and zero active scheduler turns. If a worker reports an internal error, join is also allowed after the error path has requested CANCEL; after all handles are joined it returns an error status, clears the pool, and the host may use cooperative `flat_runtime_schedule()` to finish pending cleanup. `flat_runtime_destroy()` requires that join has completed, plus the existing empty task/Channel registries.

## Native module policy

Worker startup first preflights every registered VM manifest, loading permitted optional plugins, initializing libcurl through `flat_http_prepare()` when HTTP is declared, and rejecting effective UNSUPPORTED/UNKNOWN modules before creating any worker. New tasks are preflighted before registry publication while worker mode is active, including HTTP initialization checks. Only after successful preflight does startup freeze process-wide capability metadata and enter the process-wide worker-mode guard. Native dispatch repeats the unsupported check as defense in depth. THREAD_SAFE calls remain ungated; SERIALIZED and PENDING calls use the per-module invocation gate. Since `task` and `channel` now call into mutex-protected runtime/Channel operations, their built-in policy is SERIALIZED rather than UNSUPPORTED. `dom` remains PENDING, effectively SERIALIZED. Legacy plugins without declarations remain UNSUPPORTED.

Manifest preflight covers declared optional modules; runtime-generated native calls outside a VM manifest remain protected by the dispatcher guard. The process-wide guard also applies to other runtimes' native calls during a worker pool's lifetime.

## Validation

- `flvm[42]`: four workers, 96 tasks, DRAIN and join, CANCEL of CREATED tasks and join, plus rejection of a deliberately UNSUPPORTED `math` module during worker startup preflight.
- 100 consecutive plain executions of the worker-pool test passed after fixing a shutdown race where a worker could observe STOPPED between its preflight state check and internal scheduler entry.
- Five consecutive ThreadSanitizer executions passed on the earlier worker-pool regression shape. The final expanded test (including post-start task admission and direct-run rejection) was not re-run under TSAN: the GCC TSAN rebuild exceeded the 300-second command limit, and the available Clang TSAN runtime failed to link due missing libdispatch/Blocks symbols.
- Existing `flvm[38]`, `flvm[39]`, `flvm[40]`, and `flvm[41]` regression tests all passed in separate completed runs.
- `make all -j2` and `make docs` passed.
- Windows worker execution was not run. The single-threaded Emscripten branch was syntax-checked and `flat_threads_supported()` rejects worker startup before capability freeze; the actual `emcc` link was not run because `emcc` is unavailable.

## Known limitations / next audit

1. Preflight is based on each VM's declared optional-native manifest; it does not discover arbitrary native calls synthesized dynamically from runtime strings. The dispatcher guard rejects unsupported modules if such a path is reached.
2. The worker-mode guard is process-wide; while one runtime has workers active, unsupported native calls from all runtimes are rejected.
3. `flat_runtime_workers_join()` is blocking and only succeeds after STOPPED. A blocked DRAIN must be resolved or escalated to CANCEL first.
4. No timed join, worker resizing, worker restart, or concurrent runtime destruction is supported. A worker error can require the host to complete cancellation cleanup through the cooperative scheduler after join.
5. Full-VM stress with task/channel contention, HTTP, DOM teardown, and plugin lifecycle still needs a broader dedicated test matrix before claiming complete worker-mode safety. ASan/UBSan and the complete FLVM/default suite were not run for this archive.


## Subsequent validation checkpoints

- Archive 306 (`flvm[43]`) exercises nested `task.wait()` and two contending Channel transaction children under four workers; see `18_WORKER_TASK_WAIT_CHANNEL_AUDIT.md`.
- Archive 307 (`flvm[44]`) exercises a four-node wait chain, cycle rejection, cancellation of a middle waiter, and cancellation/cleanup of a Channel waiter under worker-mode CANCEL; see `19_WORKER_WAIT_GRAPH_CANCELLATION_AUDIT.md`.
- These tests add evidence for specific interleavings only. HTTP/DOM worker lifecycle and deterministic startup failure injection remain open. Archive 316 adds concurrent first-use loading coverage for two different native plugin modules and a load-vs-capability-freeze race; see `28_NATIVE_PLUGIN_LOADING_RACE_AUDIT.md`. Windows and actual Emscripten linking remain open.


- Archive 314 fixes a confirmed Android/Termux worker-thread stack overflow in `vm_state_execute()`: `OP_GUARD`'s `FLAT_MAX_VARS`-sized `FlatSlotType` and flag arrays are now persistent `VmState` heap scratch. See `26_VM_GUARD_STACK_SCRATCH.md`. Re-run `flvm[38]`–`flvm[46]` and the ASan reproduction on Termux before treating this regression as closed.
- Archive 314 was validated by the user on Termux with `flvm[38-46]` and `port[2-5]` in one invocation: 13/13 selected tests passed (1185 discovered; the full discovery set was not executed).
- Archive 315 closes a cleanup-ownership race: `flat_runtime_task_cancel_request()` no longer reports success for a nonterminal task whose cleanup is already owned by synchronous destruction/cancellation. It remains idempotent when scheduler-owned cancellation is already pending. `flvm[46]` now covers a destroy-vs-cancel-request interleaving while a worker is blocked in a DOM gate; see `27_WORKER_TASK_CLEANUP_RACE_AUDIT.md`.


- Archive 316 extends `flvm[36]`: 16 simultaneous first-use callers load/invoke two distinct optional modules (`list` SERIALIZED and `obj` THREAD_SAFE), then a third plugin load races process-wide capability freeze. The test verifies handler results, published capabilities, and the only two valid load/freeze outcomes. `flvm[13]` continues to cover failed plugin-init rollback; `flvm[34-35]` cover capability policy and freeze rejection. The implementation's existing runtime/registry/capability locks were retained because the audit found no demonstrated race requiring a locking change. See `28_NATIVE_PLUGIN_LOADING_RACE_AUDIT.md`.


## Deterministic startup failure injection — archive 317

The `flvm[42]` harness is compiled with `FLAT_RUNTIME_TESTING`, which enables private test hooks in `flat_runtime.c`. The hooks wrap only the worker pool's calls to `flat_thread_create()` and `flat_thread_join()` and are absent from ordinary builds. They fail once after a configured number of successful operations, then automatically disable themselves. No public header/API is added.

The regression matrix covers: (1) `pthread_create`-equivalent failure at each of four worker indexes; (2) a transient join failure while rolling back a partially created pool; and (3) a transient join failure after a normally started pool reaches STOPPED. In both join-failure cases, the runtime must retain the worker array and every still-joinable handle, reject destruction, and allow a later `flat_runtime_workers_join()` to finish cleanup. Already joined handles are marked not-started and are not joined again.

This is deterministic validation of the POSIX abstraction's failure paths, not proof of Windows behavior. The production implementation continues to call the same `flat_thread_create()`/`flat_thread_join()` primitives through thin internal wrappers; no scheduling, locking, or lifecycle policy was changed. See `29_WORKER_STARTUP_FAILURE_INJECTION.md`.


## Process-wide worker-mode ownership across runtimes — archive 318

The native worker-mode guard is a process-wide atomic reference count, while each runtime owns its own worker pool and records whether it acquired a guard reference. Archive 318 extends `flvm[42]` to cover partial-start rollback in a second runtime, overlapping worker pools, release of one runtime while another remains active, and concurrent joins on two distinct stopped runtimes. No production synchronization change was needed: each tested runtime releases exactly its own reference, and the process-wide guard remains active until the last pool has joined. Same-runtime concurrent join and concurrent runtime destruction remain unsupported and are not covered by this regression. See `30_MULTI_RUNTIME_WORKER_MODE_OWNERSHIP.md`.


## Join ownership update — archive 319

`flat_runtime_workers_join()` now claims `workers_joining` under `scheduler_mutex` before it touches any worker handle. A second caller for the same runtime is rejected while that claim is active. Native join errors clear only the operation claim; handles that did not join successfully remain marked started for a later retry. The runtime cannot be destroyed while the join claim is active.

`FlatThread` preserves its native handle/valid marker on join failure and consumes it only after successful native join/close. POSIX `pthread_join()` errors are returned unchanged and are not all assumed to be transient. On Windows, `WaitForSingleObject()` success is not treated as handle release: `CloseHandle()` must also succeed before the stored handle is cleared.

The host must still prevent `flat_runtime_destroy()` from racing any API entry. Same-runtime concurrent join is explicitly rejected; distinct-runtime joins remain independent. `flvm[42]` covers four simultaneous join callers on one stopped runtime and asserts exactly one successful join. See `plans/31_FLAT_THREAD_JOIN_OWNERSHIP.md`.


## Multi-graph cancellation stress — archive 320

`flvm[47]` adds eight independent A→B→C→D wait graphs in one runtime. Eight host threads synchronize on a barrier and concurrently publish request-only cancellation for each graph's B task. A four-worker pool then drains the runtime. Each round checks A/C/D COMPLETED, B CANCELLED, joins workers, and destroys all task/runtime state. The harness repeats five rounds (40 graphs and 160 task instances per invocation).

Local POSIX validation: `flvm[47]` passed normally, in five consecutive completed invocations, under ASan, and under UBSan. Existing `flvm[39-46]` relevant Channel, scheduler, wait-graph, HTTP, and DOM tests also passed individually. No production scheduler change was made because the added interleaving did not demonstrate a defect. This is focused evidence only; broader stress, Windows execution, actual Emscripten linking, and the complete test suite remain open. See `plans/32_WORKER_MULTI_GRAPH_CANCELLATION_STRESS.md`.


## Channel / shutdown stress checkpoint — archive 321

`flvm[48]` adds four independent Channels with eight transaction writers each under four workers, asserting exact read-modify-write totals after DRAIN. A second scenario parks four waiters behind four CREATED Channel-transaction owners, verifies DRAIN BLOCKED, publishes simultaneous cancellation requests for the waiters, and checks DRAIN completion. The pool is joined after STOPPED; the host then escalates to CANCEL and uses cooperative scheduling to clean the still-CREATED owners. This ordering is required by the existing lifecycle contract: workers exit at STOPPED, and CREATED tasks are excluded from DRAIN.

The harness passed five consecutive direct executions and ASan, UBSan and TSAN on the local POSIX host. No production synchronization change was justified. This does not validate arbitrary topologies or cross-platform worker behavior.

## Archive 322 — converging DAG cancellation checkpoint

`flvm[49]` adds deterministic fan-in DAGs, simultaneous cancellation requests, worker DRAIN, terminal-state verification, join and cleanup. No production synchronization change. See `34_WORKER_DAG_CANCELLATION_STRESS.md`.


## Staged wait-graph cancellation — archive 323

`flvm[50]` adds four independent 10-task DAGs per round. Seven leaf tasks fan out across two intermediate waiters, both converging on one sink; deterministic fan-out split varies by graph and round. Eight workers start the sinks, and the harness observes RUNNING before issuing staged cancellation: sink while RUNNING, intermediate while WAITING, then intermediate/leaf while RUNNING. A fourth graph is left untouched as a DRAIN control. The test checks all 40 terminal states, worker join, task destruction and runtime destruction over three rounds.

Local POSIX validation: `flvm[47–50]` passed individually; `flvm[50]` passed six consecutive direct harness executions and ASan, UBSan and TSAN. No production scheduler change was needed. This remains deterministic stress coverage, not randomized soak or cross-platform validation. See `plans/35_WORKER_DAG_STAGED_CANCELLATION.md`.


## Archive 325 — mixed DRAIN/CANCEL escalation soak

`flvm[52]` starts 18 of 24 tasks and leaves six CREATED tasks. A forced wait edge from started work into the CREATED suffix ensures DRAIN has an outstanding dependency. Eight host callers synchronize and concurrently request both DRAIN and CANCEL (at least one of each per round), exercising escalation and the invariant that DRAIN cannot downgrade CANCELLING. The test accepts CANCELLING or STOPPED after request publication depending on whether cleanup has already completed, then requires every task to be CANCELLED, joins workers, and destroys all task/runtime state. Twelve seeded runtime cycles run per invocation. Ten additional direct executions passed (120 cycles / 2,880 task instances); ASan passed. UBSan build exceeded the command limit and TSAN was not run. This is bounded stress evidence, not a long-duration certification. No production synchronization change was justified. See `plans/37_WORKER_MIXED_SHUTDOWN_SOAK.md`.
