# FlatL v2.0 — Native Module Thread-Safety Audit

**Checkpoint:** archive 292  
**Baseline inspected:** archive 291 source tree  
**Status:** source-level inventory and one concrete race fix; this is not a blanket thread-safety certification.

## 1. Scope and method

This audit covers the embedded native handler sources under `src/lib/mod_*.c`, the built-in optional plugin entry points under `src/lib/plugin_*_entry.c`, the native registry/plugin host, and the directly shared helpers used by those handlers. It records mutable file-scope state, process-owned services, VM/context-owned state, and external-library boundaries.

The audit is source inspection, not a proof that every third-party dependency is thread-safe. It also does not test simultaneous execution of two complete FlatVm instances: the current runtime scheduler and task APIs are still single-host-thread-only. The concurrency regression added here targets the one fixed process-global HTTP sequence primitive directly.

## 2. Native module inventory

| Native module/source | State ownership observed | Worker-mode assessment |
|---|---|---|
| `mod_bytes.c` | Per-call buffers and `FlatNativeCtx` pools | No mutable file-scope state found; eligible subject to runtime/allocator and external dependency audit. |
| `mod_channel.c` | Runtime Channel bridge; task ID and Channel ID resolve through the shared `FlatRuntime` | **Blocked** until runtime registry, Channel, wait graph and wakeup synchronization are implemented. |
| `mod_csv.c` | `ctx->csv`, VM file table and VM pools | No mutable file-scope state found; separate VM contexts are the intended isolation boundary. Same external file path still has normal OS-level shared-file semantics. |
| `mod_date.c`, `flat_date.c` | Per-VM date/run state; process environment is read at run initialization | No mutable module global found. Host process environment must be frozen while workers run. |
| `mod_dom.c` | DOM runtime supplied by `FlatNativeCtx` | No mutable file-scope state found. Lexbor/library-level concurrent-use behavior still needs dependency-level confirmation. |
| `mod_enc.c` | Per-call buffers; immutable lookup tables in common encoding helpers | No mutable file-scope state found. |
| `mod_env.c` | Per-VM `ctx->env`; process environment may be consulted by the context helper | Per-VM snapshots are isolated. Concurrent host `setenv`/`putenv`/`unsetenv` during worker execution is outside the contract. |
| `mod_executor.c` | VM-owned executor context and pools | No mutable file-scope state found; separate VM contexts are the intended isolation boundary. |
| `mod_file.c` | VM-owned file table/handles | No mutable file-scope state found. A file handle must not be shared between VMs; filesystem-level conflicts remain possible. |
| `mod_fs.c`, `mod_path.c` | Per-VM path context; process default separator is an atomic byte | Shared default reads/writes are atomic, but host must freeze path staging before pool start; VM path snapshots are preferred. |
| `mod_hash.c`, `mod_math.c` | Per-VM `ctx->rng` | RNG state is VM-owned (`FlatRngState`); no process-global RNG state was found in these handlers. RNG is not cryptographic. |
| `mod_http.c` | Per-VM `FlatHttpRuntime` request slots; process-wide boundary sequence now uses an atomic counter | The identified sequence race is fixed and stress-tested. Each request slot/easy handle remains single-owner; libcurl global initialization is separately guarded. |
| `mod_io.c` | Per-VM IO context plus process stdin/stdout/stderr | No mutable module global found. Standard stream writes may interleave between workers; output ordering is unspecified unless a future synchronized per-runtime sink is added. |
| `mod_json.c`, `mod_json_query.c` | Per-call parser/lexer/AST buffers and VM pools | No mutable file-scope state found. |
| `mod_list.c`, `mod_list_values_to.c` | Per-call/VM collection pools | No mutable file-scope state found. `FLAT_LIST_DEBUG` is read from process environment; freeze host environment during worker runs. |
| `mod_math_mdim.c` | Per-VM collection pools plus process-wide debug setting | Debug cache is protected by `atomic_flag`/`atomic_int`; safe as a one-time immutable process setting after initialization. Environment must be frozen before first use. |
| `mod_math_stats.c`, `mod_math_trig.c` | Per-call arguments and VM pools | No mutable file-scope state found. |
| `mod_mem.c` | VM-owned pools and native context | No mutable file-scope state found. |
| `mod_obj.c` | VM-owned object/collection pools | No mutable file-scope state found. |
| `mod_re.c` | Per-call PCRE2 code/match data and VM pools | No mutable file-scope state found. PCRE2 dependency behavior with independent contexts must remain in the external-library validation gate. |
| `mod_str.c` | Per-call buffers and VM pools | No mutable file-scope state found. |
| `mod_task.c` | Shared runtime task APIs through native bridge | **Blocked** until worker-safe task registry, queue, wait graph and cancellation protocol exist. |
| `mod_vcx.c`, `mod_vcx_dschema.c` | VM-owned `FlatVcxRuntime` and pools | No mutable file-scope state found; independent VM contexts are the intended boundary. |

