# Worker harness bytecode ownership audit — archive 333

## Scope

Audit `FlatBytecode` ownership in the worker DAG and shutdown stress harnesses. This checkpoint changes test harness cleanup and diagnostics only; it does not change production VM, scheduler, wait-graph, Channel, or worker-pool code.

## Findings

The harnesses below loaded bytecode with `flat_fb_load_file()` but did not consistently call `flat_fb_free()`:

- `flvm[47]`: missing cleanup after successful rounds and after a failed round.
- `flvm[49]`: missing cleanup after successful rounds and after a failed round.
- `flvm[51]`: missing cleanup on success, a failed generated case, and topology-signature mismatch.
- `flvm[52]`: missing cleanup when a round failed; the success path already freed the bytecode.

The first AddressSanitizer run of `flvm[51]` reported 5,908 bytes in eight allocations rooted in `flat_fb_load_file()` / bytecode section parsing. This was an ownership omission in the test harness, not evidence of a production runtime leak. The harness now frees the loaded bytecode on every post-load exit path.

`flvm[050]` also gained lifecycle-stage diagnostics around worker join, task destruction, and runtime destruction. These markers make a slow or stalled cleanup phase distinguishable from a wait-graph phase when the direct harness is run outside the test runner.

## Validation

- `make all -j2`: PASS.
- `make docs`: PASS; 50 documentation sections generated.
- `flvm[47]`, `[49]`, `[51]`, `[52]`: each passed through `bin/run_tests` individually.
- `flvm[53]`, `[54]`, `[55]`: each passed individually.
- Grouped `flvm[47-55]`: PASS, 9/9, total test time 1:14.220.
- Grouped `flvm[42-55]`: PASS, 14/14, total test time 1:55.645.
- Grouped `flvm[50-55]`: PASS, 6/6, total test time 48.262 seconds.
- `flvm[51]` direct harness under AddressSanitizer with `detect_leaks=1:halt_on_error=1`: PASS on five consecutive repetitions after the cleanup fix; no LeakSanitizer report.
- `flvm[50]` direct harness: multiple repeated runs completed all three rounds; diagnostic markers confirmed successful worker join, destruction of all 40 task handles, and runtime destruction. Some repeated invocations took substantially longer while a dependent task was being scheduled, but the observed runs completed.

## Boundaries / open work

- No production runtime change was made; the observed intermittent delay in larger foreground invocations has not been attributed to a specific runtime defect.
- The complete `flvm[1-55]` suite has not yet completed as one invocation.
- A source scan of all FLVM C harnesses that call `flat_fb_load_file()` found no remaining harness without a corresponding `flat_fb_free()` call; this is a call-presence audit, not proof of every path by itself.
- Windows worker execution and a real Emscripten SDK build/link/run remain open platform gates.
- The supplied ZIP snapshot has no `.git` metadata, so branch and commit status cannot be inspected from this workspace.
