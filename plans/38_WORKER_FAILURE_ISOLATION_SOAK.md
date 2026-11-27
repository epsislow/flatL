# Worker failure isolation and lifecycle soak — archive 326

## Scope

`flvm[53]` exercises task failure isolation under the real POSIX worker pool while repeatedly creating and destroying independent runtimes.

Each round creates a fresh `FlatRuntime`, creates and starts eight tasks, and starts four workers. Two tasks execute the existing intentional `FAIL` bytecode; the remaining six execute a successful bytecode program. The runtime receives DRAIN immediately after worker startup. The test waits for shutdown completion, then verifies:

- both failing tasks reach `FLAT_TASK_FAILED` and expose `FLAT_VM_FAILED` / `FLAT_TERM_FAIL` results;
- all six healthy tasks reach `FLAT_TASK_COMPLETED` and expose `FLAT_VM_COMPLETED` results;
- failure of one task does not abort or cancel unrelated tasks or prevent DRAIN completion;
- all worker handles are joined before task/runtime destruction;
- every task is explicitly destroyed and the runtime is destroyed before the next round.

The harness repeats 100 independent runtime lifecycles per invocation: 800 task instances, 200 intentional task failures, 600 successful tasks, and 400 worker-thread starts. This is a bounded soak, not an unbounded endurance test or proof against every interleaving.

## Validation (local POSIX host)

- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- `bin/run_tests 'flvm[53]' -v -e` — PASS.
- Four additional completed `bin/run_tests 'flvm[53]' -e` invocations — 4/4 PASS. A fifth repeat attempt was interrupted by the outer command time limit before completion and is not counted.
- `flvm[53]` with AddressSanitizer — PASS.
- `flvm[53]` with UndefinedBehaviorSanitizer — PASS.
- `flvm[53]` with ThreadSanitizer — PASS.
- `flvm[51]` — PASS; `flvm[52]` — PASS.
- A grouped `flvm[47-53]` run passed cases 47–50 before the command time limit; it is not counted as a completed group. Cases 51, 52 and 53 were then run separately and passed.

No production scheduler changes were required. The test confirms the existing task-level failure isolation contract under the tested worker configuration; it does not certify arbitrary native plugin failure behavior or general thread safety.

The user's reported Android result for archive 320 remains 47/47 FLVM tests. Archive 326 and tests 48–53 have not yet been validated on Termux. Windows worker execution and actual Emscripten linking remain open platform gates.