The built-in optional plugin entry files (`plugin_math_entry.c`, `plugin_math_trig_entry.c`, `plugin_math_stats_entry.c`, `plugin_math_mdim_entry.c`, `plugin_list_entry.c`, `plugin_obj_entry.c`, `plugin_vcx_entry.c`, `plugin_csv_entry.c`) only register handler functions; no mutable entry-point state was found. This does not certify arbitrary plugin code loaded from disk.

## 3. Mutable process-wide state inventory

| State | Location | Synchronization / ownership | Remaining condition |
|---|---|---|---|
| Native handler table and embedded flags | `src/lib/native_plugin_host.c` | `g_registry_lock` | Handler invocation is outside the lock; handler internals require their own classification. |
| Plugin initialization, search path, lazy-load flags | `src/lib/native_plugin_host.c` | `g_runtime_lock`; registration publication uses `g_registry_lock`; staging arrays are `_Thread_local` | Configure paths before pool start; prohibit plugin load/unload and search-path mutation while workers run. |
| Numeric C locale | `src/common/flat_locale.c` | One-time atomic initialization; immutable locale handle, selected per calling thread with `uselocale` on POSIX | Do not call process-global `setlocale()` during worker execution. |
| Environment staging map | `src/common/flat_env.c` | `g_env_lock` protects map and snapshots | Process `environ` itself is not owned by this lock; host must not mutate it concurrently. |
| Default path separator | `src/common/flat_path.c` | Atomic byte; VM contexts snapshot it | Freeze process staging before pool start. |
| Runtime logger | `src/common/flat_runtime_log.c` | `g_logger_lock` protects config, sequence, buffer and writes | Configure/shutdown outside pool lifetime; stdout ordering across different writers remains unspecified. |
| Memory-stream fallback registry | `src/common/flat_memstream.c` | Atomic lock protects registry lookup/list only | Each FILE stream and caller output pointers remain single-owner. GNU `open_memstream` path has no FlatL registry. |
| Windows monotonic timer frequency | `src/common/flat_time.c` | Windows `InitOnceExecuteOnce` | Immutable after initialization. |
| Multidimensional-list debug cache | `src/lib/mod_math_mdim.c` | Atomic state + initialization flag | Read once; freeze process environment before first use. |
| HTTP multipart sequence | `src/common/flat_http_boundary.c` | `atomic_uint_fast64_t` fetch-add | Fixed in archive 292; regression covers concurrent sequence allocation. |
| libcurl global initialization | `src/http/flat_http_curl.c` | Atomic initialization state + spin gate | No `curl_global_cleanup()` path is present; easy handles and transfer state are per call. |
| WASM bridge result/limits/panic buffers | `src/fleditor_wasm/flat_wasm.c` | Mutable singleton globals; no instance lock | WASM remains cooperative and is not worker-pool capable. |

`src/common/flat_rng.c` has no process-global RNG state: the mutable RNG state is the `FlatRngState` passed by the VM/native context. Immutable lookup tables and `static const` data are not mutable shared state.

## 4. HTTP multipart sequence race fix (archive 292)

Before this archive, `http_ensure_boundary()` incremented a function-static `unsigned long seq` with an ordinary `seq++`. Two VMs building multipart requests concurrently could race on that counter.

The sequence allocator is now isolated in:

- `include/flat_http_boundary.h`
- `src/common/flat_http_boundary.c`

It uses `atomic_uint_fast64_t` and `atomic_fetch_add_explicit(..., memory_order_relaxed)`. Relaxed ordering is sufficient because the counter only allocates unique sequence values; it does not publish or protect any other memory. `mod_http.c` uses the returned sequence in its request-local multipart boundary. The request slot and its boundary string remain owned by the individual VM's `FlatHttpRuntime`.

A new C regression is integrated into the existing `bin/run_tests` framework as `flvm[31]`. Eight pthread callers allocate 5,000 values each (40,000 total); the test verifies every value is in range, unique, and present. Existing HTTP multipart cases `http[28-32]` also verify that the refactored boundary path still works in the actual native module.

This is a targeted primitive-level concurrency test. It is not a test of two complete VMs or the worker scheduler.

## 5. Native/plugin concurrency policy for worker mode

The source inventory does not justify marking every native handler `THREAD_SAFE` automatically. Worker mode needs explicit capability metadata with these semantics:

