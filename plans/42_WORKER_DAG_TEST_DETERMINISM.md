# Worker DAG test determinism and bytecode cleanup — archive 332

## Scope

This checkpoint tightens the existing `flvm[50]` staged-cancellation regression. It does not change production VM, scheduler, wait-graph, Channel, or worker-pool code.

## Findings

The original `busy.fl` used a 5,000-iteration loop while the harness polled task state every millisecond and expected to observe transient `RUNNING` states after waiters were released. Under a fast worker schedule, a task could reach `COMPLETED` between polls. The harness then reported a false state assertion (`COMPLETED` instead of the expected transient `RUNNING`).

The test now uses a 50,000-iteration workload and starts its eight workers with quantum 16. This keeps the relevant execution window observable while avoiding the excessive scheduler-turn overhead of quantum 1 for this particular state-observation test. Other cases continue to cover quantum-1 worker scheduling and stress behavior.

The harness also prints the task-state matrix if DRAIN fails to complete, so a future failure identifies which graph nodes remain active or waiting.

## ASan finding

The first AddressSanitizer run completed all three rounds but LeakSanitizer reported 5,909 bytes allocated while loading the bytecode. The allocations belonged to the `FlatBytecode` object loaded by the test harness. `main()` had omitted `flat_fb_free(&bc)` on both the success path and the `run_round()` failure path. The harness now frees the loaded bytecode in both paths.

This was a test-harness ownership leak, not a leak in `flat_fb_free()` or the runtime scheduler.

## Validation

- `make all -j2` and `make docs`: PASS after the final test-only edits (50 documentation sections generated).
- `flvm[42-43]`: grouped runner invocation PASS (2/2).
- `flvm[50]`: isolated runner invocation PASS after the final changes.
- `flvm[50]` direct harness: 8/8 repetitions with the 50,000-iteration workload and worker quantum 16 before the bytecode-free cleanup; after that cleanup, six additional normal-harness runs completed successfully. Each invocation runs three rounds and 40 tasks per round.
- `flvm[50]` AddressSanitizer: PASS with `detect_leaks=1:halt_on_error=1`; no leak report after adding `flat_fb_free()`.
- `flvm[50]` UndefinedBehaviorSanitizer: PASS with `halt_on_error=1:print_stacktrace=1`.
- Grouped `flvm[47-51]` did not complete within the available outer command window. The command reported completed cases 47–49 before the limit; it is not counted as a completed group.

## Boundaries

- No production runtime change is justified by this checkpoint.
- The full `flvm[1-55]` suite remains unverified as one invocation.
- Windows worker execution and an actual Emscripten SDK build/link/run remain open platform gates.
