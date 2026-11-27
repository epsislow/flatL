# Worker Startup Failure Injection — archive 317

## Goal

Validate rollback and ownership when the runtime cannot create every requested worker, and validate that a failed join does not cause the runtime to discard a still-joinable thread handle. This is a test-focused audit; it does not change worker scheduling semantics.

## Test seam

`src/common/flat_runtime.c` contains private wrappers around worker-pool thread creation and joining. Under `FLAT_RUNTIME_TESTING`, two one-shot counters can fail the next create/join after N successful calls. The `test-runtime-worker-pool` Makefile target alone defines this macro. The test hooks are not declared in public headers and are absent from normal production builds.

## Regression matrix

### Partial creation

For a four-worker pool, fail creation after 0, 1, 2, and 3 successful creates. These positions cover failure at the first, intermediate, and final worker creation. Each case requires `flat_runtime_workers_start()` to fail, stop and wake every worker already created, join them, release the worker array, and permit `flat_runtime_destroy()`.

### Join failure during startup rollback

Fail creation after two workers have started, then inject one transient join failure during rollback. Startup must return failure but retain `workers_started`, `worker_start_failed`, the worker array, and the still-started handle. Runtime destruction must remain rejected. A later `flat_runtime_workers_join()` must join the retained handle, release worker state, leave worker mode, and permit destruction.

### Join failure after successful startup

Start three workers, request CANCEL on an empty runtime to reach STOPPED, then fail one join after one successful join. The first `flat_runtime_workers_join()` must fail without discarding the failed handle; destruction remains rejected. A second join must finish cleanup and allow destruction.

## Findings

The existing implementation already retains each worker's `started` bit when `flat_thread_join()` fails and continues attempting to join other workers. The retry path in `flat_runtime_workers_join()` then visits only handles still marked started. The injected regressions validate this behavior and partial-start rollback without requiring a production logic change.

## Validation

- `make all -j2`: PASS.
- `make docs`: PASS (50 sections).
- `flvm[13]`, `flvm[34-36]`, `flvm[38-46]`: PASS in completed individual runs.
- `flvm[42]`: PASS in three additional consecutive runs with the failure matrix.
- One grouped run timed out after `flvm[38]`; a separate loop timed out after `flvm[41]`. These incomplete invocations are not counted as complete grouped passes.
- Full 1185-test suite, Windows worker execution, and actual Emscripten linking remain unverified.

## Next work

Continue the ownership audit beyond startup: inspect process-wide worker-mode guard transitions across multiple runtimes, concurrent runtime shutdown/join callers, and the exact contract for thread-join errors on each supported platform. Keep Windows and Emscripten validation separate from POSIX fault-injection evidence.