- `THREAD_SAFE`: handler may execute concurrently in distinct VM contexts; its dependencies and process-owned resources must also satisfy that guarantee.
- `SERIALIZED`: calls are serialized by a per-module invocation gate. The gate is held only for the handler call and must not be nested with the runtime scheduler or Channel mutex.
- `UNSUPPORTED`: handler is rejected when invoked from worker mode.

Until metadata and invocation gates exist, worker mode must reject unknown third-party plugins (`UNSUPPORTED` by default). Do not silently serialize arbitrary unknown code and claim safety: a plugin can call process-global host APIs, share state with another module, or retain pointers beyond the handler call. An explicit plugin declaration is required before it can opt into `THREAD_SAFE` or `SERIALIZED`.

For built-ins, `task` and `channel` remain blocked by the runtime synchronization work. `io`, `env`, `path`, `file`, `fs`, `dom`, `re`, and `http` require their documented host/dependency/output constraints to be satisfied before they can be enabled in a worker-mode allowlist. The pure/context-local math, list, object, string, JSON, encoding and hash handlers are candidates for `THREAD_SAFE`, but must still pass multi-VM tests and sanitizer runs before receiving that classification.

## 6. Host contract and residual risks

Before worker start, the host must freeze process environment mutation, plugin search paths, native registration, logger configuration, legacy path staging and other process-wide configuration. Plugin unload is prohibited while any VM can invoke a handler. A caller-owned FILE stream, callback target, file handle, DOM context, CSV session or request slot must not be shared across VMs unless its owner supplies an explicit synchronization contract.

The audit did not inspect every third-party library implementation or every user-supplied plugin. It did not prove the runtime scheduler, task APIs, Channel registry, wait graph, cancellation, VM destruction, or runtime teardown safe across host threads. Those remain separate worker-pool blockers from `06_WORKER_POOL_SPEC.md`.

## 7. Acceptance status

- [x] Inventory mutable file-scope state in embedded native modules and built-in optional plugin entry points.
- [x] Identify process-owned helper state and classify its current synchronization.
- [x] Fix the unsynchronized HTTP multipart sequence.
- [x] Add a pthread regression for concurrent sequence allocation.
- [x] Run actual HTTP multipart regression cases.
- [x] Run the boundary allocator regression under ThreadSanitizer and AddressSanitizer/UndefinedBehaviorSanitizer.
- [ ] Audit/approve external dependency guarantees (Lexbor, PCRE2, libcurl and platform C runtime behavior) for the supported build matrix. Lexbor is `PENDING_THREAD_SAFETY_VALIDATION`; prefer `SERIALIZED` if independent contexts cannot be certified.
- [ ] Add plugin capability metadata and worker-mode enforcement.
- [ ] Add two-complete-VM concurrent native-handler tests after the worker scheduler exists.
- [ ] Run ThreadSanitizer and AddressSanitizer/UndefinedBehaviorSanitizer on the full runtime/worker test matrix after worker implementation.

**Exit rule:** the audit reduces known native-side races but does not remove the runtime scheduler blockers and does not authorize pthread worker execution.


## 8. External dependency follow-up (archive 293)

The external dependency review is recorded in `08_EXTERNAL_DEPENDENCY_AUDIT.md`. Summary: bundled PCRE2 10.49 without JIT is a strong candidate because FlatL creates independent pattern/match-data objects per call; libcurl uses per-request easy handles but requires pre-initialization and `CURLOPT_NOSIGNAL=1` for threaded operation; Lexbor 3.0.0 uses per-VM documents and is `PENDING_THREAD_SAFETY_VALIDATION`, not `UNSUPPORTED`. If independent-context concurrency cannot be certified, use one process-wide `SERIALIZED` DOM gate covering both native handlers and Lexbor-backed VM teardown. Lexbor's process-global allocator setup must remain frozen.

Archive 293 adds `flat_http_prepare()` (native libcurl global initialization; WASM no-op) and sets `CURLOPT_NOSIGNAL` on each native easy handle. These changes improve the transport's worker preflight contract but do not certify HTTP or the VM scheduler for worker mode.


## 8. Archive 294 — DOM capability status

Lexbor/DOM is classified `PENDING_THREAD_SAFETY_VALIDATION`. The eventual native capability remains `THREAD_SAFE`, `SERIALIZED`, or `UNSUPPORTED`; pending is an audit state only. Prefer `THREAD_SAFE` if complete-VM independent-document concurrency and sanitizer tests pass for supported Lexbor builds. Otherwise prefer `SERIALIZED` with one process-wide gate around every Lexbor call, including `flat_dom_runtime_destroy()` during VM teardown. A handler-only gate is incomplete. `UNSUPPORTED` is reserved for a demonstrated constraint that cannot be contained by independent contexts or the gate. No worker-mode capability is enabled by this checkpoint.


