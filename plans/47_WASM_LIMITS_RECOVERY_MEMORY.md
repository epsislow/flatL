# WASM run limits, panic recovery and memory checks — archive 337

## Scope

Extend the headless WASM API tests using Node.js only. The tests load the real `flatl.js`/`flatl.wasm` module and exercise the exported C API; no Python test runner and no browser-only assumptions are introduced.

## New regression

`test/fleditor/wasm_api_limits_memory_smoke.mjs` covers:

- explicit Emscripten linear-memory growth with a bounded allocation before the first FlatL compile;
- `flat_wasm_set_run_limits()` with an explicit jump budget, asserting `vm.jump_limit` and `FLAT_TERM_PANIC`;
- independent `max_steps` enforcement with `no_jump_limit` enabled, asserting `vm.step_limit` and `FLAT_TERM_PANIC`;
- resetting run limits and successfully running after both limit panics;
- an unhandled native panic (`io.out.args`), validating the error buffer, `flat_wasm_get_last_panic()` and termination accessor;
- a successful independent run after the native panic, verifying the panic diagnostic is cleared;
- 100 repeated compile/run cycles after warm-up, checking that the linear-memory high-water mark does not continue growing materially.

The memory assertion deliberately treats `HEAPU8.byteLength` as a high-water mark, not as a live-allocation counter or proof of leak freedom. A modest explicit allocation forces one controlled memory growth before the compiler is invoked; repeated FlatL executions then check for runaway per-run growth.

The focused target `make fleditor-wasm-api-smoke` and aggregate `make fleditor-wasm-test` both run this new script after `wasm_api_lifecycle_smoke.mjs`. Every `.mjs` test is invoked using `WASM_NODE` (default `node`).

## Important memory finding

On the archived Emscripten artifact, the first simple FlatL compile/run grows the WASM linear memory from 25,559,040 bytes to 845,021,184 bytes. A compile-only probe reproduces the same growth, so this is not caused by VM execution, panic handling, or the 100-run loop. Ten successive `flat_wasm_run_source()` calls keep the high-water mark at 845,021,184 bytes; the added 100-run test also remains at that same value after warm-up. This indicates a very large compile-time working set rather than evidence of a per-run leak.

Source inspection identifies the dominant structural contributor: `FgirProgram` embeds `blocks[FGIR_MAX_BLOCKS]`, each `FgirBlock` embeds `instr[FGIR_MAX_INSTR]`, and each `FgirInstr` contains several fixed-size text fields. On the native compiler, `sizeof(FgirProgram)` is 418,121,328 bytes (`sizeof(FgirInstr)=3164`, `sizeof(FgirBlock)=405648`). The WASM resize requests observed during compilation are consistent with multiple large FGIR working objects. The exact allocation call-site mapping has not yet been instrumented in an Emscripten debug build, so the fixed-size FGIR layout is a strongly supported source-level explanation, not a complete allocation trace.

**This high-water footprint is unacceptable for a browser/mobile compiler even though it is stable across repeated runs.** Do not classify it as a leak, and do not hide it by weakening the test. The next engineering task should evaluate reducing the FGIR working set, preferably by moving the fixed block/instruction arrays to capacity-managed storage while preserving the same language limits and native/WASM behavior. Avoid simply lowering `FGIR_MAX_BLOCKS` or `FGIR_MAX_INSTR` for WASM because that would create a platform-specific language limit.

## Validation evidence

Using Node.js v22.16.0 and the archived Emscripten artifacts in this source snapshot:

- `wasm_api_limits_memory_smoke.mjs`: PASS; explicit memory growth 25,559,040 -> 42,729,472 bytes; compile high-water 845,021,184 bytes; after 100 repeated runs still 845,021,184 bytes.
- `make -o fleditor-wasm fleditor-wasm-test`: PASS, all ten headless WASM scripts.
- `make all -j2`: PASS.
- `make docs`: PASS; 50 sections generated.

`-o fleditor-wasm` intentionally skips the Emscripten rebuild and reuses the archived `.js`/`.wasm` pair because `emcc` is not installed in this execution environment. The user has independently confirmed that `make rebuild` including `fleditor-wasm` succeeds on Linux and Termux, and that the nine-test WASM suite passes in Termux on archive 336. The new test must be rerun by the user after rebuilding with their SDK.

No production C/VM source changed in this checkpoint. The change adds test coverage and records the compile-time memory issue for a dedicated follow-up.
