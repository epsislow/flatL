# Native Module Thread Capabilities

This page defines the native-module capability metadata used by the future FlatL worker mode. It does not claim that the current runtime or scheduler is thread-safe, and it does not enable pthread workers.

## Capability values

| Value | Meaning in worker mode |
|---|---|
| `THREAD_SAFE` | Calls may execute concurrently in independent VM contexts, subject to the module's documented host/dependency prerequisites. |
| `SERIALIZED` | Calls must be protected by one process-wide, per-module invocation gate. The gate covers the complete native handler call, but must not cover a VM quantum, task suspension, unrelated native calls, or blocking scheduler operations. |
| `UNSUPPORTED` | The module is rejected in worker mode. |
| `PENDING_THREAD_SAFETY_VALIDATION` | Audit status, not a capability value. The conservative worker policy is `SERIALIZED`. |

An unknown module or plugin without an explicit declaration is `UNSUPPORTED`. Registry locking only protects handler publication and lookup; it does not make handler internals safe.

## Plugin declaration ABI

The existing plugin initializer remains unchanged:

```c
int flat_native_plugin_init(FlatNativePluginRegisterFn reg);
```

A plugin may additionally export:

```c
FlatNativeThreadCapability flat_native_plugin_thread_capability(void);
```

The loader resolves this optional symbol after successful initialization. A missing symbol, invalid enum value, or unknown declaration is treated as `UNSUPPORTED` for worker-mode admission. This keeps old plugins compatible with the cooperative runtime while denying them worker-mode execution by default.

The declaration is module-level, not handler-level. All handlers in one plugin share dependencies and process-owned state, so per-handler declarations could give a misleading safety guarantee. Built-in optional plugins compiled as separate shared objects export the declaration explicitly. Static/fallback builds use the audited built-in module table.

## Built-in baseline

The current source-audit defaults are:

- `THREAD_SAFE` candidates: `json`, `str`, `date`, `clock`, `http`, `env`, `path`, `fs`, `enc`, `re`, `hash`, `bytes`, `mem`, `file`, `math.trig`, `math.stats`, `math.mdim`, `math`, `list`, `obj`, `vcx`, `executor`, and `csv`.
- `SERIALIZED`: `io`, because standard input/output/error are process-owned streams and output ordering is not otherwise defined.
- `PENDING_THREAD_SAFETY_VALIDATION`: `dom`. Its effective worker policy is `SERIALIZED` until complete-VM and supported-build validation chooses `THREAD_SAFE` or retains the serialized fallback. Any serialized implementation must also cover `flat_dom_runtime_destroy()` and VM teardown, not just `dom.*` calls.
- `SERIALIZED`: `task` and `channel`. Their runtime callbacks now publish task/wait-graph state under `scheduler_mutex`, and Channel state is protected by the Channel mutex. The per-module gate is an additional conservative serialization boundary.

These are admission declarations for a future worker runtime, not a blanket proof of complete-VM concurrency. HTTP additionally requires `flat_http_prepare()` before worker creation and a supported libcurl/resolver configuration. Process environment, plugin path, logger and host configuration must be frozen before workers start. PCRE2 and Lexbor still require the complete-VM/build-matrix gates documented in `plans/08_EXTERNAL_DEPENDENCY_AUDIT.md`.

## Runtime preflight and freeze

`flat_native_runtime_worker_policy()` prepares the requested native module, rejects `UNSUPPORTED` modules, and returns the effective `THREAD_SAFE` or `SERIALIZED` policy. Archive 305 calls VM manifest preflight before worker startup and before publishing new tasks while worker mode is active. For `SERIALIZED`, the common `flat_native_invoke()` boundary now acquires the per-module gate around the complete synchronous dispatch and releases it before returning to VM/scheduler logic. `PENDING` maps to `SERIALIZED` and is gated. `THREAD_SAFE` calls are not gated. `UNSUPPORTED` remains callable in the cooperative runtime when no worker pool is active for backward compatibility. Worker-mode VM manifest preflight rejects it before dispatch; the dispatcher also rejects it if reached through an unexpected/dynamic path.

`flat_native_runtime_freeze_worker_capabilities()` closes capability metadata changes. Worker-pool startup initializes the native registry and preflights all registered VM manifests, loading/configuring permitted plugins, then freezes declarations before creating the first worker. After freeze, the loader rejects a not-yet-loaded plugin and search-directory changes are ignored. Plugin unloading and search-path changes remain prohibited during worker execution.

## Invocation gates

Each canonical native module has an independent process-wide invocation mutex. `include/flat_sync.h` maps it to POSIX `pthread_mutex_t`, Windows `SRWLOCK`, or a no-op mutex for single-threaded Emscripten. The common `flat_native_invoke()` boundary consults the effective policy and gates `SERIALIZED`/`PENDING` calls. It does not gate `THREAD_SAFE` calls. Different serialized modules do not block one another.

DOM teardown is part of the same boundary: `flat_dom_runtime_destroy()` acquires the `dom` gate before destroying Lexbor documents. Protecting only DOM native handlers would leave a race during VM teardown.

The gate is held only around one synchronous native dispatch or bounded DOM teardown. It must not span a VM quantum, task suspension, scheduler wait, or blocking work performed outside the handler. Scheduler and Channel locks must be released before native dispatch and VM teardown. Native handlers must not yield while holding a module gate. A `SERIALIZED` handler must not recursively invoke another handler from the same module through `flat_native_invoke()`, because module gates are non-recursive.

## Current implementation boundary

Archive 297 added the enum, built-in module defaults, optional plugin capability symbol, conservative handling of legacy plugins, metadata freeze, and worker-policy preflight. Archive 298 adds per-module gates and DOM teardown protection. Archives 300–304 add scheduler/Channel synchronization, request-only cancellation and DRAIN/CANCEL lifecycle. Archive 305 adds the first runtime-owned worker pool, VM-manifest preflight, and invocation-time rejection of unsupported modules as defense in depth. This is not yet complete-VM thread-safety certification; see `plans/17_RUNTIME_WORKER_POOL.md`.
