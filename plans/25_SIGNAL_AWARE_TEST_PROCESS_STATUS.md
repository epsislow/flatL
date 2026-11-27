# Signal-aware test process status — archive 313

## Problem

Archive 312 added stage markers to the seven Termux worker tests. The user's rerun showed that every failing case reached `[stage:run-harness]`, while the captured tail contained no harness failure message. The existing POSIX shell capture mapped every `pclose()` status that was not `WIFEXITED()` to the integer `1`. That erased the distinction between a normal `exit(1)` and a process terminated by a signal (for example `SIGSEGV` or `SIGABRT`).

## Change

- `flat_port_shell_capture()` now returns a negative signal number (`-WTERMSIG(status)`) when the captured process is terminated by a signal. Normal process exit codes remain unchanged.
- `flat_case_run_ex()` formats negative results as `terminated by signal N` instead of reporting a misleading `exit 1`.
- The Windows process-capture implementation is unchanged; the `port[5]` regression reports a documented skip on Windows because that backend exposes native process exit codes rather than POSIX wait statuses.
- Added `port[5]`, a C-level regression that invokes a child terminated by SIGSEGV and asserts that `flat_port_shell_capture()` returns `-11`.

## Interpretation

The Termux log proves that the harness command was started. It does not yet prove a signal termination: the next run with this change will distinguish signal termination from an ordinary nonzero harness return. If it reports a signal, investigate the corresponding runtime/harness crash. If it reports a normal exit code, add or improve harness diagnostics at that failing return path.

## Validation

A local POSIX `popen()`/`pclose()` reproduction using a child that executes `kill -SEGV $$` returned `WIFSIGNALED(status)` with signal 11, confirming that this path can be represented distinctly. The C-level regression `port[5]` and the test-runner formatting probe both passed their expected checks on Linux. The project build and focused tests are recorded in the archive 313 checkpoint. Termux validation remains pending.
