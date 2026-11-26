# Build target source parity and HTTP test portability — archive 301

## Purpose

Archive 301 fixes two build regressions reported after archive 300. It does not change Task Runtime, Channel, scheduler, or worker-pool behavior.

## Native HTTP initialization test

`flvm[32]` previously invoked `pkg-config` unconditionally from its inline shell command. On systems where a `pkg-config` executable is present in `PATH` but cannot be executed (for example, a broken/non-executable symlink or restricted installation), the test failed before linking the HTTP transport. The top-level native build already has a `-lcurl` fallback, but this test did not.

The test now uses `test/cases/flvm/032_http_prepare/run.sh`:

- Prefer `pkg-config --cflags --libs libcurl` when the command is executable and returns flags.
- Fall back to `-lcurl` when `pkg-config` is missing, non-executable, or cannot resolve libcurl.
- Compile the same `flat_http_curl.c` transport and execute the same repeated `flat_http_prepare()` assertions.

The fallback intentionally matches the top-level Makefile's dynamic-libcurl fallback. Environments that only provide a static libcurl archive should install a working `pkg-config` file so its transitive link dependencies are included.

## Emscripten source graph

`Makefile.wasm` had drifted from the common runtime source graph. It included the updated `native_dispatch.c` but omitted implementations that the dispatcher/runtime now references. This produced unresolved symbols including:

- `flat_strtod`, `flat_snprintf` — `src/common/flat_locale.c`;
- `flat_native_module_worker_policy`, `flat_native_module_gate_enter` — `src/lib/native_capability.c`;
- `flat_native_task_*` — `src/lib/mod_task.c`;
- `flat_native_channel_*` — `src/lib/mod_channel.c`.

The runtime source graph also requires the shared implementations for logging, Channel storage, scheduler dispatch state and VM handles. The WASM source list now includes:

- `src/common/flat_runtime_log.c`;
- `src/common/flat_channel.c`;
- `src/common/flat_scheduler_sync.c`;
- `src/common/flat_vm_handle.c`;
- `src/common/flat_locale.c`;
- `src/common/flat_http_boundary.c` for the shared HTTP request-boundary sequence;
- `src/lib/native_capability.c`;
- `src/lib/mod_task.c` and `src/lib/mod_channel.c`.

The WASM HTTP backend remains `src/http/flat_http_fetch.c`; it does not link native libcurl. The synchronization abstraction remains the single-threaded Emscripten implementation unless Emscripten pthreads are explicitly enabled.

The two `plugin_staging_*` helpers in `native_plugin_host.c` are now compiled only when `FLAT_HAVE_DLOPEN` is available. WASM uses static plugin registration and does not need the dynamic-loader staging helpers, so this also removes the corresponding unused-function warnings.

## Validation

- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- `bin/run_tests 'flvm[32]' -v -e` — PASS.
- `flvm[32]` with a deliberately non-executable `pkg-config` earlier in `PATH` — PASS through the `-lcurl` fallback.
- `bin/run_tests 'flvm[31-38]' -v -e` — PASS (8/8).
- `bin/run_tests 'flvm[18]' -v -e` — PASS.
- `make -f Makefile.wasm -n ... fleditor-wasm` — PASS as a make/source-graph dry run; all newly required source files appear in the generated command.
- The actual `emcc` link was not run in this environment because `emcc` is not installed/on `PATH`. Therefore this archive does not claim a successful Emscripten binary build; the user should rerun `make fleditor-wasm V=1` with their Emscripten SDK.
- A full `bin/run_tests flvm` attempt exceeded the execution window while building/running the existing VM-owned lifecycle case; it did not report a failing test. The affected and newly relevant cases were rerun individually or as `flvm[31-38]`.

## Scope boundary

No pthread worker pool was introduced. No runtime scheduler or Channel semantics changed. This archive only restores build/source-list parity and makes the HTTP initialization test resilient to a broken `pkg-config` executable.
