# Worker mixed DRAIN/CANCEL escalation and soak — archive 325

## Scope

`flvm[52]` extends the archive-324 seeded-DAG coverage with repeated worker-pool shutdown races. Each round creates 24 tasks, starts 18, and retains six `CREATED` tasks. The first started task has a forced wait edge into the CREATED suffix, ensuring DRAIN cannot silently finish before CANCEL is published.

Eight host threads synchronize on a condition-variable gate and concurrently request shutdown. The request set always contains at least one DRAIN and one CANCEL; the remaining requests are selected deterministically from the round's seed. The test accepts either legal final observation after the race: `CANCELLING` while cleanup is still progressing or `STOPPED` if cancellation has already completed. It then requires every task, including the CREATED suffix, to be `CANCELLED`, joins all workers, destroys every task, and destroys the runtime.

The harness repeats 12 runtime lifecycles with distinct deterministic seeds. This is a bounded soak/regression test, not an unbounded endurance test and not a proof against every possible interleaving.

## Validation

- `bin/run_tests 'flvm[52]' -v -e` — PASS.
- Ten additional direct harness executions — 10/10 PASS (120 runtime cycles, 2,880 task instances).
- AddressSanitizer harness — PASS.
- Normal harness build — PASS.
- UndefinedBehaviorSanitizer build did not complete within the available command limit; not counted.
- ThreadSanitizer was not run for this archive.
- Full FLVM/default suite, Windows worker execution, and actual Emscripten linking remain unverified.

No production scheduler or shutdown semantics were changed. The test records and exercises the existing contract: DRAIN excludes CREATED tasks; CANCEL escalates DRAIN and includes every nonterminal task; later DRAIN requests cannot downgrade cancellation.
