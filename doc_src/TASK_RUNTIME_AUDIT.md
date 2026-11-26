# Task runtime — initial VM audit

**Baseline:** `flatL-main(57)` archive. **Execution-context update:** archive 254. This is an architectural audit, not a task implementation.

## Findings

### VM state lifetime

`flat_vm_run_opts()` creates a local `VmState st` and initializes the runtime for one complete run. The interpreter loop operates on that stack-owned state and all normal termination paths call `vm_finish()` / `vm_teardown()`.

This is suitable for the current one-shot VM invocation, but it is not yet a resumable task object. A cooperative task implementation will need a persistent execution state and an explicit suspend/resume boundary. Simply invoking `flat_vm_run_opts()` again cannot resume an instruction pointer, call stack, loop frames, or native execution state.

### Executor state

`src/common/flat_executor.c` originally owned process-global state (`g_bind`, `g_bind_ok`, `g_frames`, `g_frame_sp`). In archive 253 these were moved into `FlatExecutorContext`, owned by `VmState`. Native executor operations and panic drain/unwind receive that context explicitly.

### VM call-instruction state

In archive 253, `src/common/vm.c` still used process-global call-instruction bookkeeping (`g_vm_call_instr`, `g_instr_stack`, `g_instr_push_call_sp`, `g_last_call_function_sp`). In archive 254 these fields were moved into `FlatExecutorContext`; the resolver now receives `VmState *` explicitly, and `FlatNativeCtx.call_instr_id` carries the resolved ID to native calls.

### VCX active runtime

Archive 253 still stored `g_vcx_active` and `g_call_instr_resolver` globally. Archive 254 removes both. Native VCX operations use `FlatNativeCtx.vcx`, and call-instruction access uses `FlatNativeCtx.call_instr_id`; there is no process-global active VCX fallback.

### Other global runtime state

The VM run setup calls `setlocale(LC_ALL, "C")` and `flat_native_runtime_reset()`. The process-global RNG was moved into `VmState` in archive 255 and is passed to native calls through `FlatNativeCtx.rng`. Remaining mutable globals include the native plugin registry/reset state, date run state, path separator, environment state, memory-stream slots, runtime logger, compiler panic-site emission state, and WASM last-result state. These must be classified by ownership before the parallel worker-pool milestone. The initial cooperative runtime must use a clearly defined single-thread execution model and must not claim thread safety.

### Panic handling

`vm_jump_panic_any()` currently:

1. uses the current VM-owned executor context;
2. drains executor frames and pending call instruction contexts scoped to that context;
3. resolves the panic location and records the caught panic;
4. looks for `#panic.<loc>` and then `#panic.any`;
5. marks the panic unhandled if no handler exists.

The executor and call-instruction drains are now scoped to the failing VM context. This does not yet prove task-level isolation because there is not yet a persistent task scheduler or a fully audited native runtime.

## Initial implementation decision

The first foundation change adds an opt-in central runtime logger and documents the global-state constraints. It deliberately does not introduce task scheduling, Channel operations, `.any` panic resolution changes, or worker threads. Those require the per-task execution-context refactor and dedicated semantic tests.


## F0.3 progress — execution context (archive 254)

The executor binding/frame stack and VM call-instruction bookkeeping are owned by `FlatExecutorContext`, which is embedded in `VmState`. The call-instruction resolver receives the owning `VmState *` explicitly. `FlatNativeCtx` receives the owning VCX runtime and the resolved `call_instr_id`; no process-global active VM, active VCX, or call-instruction resolver remains.

The following globals were removed: `g_bind`, `g_bind_ok`, `g_frames`, `g_frame_sp`, `g_vm_call_instr`, `g_instr_stack`, `g_instr_push_call_sp`, `g_last_call_function_sp`, `g_vcx_active`, and `g_call_instr_resolver`.

Other process-global mutable state remains, including native runtime reset, RNG, locale, and potentially other native subsystems. Therefore this refactor does **not** make the VM reentrant, task-safe, or thread-safe. Audit those remaining systems before introducing worker threads.

Validation on archive 254: `make all -j2` PASS; `make docs` PASS; `flvm` 7/7 PASS; `func` 37/37 PASS (run as `func[1-37]` and `func[101-106]` because IDs are sparse); `fmod` 18/18 PASS; `vcx` 30/30 PASS. The full suite was not run.

## F0.4 progress — per-VM RNG (archive 255)

`FlatRngState` now owns the SplitMix64 state and is embedded in `VmState`. `FlatNativeCtx.rng` passes the owner to native calls. `math.seed`, `math.random`, and `hash.randomBytes` share one RNG within a VM but no longer share state across VM contexts. The RNG is not cryptographic.

