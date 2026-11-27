# FLEditor WASM Task/Channel runner — archive 351

## Goal

Make the already-implemented FlatL Task and Channel natives runnable from FLEditor's browser-facing WASM API. Do not emulate task behavior in JavaScript and do not start pthreads or browser Web Workers.

## Existing architecture

`Makefile.wasm` already links `flat_runtime.c`, `flat_channel.c`, `mod_task.c`, `mod_channel.c`, the scheduler synchronization layer and the VM handle implementation. The missing integration was in `src/fleditor_wasm/flat_wasm.c`: `flat_wasm_run()` loaded bytecode and called `flat_vm_run_opts()` directly, so no runtime-owned task context or `FlatTaskNativeBridge` was installed.

## Implementation

- Add `flat_runtime_run_scheduled()` as a one-shot host adapter.
- Create an independent `FlatRuntime` using the current staged host configuration.
- Create and start a root task through the normal runtime task-creation path; this installs the existing Task/Channel bridge.
- Drive `flat_runtime_schedule()` cooperatively with an instruction quantum of 8.
- Return the root `FlatVmResult` semantics: exit code, termination and diagnostic.
- Let runnable child tasks complete. If tasks remain nonterminal after the runnable queue drains, request runtime CANCEL and drive scheduler-owned cleanup.
- Destroy task records before Channel records, so task-held Channel references and open transactions are released; then destroy the runtime.
- Route `flat_wasm_run()` through this adapter. Existing compile/run APIs and JS call signatures remain unchanged.
- Browser execution remains single-threaded cooperative execution. This does not enable the native worker pool or browser Web Workers.

## Runnable examples

The same standalone programs are stored in `examples/task_channel/` and `docs/fleditor_site/examples/task_channel/` and listed in the FLEditor examples manifest:

1. `parallel_inventory.fl`: three child tasks reserve stock through one shared Channel and explicit transactions.
2. `transaction_rollback.fl`: a worker writes a temporary value, calls `channel.revert()`, writes a corrected value and commits.
3. `task_results.fl`: waits for successful and failed children and reads `task.state()` / `task.result()`.

## Tests

- `flvm[56]` compiles and runs all three samples through `flat_runtime_run_scheduled()` three times each, checking terminal result metadata and repeated runtime/Channel/task cleanup.
- `test/fleditor/wasm_task_channel_smoke.mjs` runs all three samples through the real `flat_wasm_run_source()` API and checks their output and termination.
- `fleditor-examples-smoke` checks the manifest paths.

## Boundaries

- `flat_runtime_run()` remains a direct VM run and does not install a task bridge.
- `flat_vm_run_opts()` behavior is unchanged.
- The adapter is one-shot and destroys all runtime-owned tasks/Channels before returning; it does not expose persistent runtime handles to JavaScript.
- No UI event Channel bridge, persistent WASM task session, or worker-thread integration is introduced.

## Validation status

- `make all -j2`: PASS.
- `flvm[56]`: PASS on native host, including all three examples and repeated cleanup.
- `make test` was attempted locally: the runner reached 320/1,168 before the 300-second execution limit. This is incomplete, not a pass.
- Emscripten/WASM rebuild and `make fleditor-wasm-test` are pending validation on Termux because `emcc` is unavailable in the implementation environment.
