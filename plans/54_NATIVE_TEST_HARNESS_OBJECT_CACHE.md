# Native Test Harness Object Cache

## Problem

The FLVM integration cases build small C harnesses through individual `make test-*` targets. Most targets passed the same `FB_SRC`, `VM_SRC` and `PORT_SRC` source lists directly to the compiler together with one test-specific C file. Since `bin/run_tests` invokes those targets independently, the compiler repeatedly rebuilt the same large native runtime source set for every case.

This duplicated compile work was concentrated in the runtime, VM, task, Channel and worker-pool integration cases. The stress tests themselves remain intentionally unchanged; their execution time is real concurrency coverage and must not be reduced to make the suite appear faster.

## Design

The Makefile now compiles the shared `FB_SRC + VM_SRC + PORT_SRC` translation units into reusable object files under:

```text
bin/test-obj/<configuration-key>/...
```

Each harness target still compiles its own test source and links the same common runtime objects. The cache key is derived from the compiler, preprocessor/compiler flags, optional-module compile flags, and enabled feature configuration. A Makefile checksum is included so changes to the object-build recipe invalidate the cache. This keeps sanitizer and feature-specific builds isolated rather than reusing objects compiled with incompatible flags.

Compiler-generated `.d` files track header dependencies. `make clean` already removes `bin/`, so it also clears the object cache. Repeated recursive `make test-*` calls do not touch a shared stamp and therefore do not force unnecessary recompilation.

`test-runtime-worker-pool` is the one exception to the common object set: `FLAT_RUNTIME_TESTING` changes `src/common/flat_runtime.c`, so that target links a dedicated `flat_runtime_testing.o` while reusing all other common objects. The ordinary `flat_runtime.o` is excluded from that link to avoid duplicate symbols and preserve the test-only hooks.

The `test-emscripten-no-pthreads` target remains separate because it compiles the source set with `__EMSCRIPTEN__` and without pthread flags. Small standalone tests such as scheduler-sync and port shell-signal also remain independent.

## Scope

Updated the 34 native integration harness targets that previously compiled the shared runtime source list directly, including runtime lifecycle, VM stepping, cooperative tasks, Channels, native invocation, worker pool, wait-graph and stress/soak tests.

No runtime implementation, VM behavior, test assertions, stress rounds, timeout values, language semantics or WASM build rules were changed.

## Validation

- `make all -j2` passed.
- First shared-object build plus one harness link completed in 7.85 s on the available x86-64 host; a second harness link with the same configuration completed in 0.20 s.
- A cold-cache run of `flvm[13-53]` passed 41/41 in 28.652 s. The first case paid the one-time 7.476 s common-object build; subsequent cases reused those objects.
- A warm-cache run of the same selection passed 41/41 in 20.975 s.
- The 41-case selection includes the worker/Channel concurrency and stress cases; their execution logic was not altered.
- `make docs` passed (50 sections). A local full `make test` attempt reached 320/1,160 before the 300-second outer execution limit; this is incomplete, not a pass. The user had already confirmed 1,160/1,160 on the unmodified archive 343 baseline, and all 41 FLVM cases affected by this Makefile change pass in the focused run.

## Explicitly out of scope

- No changes to the worker scheduler, runtime or native modules.
- No reduction in concurrency, stress repetitions or test coverage.
- No shared object cache for the Emscripten harness or unrelated compiler/linker tests.
- No persistent cache outside `bin/`; `make clean` removes all cached objects.
