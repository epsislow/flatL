# Emscripten single-thread worker gate — archive 327

## Scope

This checkpoint audits the `__EMSCRIPTEN__ && !__EMSCRIPTEN_PTHREADS__` branch in `include/flat_sync.h` and verifies the runtime's documented behavior when browser pthreads are not enabled. It is deliberately **not** an Emscripten toolchain build and does not validate WASM linking, Asyncify, browser execution, or Emscripten pthread mode.

## Source findings

- The single-thread branch uses inert mutex/condition storage; mutex lifecycle/lock/unlock and condition signal/broadcast return success because there is no competing OS thread in this configuration.
- `flat_cond_wait()` returns `-1`; a synchronous blocking wait cannot be implemented safely on the browser event loop by this abstraction.
- `flat_thread_create()` and `flat_thread_join()` return `-1`, and `flat_threads_supported()` returns `0`.
- `flat_runtime_workers_start()` checks `flat_threads_supported()` before acquiring runtime state or allocating the worker array. An unsupported build therefore rejects worker startup before changing the runtime lifecycle or entering native worker mode.
- The ordinary cooperative runtime remains available; this test focuses on construction/destruction and clean rejection of the worker-pool API.

## Regression

Added `flvm[54]` in `test/cases/flvm/054_emscripten_no_pthreads/`. Its dedicated Make target compiles the complete runtime source set with the host C compiler and `-D__EMSCRIPTEN__`, while explicitly removing `-pthread` from compile and link flags. This selects the exact single-thread branch in `flat_sync.h` and exercises `FlatRuntime` creation, rejected worker startup, unchanged `RUNNING` lifecycle, rejected join, and successful runtime destruction.

The host-compiler define is a branch/contract simulation only. It is not a substitute for `emcc` and must not be described as an Emscripten build.

## Validation — archive 327

- `make test-emscripten-no-pthreads TEST_BIN=/tmp/emscripten_no_pthreads_test` — PASS.
- Harness output: `emscripten-no-pthreads-ok sync=noop blocking=rejected worker-start=rejected runtime=clean`.
- `bin/run_tests 'flvm[54]' -v -e` — PASS (1/1; 1193 discovered at this snapshot).
- Actual `make -f Makefile.wasm fleditor-wasm` — NOT RUN: `emcc`/`emcmake` are not installed in this environment.
- Windows worker execution — NOT RUN: no Windows cross-compiler/runtime is available.

## Remaining gates

1. Run the real `Makefile.wasm` target with a supported Emscripten SDK and execute the generated smoke tests in Node/browser.
2. Build and run Windows worker tests on Windows (including partial startup rollback, join retry and shutdown).
3. Validate the newer `flvm[48–54]` cases on Termux; the user's complete 47/47 result applies to archive 320 only.

No production synchronization or scheduler code changed in this checkpoint.
