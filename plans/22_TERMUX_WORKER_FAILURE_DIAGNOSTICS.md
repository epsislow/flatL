# Termux worker-test failure diagnostics — archive 310

## Context

After archive 309, the user reported eight failures in `flvm[34-46]` on Termux after `make rebuild`:

- `flvm[34]` and `flvm[35]` failed in native worker-capability assertions.
- `flvm[38]`, `[40]`, `[42]`, `[43]`, `[45]` and `[46]` returned exit 1 from compound shell commands.

The original `run_tests` error included the command and exit code, but discarded the captured output whenever an `exec` case returned the wrong exit code. This made it impossible to distinguish an `flc` compile failure, a C harness failure, or a runtime failure from the user's summary.

## Confirmed corrections

### Native capability expectations

The implementation's current worker policy is explicit:

- `task` and `channel` are `SERIALIZED`, because their shared runtime state is protected by their respective synchronization/gating mechanisms.
- `dom` is `PENDING`, with effective `SERIALIZED` fallback.
- legacy/unknown plugins remain `UNSUPPORTED` unless they provide an explicit declaration.

The tests still asserted that `task` was `UNSUPPORTED`, which contradicted the implementation and the worker-pool checkpoint documented in plan 17. This caused deterministic failures:

- `flvm[34]` returned 3 for the initial `task` policy assertion (and its concurrent query also expected the stale value).
- `flvm[35]` returned 8 because it expected `FLAT_NATIVE_TASK_SPAWN` to be rejected.

The tests now expect `SERIALIZED` for task and channel and explicitly verify that the task module is admitted with that policy. The legacy plugin and lazy plugin load after capability freeze remain rejected.

### Failure output from `run_tests`

For `CASE_KIND_EXEC`, when the shell command exits unexpectedly, the runner now appends the last 420 characters of captured output to the failure diagnostic. This preserves the existing success behavior and makes compiler/runtime stderr visible on the next Termux run. The tail is used because compiler and Make output often precede the actual error.

## Validation on the available POSIX host

- `make all -j2` — PASS.
- `flvm[34]` — PASS after correcting the stale task/channel expectations.
- `flvm[35]` — PASS after correcting the stale task-module expectation.
- `flvm[38]`, `[40]`, `[42]`, `[43]`, `[45]`, `[46]` — each passed in separate completed invocations.
- The `flvm[43]` C harness passed five direct consecutive executions; each execution runs 20 independent worker-runtime lifecycles.

## Still unverified

The six Termux failures (`flvm[38]`, `[40]`, `[42]`, `[43]`, `[45]`, `[46]`) are not yet diagnosed. They passed individually on the available POSIX host, but that does not prove Termux correctness. No runtime scheduling, worker, HTTP, DOM, or synchronization code was changed in this archive. The next Termux run should use this archive's improved runner diagnostics; the captured output tail will identify the failing stage and concrete error. Only then should any runtime behavior or test deadlines be changed.

The full default suite was not run. Windows and actual Emscripten worker execution remain outside this validation.
