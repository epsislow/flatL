# Multi-Runtime Worker-Mode Ownership Audit — archive 318

## Scope

Audit the process-wide native worker-mode guard when more than one `FlatRuntime` owns an active worker pool. This is distinct from the runtime-owned worker arrays and thread handles: each runtime must release exactly the process-wide reference it acquired, without disabling the guard while another runtime still has workers.

## Existing implementation

`src/lib/native_capability.c` maintains `g_worker_mode_users` as an atomic unsigned reference count. `flat_native_worker_mode_enter()` requires process-wide capability metadata to be frozen and increments the count. `flat_native_worker_mode_leave()` uses a compare/exchange loop to decrement only when nonzero. `flat_native_worker_mode_is_active()` tests whether the count is nonzero.

`flat_runtime_workers_start()` acquires one reference after capability freeze and before creating OS workers. A clean partial-start rollback releases that reference. If rollback cannot join a created thread, the runtime retains its worker state and reference; a later successful `flat_runtime_workers_join()` releases it. Normal join releases the reference only after all worker handles have been joined and runtime-owned worker state is cleared.

Capability freeze is process-wide and irreversible for the process lifetime. This audit does not attempt to make capability overrides runtime-local.

## Regression coverage

The `flvm[42]` harness now verifies:

1. With runtime A active, a partial worker-start failure in runtime B rolls back B's reference but leaves the process-wide worker-mode guard active for A.
2. With two runtime pools active simultaneously, stopping and joining A leaves the guard active while B remains active.
3. Stopping and joining the last active runtime clears the guard.
4. Two distinct runtimes can complete `flat_runtime_workers_join()` concurrently; both joins succeed and the process-wide reference count returns to inactive.

These tests exercise both rollback and normal release paths, including concurrent atomic decrements. They do not claim that concurrent `workers_join()` calls on the same runtime, or concurrent runtime destruction, are supported.

## Findings

No production synchronization change was justified by this audit. The atomic process-wide reference count and per-runtime `worker_mode_entered` ownership flag provide the expected independent lifetime accounting in the tested interleavings. The regression was added to protect that contract against future changes.

## Validation

- `make all -j2`: PASS.
- `flvm[42]`: PASS with the new multi-runtime cases.
- Additional repeated `flvm[42]` runs passed before the concurrent-join case was added; a repeat batch was interrupted by the command execution limit and is not counted as complete.
- Full FLVM/default suite, ASan/UBSan for the expanded final harness, Windows worker execution, and actual Emscripten linking remain unverified.

## Next audit

Review the thread abstraction's join-error contract on POSIX and Windows, then inspect runtime shutdown/join ownership boundaries. Keep same-runtime concurrent join and concurrent destroy outside the supported contract unless a deliberate API synchronization design is introduced.