The new `flvm` case 008 checks independent deterministic RNG contexts, including interleaved reads and reseeding one context without changing the other. Validation on archive 255: `make all -j2` PASS; `flvm` 8/8 PASS; `math` 34/34 PASS; `hash` 4/4 PASS. The `bytes` suite was attempted but exceeded the execution limit after reporting test 11; it is not marked PASS.

## F0.4 progress — native plugin registry and per-VM manifest (archive 257)

The native plugin host no longer keeps a process-global program manifest. `FlatNativeRuntimeContext` holds the manifest pointer/count for one VM; `VmState` owns it and `FlatNativeCtx.native_runtime` carries it into native dispatch. `flat_io_exec_line_manifest()` also creates a temporary context from its explicit manifest arguments, preserving the same authorization scope during nested native evaluation.

`flat_native_runtime_reset()` has been replaced with `flat_native_runtime_init()`, which registers embedded core handlers once per process. The process-wide handler table, loaded-module flags, and plugin search directory are retained between VM runs. Optional plugin handlers are cached after loading, but the owning VM's manifest is still checked before a cached optional handler can be reused. Embedded core handlers are marked separately during initialization so existing core operations such as `mem.vars_max()` do not incorrectly require optional-plugin authorization.

Regression coverage: `flvm` case 009 checks independent manifests; `native`, `native_use`, `fmod`, `math`, `list`, `obj`, and `csv` suites validate the plugin/core call paths.

This is an ownership/lifecycle improvement, not a thread-safety claim. First-time registry initialization and lazy plugin loading mutate process-wide state without synchronization. Concurrent VM execution remains unsupported until those operations and other process globals are frozen or synchronized.

## F0.4 progress — locale-independent numeric conversion and stdio setup (archive 258)

The VM no longer calls `setlocale(LC_ALL, "C")`. That call changed the embedding application's process locale as a side effect of executing a FlatL program. Numeric conversion is now routed through `include/flat_locale.h`:

- `flat_strtod()` parses with the C numeric locale without changing the process locale. POSIX temporarily selects a shared immutable C numeric locale for the current thread with `uselocale()`; Windows uses `_strtod_l()`.
- `flat_snprintf()` formats numeric values under the C numeric locale. POSIX uses the same thread-local locale boundary; Windows uses `_vsnprintf_l()`.
- WASM uses the standard C conversion/formatting functions because its runtime uses the C numeric locale.

The helpers are used by VM numeric parsing, value/typed-number parsing, math expressions, date numeric inputs, JSON query numbers, number formatting, JSON serialization, HTTP form serialization, and `str.from_num`. The C locale object is shared and immutable after atomic one-time initialization; POSIX applies it only to the calling thread with `uselocale()`.

`flat_port_stdio_setup()` was also removed from `flat_vm_run_opts()` and moved to the `flvm` and `flatl` CLI process entry points. It is process-level setup (not VM state); embedding hosts that need Windows text-mode stdio must perform that setup once themselves.

Validation on archive 258: `make all -j2` and `make docs` PASS; `flvm` 9/9, `math` 34/34, `json` 50/50, `date` 82/82, `io` 53/53 passed across three bounded runs (the middle run timed out after 24 of its 25 cases; the final case passed separately), `values` 9/9, `str` 16/16, `http` 32/32, and selected `types` cases 82/82. The full default suite was not run. A non-C host locale was not available/verified in this run, so that specific integration scenario remains to be tested on a host with an installed locale such as `de_DE.UTF-8`.

The environment override/argv state, path separator, fallback memory-stream slot registry, and runtime logger remain process-owned mutable state. Plugin registry lazy loading also remains unsynchronized. These are still blockers for claiming concurrent VM execution or worker-pool readiness.


## Archive 259 — environment/path ownership and memory streams

Environment overrides and program arguments are copied into a per-VM `FlatEnvContext`; native `env.*` operations read that snapshot. The host process environment remains process-owned and must not be mutated during a VM run. Path separator configuration is copied into a per-VM `FlatPathContext`; native `path.*` and `file.sep` use the VM snapshot rather than the mutable process staging value.

The portable fallback memory-stream registry is dynamically allocated and uses an atomic-flag lock for registry insertion/removal. It no longer has a 16-stream cap. Runtime logger state remains process-owned and unsynchronized; it is not yet suitable for concurrent VM execution.


## Archive 260 — logger and compiler context

The four known failing env/path fixtures were repaired without changing runtime behavior. They now issue the native operation as its own instruction with an explicit output binding (`env.get(...) -> p`, `env.has(...) -> p`, `path.normalize(...) -> p`). Wrapping a panic-producing native inside `io.outln(...)` caused the outer IO argument evaluator to mask the expected native panic detail. The repaired cases are env 004, 008, 009 and path 006; the full env and path suites now pass.

