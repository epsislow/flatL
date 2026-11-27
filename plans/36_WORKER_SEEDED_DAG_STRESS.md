# Worker seeded DAG stress — archive 324

Archive 324 adds `flvm[51]` to extend the v2.0 worker stress matrix from fixed graph families to reproducible seeded DAG topologies.

## Topology generation

- Each runtime instance contains 16 tasks.
- A fixed xorshift32 PRNG seed generates a directed graph. Every generated wait edge points from a lower task index to a higher task index, so cycles are impossible by construction.
- Each task has at most one outgoing `task.wait()` edge, matching the current API contract. Multiple tasks may converge on the same target; disconnected components and variable fan-in are allowed.
- Four distinct task nodes are selected for cancellation from the same PRNG stream.
- Six fixed seeds are each executed twice. The harness compares the topology/cancellation signature across both repetitions to verify deterministic generation.

## Concurrent cancellation and cleanup

For each generated topology, all 16 tasks are created and started, then wait edges are installed before worker execution begins. Four host threads wait on a condition-variable gate and publish request-only cancellation concurrently. The harness then starts six runtime workers and requests DRAIN.

After shutdown completion, every explicitly cancelled task must be `CANCELLED`; every other task must be `COMPLETED`. The harness joins the worker pool, destroys all tasks and destroys the runtime. No production scheduler behavior or public API was changed.

This case deliberately complements `flvm[50]`: it exercises seeded topology diversity and reproducibility, while `flvm[50]` remains the live RUNNING/WAITING staged-cancellation case. This test does not claim randomized scheduling or arbitrary multi-edge outgoing waits.

## Validation

Local POSIX host:

- `bin/run_tests 'flvm[51]' -v -e` — PASS.
- Dedicated harness, three additional consecutive executions — PASS.
- `flvm[51]` under AddressSanitizer — PASS.
- `flvm[51]` under UndefinedBehaviorSanitizer — PASS.
- Dedicated ThreadSanitizer harness build and direct execution — PASS. The equivalent `bin/run_tests` invocation exceeded the command time limit and is not counted as a completed runner pass.
- `bin/run_tests 'flvm[50]' -v -e` — PASS.
- `bin/run_tests 'flvm[47-51]' -v -e` reported 47, 48 and 49 passing before the command time limit; this grouped run is incomplete. Test 50 and test 51 passed in separate completed invocations.
- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).

User-reported Termux validation for archive 320: `bin/run_tests flvm` completed 47/47 tests, 0 failures, with 1186 discovered tests; elapsed 7:40.819. This validates the `flvm` category present in archive 320 on Android/Termux. It does not validate the later tests 48–51 or the full 1186-test discovery set.

Windows worker execution, actual Emscripten linking, and the complete default suite remain open. This checkpoint is stress coverage, not a general thread-safety certification.
