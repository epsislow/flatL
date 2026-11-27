# FlatThread Join Ownership Audit — archive 319

## Scope

Review the native join contract in `FlatThread` and the runtime-level ownership of a worker pool's thread handles. This continues the open item recorded by archive 318. The scope is deliberately limited to joining; it does not claim that `flat_runtime_destroy()` is safe against concurrent API entry.

## Findings

### POSIX

`pthread_join()` consumes the joinable thread only when it returns success. The abstraction keeps `FlatThread.valid` set on failure and clears it only on success. The native error code is returned unchanged. A failed `pthread_join()` is not universally retryable: the caller must not assume that every error is transient. In particular, invalid/nonexistent thread identifiers and competing join ownership indicate a violated ownership contract rather than a guaranteed retry opportunity.

### Windows

`WaitForSingleObject()` and `CloseHandle()` are separate operations. A successful wait establishes that the thread has terminated, but does not release the handle. The previous implementation ignored `CloseHandle()` failure and cleared `thread->native` unconditionally. The abstraction now reports failure and retains the handle value unless `CloseHandle()` succeeds. A retry may wait immediately on an already-signaled thread handle before attempting the close again.

### Runtime-level ownership

`flat_runtime_workers_join()` previously released `scheduler_mutex` before joining handles without publishing an exclusive join-operation owner. Two host threads could therefore enter the same runtime's join loop and concurrently call `pthread_join()` on the same `pthread_t`, which is not a supported POSIX ownership pattern.

A `workers_joining` claim is now published under `scheduler_mutex` before any native join call. A concurrent join caller for the same runtime is rejected with `-1`. If one or more native joins fail, the claim is cleared while all handles not successfully joined remain retained; a later caller can retry. On complete success, worker state and the claim are cleared together under the mutex.

The runtime destruction precondition also checks `workers_joining`. The public contract explicitly rejects concurrent join callers on one runtime and continues to require the host to prevent concurrent runtime destruction/API entry. Joins on distinct runtimes remain independent.

## Regression coverage

`flvm[42]` now starts a stopped 16-worker runtime and releases four host threads simultaneously to call `flat_runtime_workers_join()` on that same runtime. Exactly one caller must succeed; the other three must be rejected. The existing injected transient-join-failure tests continue to verify that a failed join releases the operation claim and permits a later retry.

## Validation (archive 319, local POSIX host)

- `make all -j2`: PASS.
- `flvm[42]`: PASS.
- Dedicated `flvm[42]` harness: 20 consecutive executions PASS.
- Windows-specific code was reviewed but not compiled or executed; no Windows cross-compiler was available.
- Actual Emscripten build remains unverified; `emcc` was unavailable.
- The complete 1185-test suite was not run.

## Remaining limitations

- The POSIX implementation has been exercised on the local Linux host; Termux confirmation remains for the user.
- Windows worker execution and handle-close failure behavior need validation on a Windows toolchain/runtime.
- Single-thread Emscripten is expected to reject worker startup through the existing unsupported thread abstraction; an actual Emscripten build remains open.
- Concurrent `flat_runtime_destroy()` with any other API call remains unsupported and must be prevented by the host.
