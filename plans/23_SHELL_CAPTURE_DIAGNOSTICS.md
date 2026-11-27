# Shell capture diagnostics — archive 311

## Context

Archive 310 added an output tail to unexpected `exec`-case failures. The next Termux run showed that this was useful, but the tail was still empty for several failing compound commands. Inspection of the POSIX shell capture path found two limitations:

1. `flat_port_shell_capture()` appended `2>&1` to the end of the command string. For a compound list such as `compile && make && test`, that redirection applied only to the final command. Diagnostics emitted by an earlier failing stage were not captured.
2. The capture loop stopped reading once the fixed buffer filled. It retained the beginning of output rather than draining the pipe and retaining the actual final bytes. A noisy command could therefore hide the failure message and could block once the pipe filled.

## Changes in archive 311

- POSIX command execution now wraps the complete command list in a subshell and redirects both streams for the entire list: `( command-list ) 2>&1`.
- POSIX capture drains the complete pipe and maintains a bounded rolling tail. It does not stop reading when the buffer fills.
- Windows process capture now uses the same rolling-tail behavior while continuing to drain the child pipe; stdout and stderr were already connected to the same pipe there.
- Added `port[2]`, which verifies stderr emitted by multiple commands in a compound list is captured.
- Added `port[3]`, which writes more than 30 KB before a final stderr marker and verifies that the marker survives in the captured tail.
- No Task Runtime, scheduler, worker-pool, native capability, HTTP, or DOM behavior changed.

## Validation on the available POSIX host

- `make all -j2` — PASS.
- `bin/run_tests 'port[2-3]' -v -e` — PASS.
- `flvm[34]` through `[46]` — each selected test passed in a separately completed invocation (including the worker HTTP/DOM cases).
- `flvm[38-40]` — one grouped invocation PASS.
- A larger grouped `flvm[34-46]` invocation did not complete within the available command limit. This remains a stress/sequence limitation; individual test passes must not be conflated with a completed grouped run.
- Termux behavior has not yet been re-tested against archive 311.

## Next Termux check

Run:

```sh
make rebuild
bin/run_tests 'port[2-3]' -v -e
bin/run_tests 'flvm[34-46]' -v -e
```

If a compound command fails before its final stage, its stderr should now be visible in `output tail`. If a case hangs rather than exits, the current runner still cannot print a failure tail until the child exits; the process must be interrupted and its last active case identified separately.