The process-owned runtime logger now serializes access to its configuration, sequence counter, bounded buffer, dropped counter, flush and shutdown with a C11 atomic-flag lock. This protects logger state only; it does not make VM execution generally thread-safe.

Compiler emission state was moved into a stack-local `FlatCompileContext` per `flat_compile_fgir()` invocation. This replaces the mutable globals for schema catalog access, panic-site collection and current emitted instruction location/IP. The compiler no longer shares those mutable fields between concurrent compile invocations.

The legacy WASM last-termination/last-panic getters remain module-singleton state for the current single-instance FLEditor bridge. They are not an independent-result API for concurrent WASM instances. Native plugin lazy loading and process-level env/path staging also remain outside the per-VM ownership boundary.

Validation: `make all -j2` PASS; `env path flvm flc panic` 44/44 PASS. The full 1147-test default suite was not run. General VM reentrancy and worker-pool readiness are not claimed.

## Archive 261 — native plugin registry synchronization

The native plugin host serializes process-wide initialization and lazy loading with a lifecycle lock, and protects handler publication/lookup with a separate registry lock. The handler pointer is copied under the registry lock and invoked after all locks are released. Plugin initialization stages registrations per thread and commits them only after a successful initializer return; failed initialization discards staged pointers before unloading the library. Embedded registration marking is thread-local.

`FLAT_NATIVE_PATH` is captured on the first optional plugin load. Configure it before the first optional load. `flat_native_runtime_set_search_dir()` updates are synchronized with lazy loading. The host must not mutate the process environment concurrently with the initial snapshot.

`flvm[13]` exercises eight concurrent callers racing through runtime initialization, search-directory updates, and optional math plugin preparation, then verifies that a different VM manifest remains unauthorized after the plugin is loaded. This protects registry lifecycle and per-VM authorization; it does not prove arbitrary VM execution is thread-safe.

## Archive 262 — transversal global-state audit

The source audit now includes function-local mutable statics, not only file-scope `g_*` variables. Several first-use races were fixed: libcurl global initialization is serialized; Windows QPC frequency uses `INIT_ONCE`; the math multidimensional debug setting is atomically cached; native fallback signature text and the WASM module-list buffer are thread-local; and the Windows temporary shell-script sequence uses `InterlockedIncrement`.

The process environment override/argv staging object is now protected by a lock, including the deep-copy operation that creates each VM's `FlatEnvContext`. This guarantees that one VM receives a consistent snapshot while another host thread updates staging. It does not synchronize external host calls to `setenv()` / `putenv()`; the embedding host must keep the process environment immutable while VMs are running. The legacy `flat_env_arg()` returns a borrowed pointer that is invalidated by a subsequent staging mutation.

The process path separator is now an atomic byte. Each VM still snapshots it into its own `FlatPathContext`, so later staging changes do not affect a running VM.

Important remaining singleton constraints are now explicit:

- The legacy WASM last-termination/last-panic getters and run limits are module-global.
- WASM environment and path setters configure one shared module staging state.
- WASM compilation uses fixed `/fleditor_src.fl` and `/fleditor_out.fb` paths, and execution changes the module working directory to `/fleditor`; concurrent WASM compile/run calls are not supported.
- Process environment reads, plugin-defined globals, and any host-side mutation of environment variables remain outside FlatL's synchronization boundary.

New regression `flvm[14]` concurrently updates environment/path staging while other threads create VM snapshots. It verifies each environment snapshot is internally consistent and each path snapshot observes a valid separator.

Validation for archive 262: `make all -j2` PASS; `make docs` PASS; `flvm` 14/14; `env` 14/14; `path` 9/9 across two bounded ranges; `http` 32/32 across four bounded ranges; `math_mdim` 51/51; `port` 1/1. The full 1149-test default suite was not run. Windows-specific code was not built/tested in this Linux environment.

**Readiness:** the audit fixes several shared-initialization races but does not establish general VM reentrancy or thread safety. The WASM bridge remains single-instance, process environment must remain stable, and parser/linker plus plugin-owned state need further audit. No scheduler or worker pool is introduced here.

## Archive 263 — parser/linker and host configuration

The parser and linker use invocation-owned state: parse frames, current block/schema/function indices, import graph, module records, fmod records, merge indices, and temporary parsed programs are local to each call. No mutable file-scope or function-local static state was found in `parse.c` or `link.c`. The larger linker import queue and module/fmod workspaces were moved from fixed stack arrays to per-call heap allocations with centralized cleanup, reducing stack pressure for future worker threads.

