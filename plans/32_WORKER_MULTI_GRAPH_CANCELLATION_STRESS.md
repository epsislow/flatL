# Worker Multi-Graph Cancellation Stress — archive 320

## Scope

This checkpoint advances the still-open v2.0 stress item in `02_ROADMAP.md`. It adds deterministic coverage for several independent wait graphs receiving cancellation requests concurrently while a real worker pool later drains the graph.

This is a focused regression, not a claim that arbitrary wait graphs or all cancellation interleavings are exhaustively covered.

## Test shape

New test: `flvm[47]`, `test/cases/flvm/047_worker_multi_graph_cancel`.

Each round creates one runtime with eight independent four-task chains:

`A ->wait B ->wait C ->wait D`

Eight host threads wait on a common mutex/condition-variable barrier. Once all are ready, they concurrently call `flat_runtime_task_cancel_request()` for the eight distinct `B` tasks. The harness joins the request threads, starts a four-worker pool, requests DRAIN, waits for STOPPED, joins the worker pool, checks terminal task states, destroys all tasks, and destroys the runtime.

The expected terminal state per graph is:

- A: COMPLETED (woken when B is cancelled)
- B: CANCELLED (request-only cancellation is completed by scheduler/worker cleanup)
- C: COMPLETED (continues waiting for D)
- D: COMPLETED

The complete scenario is repeated five times per harness execution: 40 independent wait graphs, 160 task instances, and 40 simultaneous cancellation batches across the five rounds.

## What this adds to the v2.0 matrix

- Multiple independent wait graphs in one runtime.
- Simultaneous cancellation requests from multiple host threads.
- Real worker-mode DRAIN after the concurrent requests.
- Terminal-state verification and full task/runtime cleanup after every round.

Existing tests continue to cover adjacent dimensions: `flvm[39]` Channel read/write contention, `flvm[43]` nested waits with Channel transaction contention, `flvm[44]` wait-graph cycle rejection and Channel-waiter cancellation, `flvm[45]` HTTP worker lifecycle, and `flvm[46]` DOM teardown/cancellation.

## Validation (archive 320, local POSIX host)

- `make all -j2`: PASS.
- `make docs`: PASS (50 sections).
- `flvm[39]`: PASS.
- `flvm[40-41]`: 2/2 PASS.
- `flvm[42]`, `flvm[43]`, `flvm[44]`, `flvm[45]`, `flvm[46]`: PASS individually.
- `flvm[47]`: PASS; five consecutive completed invocations also passed. A subsequent repeat batch was stopped by the command execution limit after the fifth reported PASS; no test assertion failure was reported.
- `flvm[47]` with AddressSanitizer: PASS (`detect_leaks=0`).
- `flvm[47]` with UndefinedBehaviorSanitizer: PASS (`halt_on_error=1`).
- A grouped `flvm[42-47]` invocation printed PASS markers through `flvm[45]` before the command execution limit; `flvm[46]` and `flvm[47]` were then run and passed separately. The grouped invocation is not counted as a complete 42–47 run.
- The full 1186-test discovery set was not executed. Windows worker execution and an actual Emscripten build remain unverified.

## Findings and limits

No production scheduler or locking change was justified by this test. The current request-only cancellation and wait-graph cleanup behavior passed this particular multi-graph interleaving on the POSIX host.

The v2.0 broader stress item remains open. Follow-up coverage should include randomized/varied graph topologies, repeated mixed DRAIN-to-CANCEL escalation, Channel multi-writer transactions under worker load, and sanitizer runs on the complete worker test matrix. Windows and Emscripten remain separate platform gates.
