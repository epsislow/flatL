# Worker lifecycle sanitizer sweep — archive 334

## Scope

This checkpoint extends sanitizer coverage for the worker-pool lifecycle and wait-graph cancellation tests. It is validation and documentation only: no production VM, scheduler, Channel, wait-graph, worker-pool, or test-harness source was changed.

## Full focused FLVM sweep

All 55 cases in the `flvm` suite passed on the archive-333 source snapshot, split into three completed invocations:

- `bin/run_tests 'flvm[1-20]' -v -e`: PASS, 20/20.
- `bin/run_tests 'flvm[21-40]' -v -e`: PASS, 20/20.
- `bin/run_tests 'flvm[41-55]' -v -e`: PASS, 15/15.

This closes the per-case coverage gap for the current snapshot. It does not claim that one single `flvm[1-55]` process invocation completed; that separate runner-capacity/stability check remains open.

## Sanitizer coverage

The following harnesses were compiled with GCC using `-fsanitize=address,undefined -fno-omit-frame-pointer` and run with LeakSanitizer and UBSan configured to halt on errors:

- `flvm[47]` multi-graph cancellation: PASS (5 rounds, 8 graphs, 32 tasks).
- `flvm[49]` converging DAG cancellation: PASS (5 rounds, 8 graphs, 48 tasks).
- `flvm[50]` staged DAG cancellation: PASS (3 rounds, 4 graphs, 40 tasks). One initial sanitizer invocation exceeded the outer command time limit during the third round; a subsequent bounded rerun completed all rounds and cleanup with no sanitizer report.
- `flvm[52]` mixed DRAIN/CANCEL soak: PASS (12 rounds, 24 tasks).
- `flvm[53]` failure-isolation soak: PASS (100 runtime cycles, 8 tasks per cycle).

`flvm[52]` also passed GCC ThreadSanitizer (12 rounds, mixed DRAIN/CANCEL, worker join and cleanup complete).

The earlier archive-333 result for `flvm[51]` remains: five consecutive ASan/leak-detection repetitions passed after fixing its bytecode ownership cleanup. This checkpoint does not re-run or upgrade that evidence.

## Build and documentation

- `make all -j2`: PASS on the archive-333 source snapshot before this documentation-only checkpoint.
- `make docs`: PASS; documentation site regenerated with 50 sections before adding this plan. A final regeneration is required after this plan is added.

## Boundaries and remaining gates

- No production runtime change was justified by these results.
- One complete `flvm[1-55]` invocation remains unverified; all 55 cases did pass across the three completed groups above.
- Windows worker compilation/execution and a real Emscripten SDK build/link/run remain open platform gates.
- The supplied archive has no `.git` metadata, so the user's branch, commits, and uncommitted local changes cannot be inspected from this snapshot.
