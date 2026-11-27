# WASM C API and lifecycle testing — archive 335

## Scope

This checkpoint starts the headless validation layer for the FLEditor WASM module. Tests are JavaScript `.mjs` programs executed by Node.js against the actual Emscripten-generated `flatl.js`/`flatl.wasm` module. Python is not used as a test runner or as a replacement for the project's test framework.

## New API/lifecycle regression

`test/fleditor/wasm_api_lifecycle_smoke.mjs` exercises the exported C API:

- toolchain version and native-module metadata;
- path-separator configuration and run-limit setter/reset calls;
- `flat_wasm_compile()` output ownership and `flat_wasm_free()`;
- `flat_wasm_run()` on caller-owned compiled bytecode;
- compile diagnostics for invalid source;
- empty and malformed bytecode rejection;
- distinction between logical `FAIL` and a runtime/API error, including last termination and panic accessors;
- repeated compile+run operations in one module instance (40 independent runs);
- `flat_wasm_compile_entry()`, `flat_wasm_disassemble_entry()`, and `flat_wasm_run_entry()` using MEMFS.

The focused test is available as `make fleditor-wasm-api-smoke`; `make fleditor-wasm-test` builds the WASM module once and runs all nine Node.js WASM smokes. Both use Node.js. They assume an Emscripten SDK is available when a rebuild is needed.

## Existing test-harness correction

`test/fleditor/wasm_csv_reader_smoke.mjs` stages only the root-level `csv_reader.fl` and `stock.csv` fixtures in MEMFS. The FlatL program creates `examples/` and `examples/csv_reader/` with the native `fs.mkdir()` module, copies the CSV with the `file` module, and only then opens it with `csv.reader()`. The CSV module does not create directories. This keeps directory ownership in `fs`, file operations in `file`, and parsing in `csv`.

## Validation evidence

Using the WASM artifacts present in this source snapshot and Node.js v22.16.0:

- `wasm_api_lifecycle_smoke.mjs`: PASS.
- `wasm_csv_reader_smoke.mjs`: PASS after the MEMFS parent-directory fix.
- `wasm_csv_smoke.mjs`: PASS.
- `wasm_env_smoke.mjs`: PASS.
- `wasm_file_smoke.mjs`: PASS (relative, absolute and project-synced paths).
- `wasm_multifile_smoke.mjs`: PASS.
- `wasm_order_flow_smoke.mjs`: PASS.
- `wasm_smoke.mjs`: PASS (hello, logical FAIL, asynchronous line input).
- `wasm_var_smoke.mjs`: PASS.

The malformed-bytecode negative case intentionally emits `flvm: bad magic (expected FLAT)` on the WASM stderr path before the API returns its diagnostic; the test asserts the API failure and continues successfully.

The current execution container does not have `emcc` installed, so it could not rebuild the module during this checkpoint. These results validate the API against the archived `flatl.js`/`flatl.wasm` artifacts, not against a newly linked binary from the edited source tree. The user has separately confirmed that the WASM build succeeds on their Linux and Termux environments. A fresh build followed by `make fleditor-wasm-api-smoke` remains the next confirmation step on an SDK-equipped environment.

## Boundaries / next steps

- This is API-level functional and lifecycle coverage, not a leak-sanitizer proof for Emscripten allocations.
- Next add focused error-path and memory-growth checks where the exported API provides reliable observability; avoid treating WebAssembly linear-memory high-water size as proof of a leak because allocators may retain freed pages/blocks.
- Then port selected deterministic native VM cases into Node-driven WASM comparisons (stdout, return code, termination, diagnostics).
- The WASM runtime remains a single-threaded build; native worker-pool tests do not validate pthread behavior in this target.

## CSV fixture setup clarification

The CSV reader itself must not create project directories. The example now explicitly uses FlatL's `fs.mkdir()` to create `examples/` and `examples/csv_reader/`, then uses the `file` module to place the staged `stock.csv` fixture at the expected path, and only then opens it with `csv.reader()`. The Node.js harness stages only root-level input files in MEMFS; it does not pre-create the nested fixture directory. This makes the ownership clear and tests the actual FlatL `fs` + `file` + `csv` path.

The Makefile exposes `fleditor-wasm-api-smoke` for the focused C API/lifecycle test and `fleditor-wasm-test` for all nine headless WASM tests. Both execute `.mjs` tests with Node.js through `WASM_NODE` (default: `node`), independently of the `NODE` variable used by FLEditor server targets.
