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
- These tests add evidence for specific interleavings only. HTTP/DOM worker lifecycle, plugin loading races, deterministic startup failure injection, Windows and actual Emscripten linking remain open.


- Archive 314 fixes a confirmed Android/Termux worker-thread stack overflow in `vm_state_execute()`: `OP_GUARD`'s `FLAT_MAX_VARS`-sized `FlatSlotType` and flag arrays are now persistent `VmState` heap scratch. See `26_VM_GUARD_STACK_SCRATCH.md`. Re-run `flvm[38]`–`flvm[46]` and the ASan reproduction on Termux before treating this regression as closed.
