# Worker test stage diagnostics — archive 312

## Context

The archive 311 Termux rerun confirmed that `port[2-3]` capture regressions pass, but worker cases `flvm[38]`, `[40]`, `[42]`, `[43]`, `[44]`, `[45]`, and `[46]` still returned nonzero with empty output tails.

This does not establish a shell-capture failure: a child process can exit without writing stdout/stderr, and several C harnesses have early-return paths with little or no diagnostic context. The next step is to identify which command in each compound test case was reached before changing runtime code.

## Changes in archive 312

Added explicit stderr stage markers to the affected `case.toml` commands. Markers identify each transition:

- FlatL bytecode compilation (`flc`), including separate main/DOM/unsupported/channel programs where applicable;
- C harness build (`make` target);
- C harness execution.

The markers are emitted before each stage, so the final captured tail identifies the last stage reached even if the next process exits without diagnostics. They are emitted to stderr, which the archive 311 capture path merges into the captured output. Added `port[4]` to verify that a stderr stage marker is retained when the command itself exits nonzero.

No runtime, scheduler, worker-pool, native capability, HTTP, DOM, or shell-capture implementation was changed in this archive.

## Validation on the available POSIX host

- `make all -j2` — PASS.
- `bin/run_tests 'port[2-4]' -v -e` — PASS.
- `flvm[38]`, `[40]`, `[42]`, `[43]`, `[44]`, `[45]`, `[46]` — PASS in separately completed invocations.
- The initial looped validation command hit its overall command time limit while reaching `flvm[45]`; `[45]` was then run separately and passed. `[46]` was also run separately and passed.
- Termux rerun — PENDING. No platform-specific runtime conclusion is claimed yet.

## Next Termux check

```sh
make rebuild
bin/run_tests 'port[2-4]' -v -e
bin/run_tests 'flvm[38]' 'flvm[40]' 'flvm[42]' 'flvm[43]' 'flvm[44]' 'flvm[45]' 'flvm[46]' -v -e
```

If a test fails, retain its complete `output tail`, especially the last `[stage:...]` marker. That identifies whether failure occurs during FlatL compilation, C harness compilation, or harness execution. Only then should the corresponding source be changed.
