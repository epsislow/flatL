# Native synchronization abstraction and invocation gates — archive 298

## Scope

Archive 298 implements the first native synchronization primitive and applies the existing module capability policy to native invocation. It does **not** implement worker threads, scheduler synchronization, cancellation requests, or a worker pool.

## Platform mutex abstraction

`include/flat_sync.h` provides the small `FlatMutex` abstraction used by process-wide native invocation gates:

- POSIX: `pthread_mutex_t` with `PTHREAD_MUTEX_INITIALIZER`.
- Windows: `SRWLOCK` with `SRWLOCK_INIT`.
- Single-threaded Emscripten: no-op mutex implementation. A future Emscripten pthread build uses the pthread branch when `__EMSCRIPTEN_PTHREADS__` is enabled.

The abstraction deliberately exposes only lock/unlock at this stage. Thread create/join and condition variables belong to the later worker-pool runtime layer and are not introduced here.

## Per-module invocation gates

Each built-in native module metadata entry owns one process-wide mutex. `flat_native_module_gate_enter()` and `flat_native_module_gate_leave()` address the gate by canonical module name.

`flat_native_invoke()` now resolves the native's canonical module and consults `flat_native_module_worker_policy()` before dispatch:

- `SERIALIZED` and audit `PENDING` (effective `SERIALIZED`) acquire that module's gate around the complete native dispatch.
- `THREAD_SAFE` does not acquire a module gate.
- `UNSUPPORTED` remains callable in the existing cooperative runtime; it is rejected by the worker-mode preflight. This preserves current cooperative behavior while keeping worker admission conservative.

The gate is per module, not global: a serialized `io` call does not block a serialized `dom` call. Gates are held only for one native invocation, never across a VM quantum or scheduler wait.

## DOM teardown boundary

The `dom` module is `PENDING`, so its effective policy is `SERIALIZED`. DOM native calls pass through the common `flat_native_invoke()` gate. `flat_dom_runtime_destroy()` also acquires the exact same `dom` gate while it destroys Lexbor documents. This closes the handler-only protection gap identified in archives 294–295.

The gate must not be nested with scheduler or Channel locks. The worker design already requires those locks to be released before native invocation and VM teardown. `flat_dom_runtime_create()` currently allocates only the FlatL wrapper and does not call Lexbor.

## Locking and lifecycle rules

- Capability metadata must be frozen before worker creation, so the selected policy cannot change while workers execute.
- Native handler registry/lifecycle locks are not held while the handler executes.
- The invocation gate is acquired outside the handler and released after it returns, including error returns.
- A handler must not suspend/yield while holding a module gate. Current native handlers are synchronous calls; future asynchronous native APIs must release the gate before suspension or use a different ownership protocol. A `SERIALIZED` handler must not recursively invoke another handler from the same module through `flat_native_invoke()`; the gate is deliberately non-recursive.
- The DOM teardown path uses the same gate as DOM handlers.
- Plugin loading/unloading remains setup-only; plugin unload is not supported while invocations may be active.

## Validation

`test/cases/flvm/036_native_invocation_gates/` verifies:

1. Eight pthread callers repeatedly enter the `dom` gate; the measured critical-section concurrency remains exactly one.
2. `io` and `dom` gates are independent; holding one does not prevent progress through the other.
3. `flat_dom_runtime_destroy()` blocks behind the `dom` gate and proceeds after it is released.

The test is run through `bin/run_tests 'flvm[36]' -v -e` and has passed normally, under ThreadSanitizer, and under AddressSanitizer/UndefinedBehaviorSanitizer. This is a focused mutex/gate test, not a complete-VM worker test and not proof that all native modules are thread-safe.

## Remaining work

- Add complete-VM DOM concurrency tests after scheduler and VM lifetime synchronization exist; retain DOM `PENDING` until then.
- Add worker-mode dispatcher admission/preflight at the worker boundary. The current invocation wrapper applies serialization policy but does not itself reject `UNSUPPORTED` modules in cooperative execution.
- Add worker-owned cancellation, shutdown, queue/condition-variable and runtime lifetime implementation.
- Audit blocking/long-running native handlers and ensure no future handler can suspend while holding a module gate.
- Extend sanitizer and platform build matrix, especially Windows and Emscripten pthread configurations.