## 9. Archive 295 — Lexbor independent-context concurrency evidence

`flvm[33]` directly compiles the bundled Lexbor 3.0.0 amalgamation and stresses eight independent pthread contexts. Each thread owns its document, CSS parser, selector engine, callbacks and buffers; 2,000 total document lifecycles cover parse, CSS selection, attribute mutation/readback, serialization and destruction. The test passed normally, under ThreadSanitizer, and under AddressSanitizer/UndefinedBehaviorSanitizer. Existing FlatL `dom[1-18]` integration tests also pass.

The `src/lib/mod_dom.c` audit found only file-scope `static` functions, not mutable static data. `FlatDomRuntime` and its document/node tables are VM-owned. The process-global Lexbor allocator function pointers are initialized to the default C allocators and FlatL does not call `lexbor_memory_setup()`; hosts must not reconfigure them while DOM is active.

This result does not test simultaneous complete FlatL VMs, system-Lexbor builds, scheduler/task/Channel concurrency, or concurrent host-triggered VM teardown. Keep DOM `PENDING_THREAD_SAFETY_VALIDATION`. `THREAD_SAFE` remains a candidate; `SERIALIZED` remains the fallback and must include both native DOM calls and document destruction. Do not interpret this test as enabling DOM in worker mode.

## Archive 296 — plugin capability enforcement remains a pre-worker gate

Archive 296 defines cancellation-request and shutdown behavior but does not yet implement plugin capability metadata. The previously proposed capability classes remain `THREAD_SAFE`, `SERIALIZED`, and `UNSUPPORTED`; plugins without explicit metadata remain denied in worker mode by default. The next architecture step is to settle metadata ownership, registration defaults, per-plugin serialization scope, and behavior when a plugin attempts to change capability after worker startup. No worker-mode plugin is enabled by this archive.


## 6. Capability metadata implementation — archive 297

The capability model is now represented in code rather than existing only in this audit:

- `include/flat_native_capability.h` defines `FlatNativeThreadCapability`.
- `src/lib/native_capability.c` owns the process-wide module table. Unknown module names resolve to `UNSUPPORTED`; `PENDING` resolves to the conservative worker policy `SERIALIZED`.
- Capability declarations can be overridden before startup and then frozen. A future worker-pool startup must initialize the native registry and load/configure all permitted plugins first, call `flat_native_runtime_freeze_worker_capabilities()`, and only then create workers. After freeze, lazy loading of a not-yet-loaded plugin is rejected and search-directory changes are ignored.
- Plugins retain the current `flat_native_plugin_init(reg)` ABI and may export `flat_native_plugin_thread_capability()`. The loader treats a missing or invalid symbol as `UNSUPPORTED`. All bundled optional plugin entrypoints now export an explicit declaration.
- `flat_native_runtime_worker_policy()` is the preflight API: it resolves/prepares the module, rejects unsupported worker use, and returns `THREAD_SAFE` or `SERIALIZED`.

The baseline table declares `io` serialized; `dom` pending with serialized fallback; `task` and `channel` unsupported until scheduler synchronization; and context-owned modules as thread-safe candidates subject to their external dependency and complete-VM gates. HTTP still requires `flat_http_prepare()` before worker creation and a supported libcurl resolver/backend.

**Not implemented yet:** a per-module mutex gate and integration of the preflight into a worker dispatcher. In particular, a serialized DOM gate must protect both native DOM calls and `flat_dom_runtime_destroy()` during VM teardown. Do not interpret a `SERIALIZED` preflight result as if the handler is already locked.

## Archive 298 — invocation-gate implementation

The capability metadata now has an execution boundary for serialized calls:

- `flat_native_invoke()` resolves the native's canonical module and acquires that module's process-wide gate for effective `SERIALIZED` policy. Audit `PENDING` maps to `SERIALIZED`, so DOM calls are gated.
- Each module has its own gate. The implementation does not serialize all native calls behind one process-wide lock.
- `THREAD_SAFE` calls do not acquire a gate. `UNSUPPORTED` remains available to the cooperative runtime but is rejected by worker preflight; the worker scheduler must perform admission before a task is dispatched.
- `flat_dom_runtime_destroy()` acquires the same DOM gate before Lexbor document destruction, covering the teardown path outside the native dispatcher.
- The gate is held for one synchronous native call or bounded DOM teardown only. No scheduler/Channel lock may be held while entering it, and a native handler must not suspend while holding it.

`flvm[36]` validates per-module exclusion, independence between module gates, and DOM teardown protection. It is not a complete-VM concurrency test. Keep DOM `PENDING_THREAD_SAFETY_VALIDATION` and keep worker creation disabled until scheduler, task/VM lifetime and shutdown protocols are implemented.