FlatL source syntax is ASCII-defined. Parser, linker, function, schema, native-use, label, slot, numeric, and literal syntax helpers now use `flat_ascii.h` instead of locale-sensitive `ctype` classification. This prevents an embedding application's `setlocale()` from changing which bytes the compiler treats as whitespace or identifier characters.

### FlatHostConfig

New `include/flat_host_config.h` groups an independently owned `FlatEnvContext` and `FlatPathContext`. `flat_host_config_init()` creates a clean configuration with platform defaults; `flat_host_config_capture_staging()` snapshots the legacy process-level environment/argv and path settings; the host must not mutate either legacy staging source during this capture. Configure `config.env` through `flat_env_context_*` and `config.path` through `flat_path_context_set()`.

Pass the configuration using `FlatVmOptions.host_config`. At VM startup, FlatL deep-copies the environment context and copies the path context into `VmState`; the VM does not borrow the configuration during execution. A host config can be reused across runs while immutable. Do not mutate or destroy it concurrently with a call to `flat_vm_run_opts()`. If `host_config` is omitted, the existing CLI/WASM process-staging path remains unchanged.

Parser/linker concurrency still requires distinct caller-owned `FgirProgram` outputs, stable source files, and a stable process working directory. Relative entry paths depend on the current directory; hosts should prefer absolute paths and must not call `chdir()` concurrently with parsing/linking. Verbose diagnostics may interleave on process `stderr`.

Archive 263 validation: `make all -j2` and `make docs` PASS; `flvm[10]`, `flc`, and `native_use` 10/10 PASS; selected `types` and `func` cases 15/15 PASS. Broader test commands reached the execution timeout after printing passing cases and are not counted as complete. The full default suite was not run. This work does not declare general VM thread safety or worker-pool readiness.

## Archive 264 — runtime instance lifecycle foundation

Added opaque `FlatRuntime` (`include/flat_runtime.h`, `src/common/flat_runtime.c`). A runtime owns a deep copy of a supplied `FlatHostConfig`; when no config is supplied it captures the legacy process staging values at creation. The caller may then destroy or modify its source config without changing the runtime's snapshot.

`flat_runtime_run()` forwards the requested VM options while making the runtime-owned host configuration authoritative. Each VM still snapshots that configuration into its own `VmState`; the runtime does not own VM memory, bytecode, executor state, or task state. Multiple calls may execute against one immutable runtime configuration concurrently, subject to the existing VM/native subsystem limitations. Caller-owned output buffers in `FlatVmOptions` must remain distinct and valid for each concurrent call.

The runtime tracks active calls. `flat_runtime_destroy()` refuses to destroy an instance while a run is active. The host must externally prevent new API calls from starting concurrently with destruction; passing a freed runtime pointer is outside the API contract. This is a lifecycle guard, not a claim that all VM execution paths are thread-safe.

New `flvm[15]` stubs the VM boundary to verify that `flat_runtime_run()` passes the owned config snapshot, preserves the runtime during an active run, rejects destruction while active, and allows destruction after the call completes. It also verifies creation from legacy staging. New `flvm[16]` links the real VM and executes `001_min_ok/min_ok.fb` through `flat_runtime_run()`, verifying the public API-to-VM boundary end to end.

Validation for archive 264: `make all -j2` PASS; `make docs` PASS; `flvm` 16/16 PASS, including the real VM integration test. The full default suite (1151 discovered tests) was not run. Windows/WASM builds and actual concurrent VM execution are not validated here. `FlatRuntime` is a foundation for explicit host/runtime ownership; no scheduler, task API, yield/resume, Channel, or worker pool is introduced.

## Archive 265 — runtime resource ownership

The resource audit confirms that `FlatRuntime` currently owns only its immutable `FlatHostConfig` snapshot. The mutable CSV/DOM/VCX/HTTP/IO/file/pool/executor/RNG/date resources remain owned by each `VmState` invocation because they contain mutable handles and references into other VM-owned structures. Bytecode and VM output buffers remain caller-owned. Native handlers and loaded plugin libraries, logger, locale, libcurl initialization, and the fallback memory-stream registry remain process-owned; destroying one runtime must not unload or shut down those shared services.

Added `flvm[17]`: the test deliberately causes an opcode error after VM setup, verifies the error termination and diagnostic, restores the caller-owned bytecode, then successfully executes the same bytecode through the same runtime. The second run must not inherit the first run's diagnostic. This validates the failed-run teardown/reuse boundary, not general thread safety or persistent VM behavior.

Validation for archive 265: `flvm[15-17]` 3/3 PASS; `make all -j2` PASS; `make docs` PASS (45 sections). The full default suite (1152 discovered tests) was not run. The next design step may introduce a persistent VM/task object, but resources should move out of `VmState` only when their suspend/resume lifetime is explicitly specified.
