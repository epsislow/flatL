# VM worker ASan/UBSan regression validation — archive 330

## Scope

This checkpoint extends sanitizer evidence for the current archive-329 source snapshot. It does not change VM, scheduler, Channel, worker-pool, or test-runner behavior. The test runner fix in archive 329 remains the current implementation.

## `flvm[48]` — multi-Channel worker and shutdown stress

The existing `test-worker-channel-shutdown-stress` harness was rebuilt from the clean archive-329 source with Clang and the default module configuration. The build was run from the repository root so native plugin lookup resolves against the repository's `bin/` directory.

- AddressSanitizer: 3/3 consecutive direct harness executions PASS, using `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`.
- UndefinedBehaviorSanitizer: 3/3 consecutive direct harness executions PASS, using `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.
- No sanitizer diagnostics were emitted.
- Each execution reported `worker-channel-shutdown-stress-ok` with four Channels, eight writers per Channel, four workers, DRAIN completion, concurrent cancellation and post-join cleanup.

Representative build commands (the environment assignments intentionally allow the Makefile to append its normal platform/module flags):

```sh
CFLAGS='-Wall -Wextra -std=c11 -Iinclude -D_GNU_SOURCE -g -O0 -fno-omit-frame-pointer -fsanitize=address' \
LDFLAGS='-fsanitize=address' \
make -s test-worker-channel-shutdown-stress CC=clang TEST_BIN=/tmp/worker_channel_shutdown_stress_asan

CFLAGS='-Wall -Wextra -std=c11 -Iinclude -D_GNU_SOURCE -g -O0 -fno-omit-frame-pointer -fsanitize=undefined' \
LDFLAGS='-fsanitize=undefined' \
make -s test-worker-channel-shutdown-stress CC=clang TEST_BIN=/tmp/worker_channel_shutdown_stress_ubsan
```

The bytecode fixtures are generated from the existing writer/waiter templates using the repository's `bin/flc`, as in `flvm[48]`. Run the harness from the repository root and pass the four writer bytecode paths followed by the four waiter bytecode paths.

## `flvm[54]` — Emscripten single-thread synchronization branch

- Rebuilt `test-emscripten-no-pthreads` with Clang AddressSanitizer and the default module configuration.
- Harness execution: PASS with leak detection enabled.
- Output: `emscripten-no-pthreads-ok sync=noop blocking=rejected worker-start=rejected runtime=clean`.

This remains a host-compiler preprocessor-branch simulation, not a real Emscripten/WASM build.

## Termux confirmation

The user ran the archive-329 test runner on Termux:

```text
bin/run_tests 'flvm[48]' -v -e
Passed: 1 Failed: 0 Total: 1
Total tests time: 18092 ms
```

This confirms that the expanded-command test-runner fix works for the long `{root}` / `{tmp}` command used by `flvm[48]` on the user's Android/Termux environment.

## Boundaries / remaining work

- The complete `flvm` suite on the archive-329 snapshot is not counted as passing; the prior full run exceeded the available command window.
- These new sanitizer runs cover `flvm[48]` and `flvm[54]`, not every VM/worker test. Earlier sanitizer evidence for other focused cases remains recorded in their individual plan documents.
- Windows worker execution and an actual Emscripten SDK build/link/run remain open platform gates.
- No production source changes were made in this checkpoint.


## Focused FLVM regression sweep — archive 331

The current archive-329 source snapshot was also checked through the normal test runner. Every case from `flvm[1-55]` completed successfully in at least one completed grouped or isolated invocation. The completed batches included `flvm[1-16]` (16/16), `flvm[17-19]` (3/3), `flvm[21-23]` (3/3), `flvm[33-37]` (5/5), `flvm[39-41]` (3/3), and `flvm[44-46]` (3/3); the remaining cases were run individually or in smaller selections and passed.

Additional targeted evidence for the worker/task-wait path:

- `flvm[42-43]` passed together in a completed two-case invocation.
- The `flvm[43]` dedicated harness passed 10/10 direct repetitions (200 complete runtime/task lifecycle rounds total).
- The `flvm[43]` runner case passed 3/3 consecutive isolated invocations.
- `flvm[48-55]` cases all passed in completed grouped or isolated invocations; the full eight-case grouped command did not finish within the outer execution limit, so it is not counted as a completed group.

Some larger grouped selections exceeded the outer execution limit after printing PASS for the cases completed so far. The affected cases passed when rerun individually, and no assertion failure or sanitizer diagnostic was observed. This is not evidence that the entire suite passed in one invocation: the complete `flvm[1-55]` suite remains unverified as a single run. Continue investigating the occasional grouped-run timeout separately from VM correctness; do not infer a production runtime defect from the timeout alone.
