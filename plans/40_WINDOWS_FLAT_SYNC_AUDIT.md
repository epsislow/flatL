# Windows FlatSync / FlatThread audit — archive 328

## Scope and validation boundary

This checkpoint reviews the `_WIN32` branch in `include/flat_sync.h`, the ready-queue condition-variable call sites, and the runtime worker create/join ownership path. The available environment has no Windows SDK, MinGW cross-compiler, MSVC, or Windows runtime. Therefore this is a source audit plus a portable regression that can be compiled on Windows later; it is **not** Windows compilation or execution evidence.

## Findings and changes

### Failed `FlatThread` creation state

The Windows `flat_thread_create()` previously returned immediately for a null function, allocation failure, or `_beginthreadex()` failure without first establishing a non-joinable handle state. A caller that subsequently attempted to join the failed object could read an uninitialized `uintptr_t`. The POSIX branch had a similar null-function ordering issue because it returned before clearing `valid`.

Both threaded branches now clear their non-joinable marker before validating the function or attempting allocation/native creation. The documented precondition is that a `FlatThread` passed to create is fresh or has been successfully joined; create must not be used to overwrite a live/unjoined handle.

### Windows trampoline ownership

The Windows trampoline previously retained a pointer to the caller's `FlatThread` and wrote the function return value into a Windows-only `result` member. POSIX `pthread_join()` discards the function return value, and no caller reads this Windows-only field. The trampoline now owns only its heap-allocated `{fn,arg}` start record, invokes the function and discards its return value, matching the POSIX abstraction. This removes an unnecessary cross-thread write and avoids coupling the trampoline to the lifetime of the `FlatThread` wrapper object.

### Synchronization semantics reviewed

- `SRWLOCK` is used exclusively; its init/destroy operations are intentionally no-op/success wrappers around Windows' static-lifetime primitive semantics.
- `SleepConditionVariableSRW(..., INFINITE, 0)` releases and reacquires the exclusive SRW lock around the wait. The ready queue uses predicate loops under the mutex, so spurious wakeups are rechecked.
- `WakeConditionVariable` and `WakeAllConditionVariable` are called while the ready-queue mutex is held, matching the queue's predicate-update protocol.
- `_beginthreadex` is used rather than `CreateThread`, preserving CRT thread initialization for C runtime use.
- `WaitForSingleObject` and `CloseHandle` remain separate join steps. The wrapper clears the stored handle only after both succeed; same-handle join calls must be serialized by the owner.

### Windows SDK target requirement

`SRWLOCK`, `CONDITION_VARIABLE`, `SleepConditionVariableSRW`, and the wake APIs require Windows Vista / `_WIN32_WINNT >= 0x0600`. The source currently does not force a target macro. The Windows build configuration must define a compatible target (or rely on an SDK default that does); this remains an explicit build-system check for the first real Windows build. This archive does not silently raise the application's global Windows API target from a public header.

## Regression

Added `flvm[55]` (`test/cases/flvm/055_flat_thread_contract/`) and `make test-flat-thread-contract`:

- Poison-initialize a `FlatThread`, call create with a null function, then verify join rejects the failed-create object safely.
- Verify null wrapper arguments are rejected.
- Exercise mutex + condition-variable predicate wait/wakeup.
- Create and join one thread; verify a second join is rejected.
- Destroy synchronization objects after the worker has joined.

The Make target uses `THREAD_FLAGS ?= -pthread`; a native Windows GNU make invocation can set `THREAD_FLAGS=`. This regression is platform-neutral C source, but only its POSIX build/run was executed in this environment.

## Validation — archive 328

- `make test-flat-thread-contract` — PASS on local POSIX host.
- `bin/run_tests 'flvm[55]' -v -e` — PASS (1/1).
- `flvm[37]` — PASS (1/1); `flvm[42]` — PASS (1/1); `flvm[55]` — PASS (1/1).
- Dedicated `flvm[55]` harness under AddressSanitizer and UndefinedBehaviorSanitizer — PASS.
- Windows compile/run — NOT RUN: no Windows SDK/toolchain/runtime available.
- Actual Emscripten build — still NOT RUN: no `emcc`/`emcmake` available.

## Remaining platform gate

A Windows host/toolchain must compile and run at least `flvm[37]`, `flvm[42]`, and `flvm[55]`, with the intended `_WIN32_WINNT` setting. It must additionally exercise partial worker-start rollback, normal join, join retry behavior where injectable, DRAIN/CANCEL shutdown, and worker-mode native capability preflight. This source audit does not close the Windows gate or declare v2.0 complete.
