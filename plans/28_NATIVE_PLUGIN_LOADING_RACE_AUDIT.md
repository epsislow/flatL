# Native Plugin Loading Race Audit — archive 316

## Scope

Audit the process-wide optional-native plugin loader and capability publication paths used by the runtime-owned worker pool. The goal was to verify concurrent first-use loading, handler publication, capability publication, and the boundary between plugin loading and worker-capability freeze.

This archive adds regression coverage. It does not change the loader's locking implementation because the inspected paths already serialize the relevant state transitions.

## Existing synchronization and ownership

- `g_runtime_lock` in `src/lib/native_plugin_host.c` serializes embedded registry initialization, search-path snapshot/configuration, lazy module loading, and `flat_native_runtime_freeze_worker_capabilities()`.
- `g_registry_lock` protects handler-table reads and writes. Plugin handlers are never invoked while either loader/registry lock is held.
- Plugin initialization stages registrations in thread-local arrays. `plugin_staging_end(1)` publishes the staged handlers under the registry lock only after the initializer returns success; failed initialization discards the staged handlers.
- A successfully loaded shared library remains resident for process lifetime. This is required because the registry stores function pointers into that module.
- `flat_native_module_set_thread_capability()` serializes updates with capability freeze using `g_capability_lock` and rechecks the frozen state after acquiring the lock.
- `flat_native_invoke()` calls `flat_native_runtime_prepare()` before selecting a module invocation gate. This ensures the first invocation does not select a default policy before a plugin's declared policy is published.
- Worker startup preflights registered VM manifests before capability freeze and before creating worker threads. The freeze operation takes the same loader runtime lock used by lazy loading.

## Findings

No demonstrated data race or partially published plugin state was found in the audited internal paths:

1. Concurrent first-use calls for the same module serialize through `g_runtime_lock`.
2. Concurrent first-use calls for different modules also serialize through that lock; each initializer's thread-local staging remains isolated, and each successful publication is committed under `g_registry_lock`.
3. The plugin capability declaration is published before the loader releases `g_runtime_lock`, so an internal worker-start freeze cannot overtake a partially completed load.
4. Once capabilities are frozen, a missing plugin cannot be lazily loaded; the existing post-freeze rejection is intentional and covered by `flvm[35]`.
5. Failed plugin initialization does not publish staged handlers; rollback remains covered by `flvm[13]`.

No production locking change was justified by this audit. The remaining process-wide constraints are intentional: plugin search configuration is fixed before first load/freeze, loaded libraries remain resident, and capability metadata is frozen process-wide after worker startup.

## Regression coverage added

`test/cases/flvm/036_native_invocation_gates/` now builds two plugin fixtures:

- `list`: declares `FLAT_NATIVE_THREAD_CAP_SERIALIZED` and its handler detects overlapping execution.
- `obj`: declares `FLAT_NATIVE_THREAD_CAP_THREAD_SAFE` and returns a distinct marker result.

The harness releases 16 callers simultaneously: eight first-use callers for each module. It verifies every handler result and then verifies that `list` and `obj` expose their respective declared worker policies. This covers concurrent first-use loading of distinct modules, not merely contention on one module.

The same test then races first-use preflight of a third plugin (`csv`) against process-wide worker-capability freeze. It accepts only two outcomes: the complete plugin handler+THREAD_SAFE policy is published before freeze, or freeze wins and the plugin load is rejected with the expected post-freeze diagnostic. A subsequent prepare attempt must not observe a partially loaded plugin.

Existing complementary coverage remains:

- `flvm[13]`: plugin initialization rollback and serialized lazy loading.
- `flvm[34]`: capability metadata and gate policy.
- `flvm[35]`: worker preflight and post-freeze lazy-load rejection.
- `flvm[36]`: per-module gates, DOM teardown gate, and concurrent multi-module first use.

## Validation

- `flvm[13]`, `flvm[34]`, `flvm[35]`, `flvm[36]`: PASS in a focused run.
- `flvm[36]`: 10 consecutive `bin/run_tests` runs PASS before the freeze-race extension; the final native harness with both multi-module loading and load-vs-freeze races passed 50 consecutive fresh-process executions, followed by a final `bin/run_tests` run.
- `flvm[42]`: PASS separately.
- `flvm[43-45]`: PASS in a focused run; the grouped command reached the execution limit before test 46, which was then run separately.
- `flvm[46]`: PASS separately.
- `make all -j2`: PASS.
- `make docs`: PASS (50 sections).

A combined `flvm[13] flvm[34-36] flvm[42-46]` run exceeded the command execution limit after `flvm[42]` passed. A second grouped `flvm[43-46]` run passed 43–45 before reaching the limit; 46 passed separately. These are not recorded as completed grouped-suite passes. The complete 1185-test discovery set was not executed in this validation.

## Next step

Proceed to deterministic worker-startup failure injection. Simulate `pthread_create()` failure at each worker index and verify stop/broadcast, join of every successfully started worker, retention of handles if join itself fails, and cleanup of worker-array/runtime state. Do not alter plugin-loader locks without a failing regression or a concrete violated invariant.
