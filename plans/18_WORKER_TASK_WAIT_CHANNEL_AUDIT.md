# Worker Task Wait and Channel Contention Audit — archive 306

## Scope

Archive 306 is a focused concurrency-validation step. It does not change runtime synchronization or worker scheduling semantics. It reuses the existing `027_channel_native` Flat program, but executes it through the runtime-owned worker pool instead of the cooperative host scheduler.

## Covered behavior

The test program:

1. Creates one runtime Channel and seeds shared values.
2. Spawns two child tasks that contend for the same Channel transaction.
3. Uses `task.wait()` in the parent to wait for both children.
4. Verifies transaction-owner scheduling allows the lock owner to finish and the second contender to retry safely.
5. Spawns a child that attempts `task.wait()` while holding a Channel transaction; that child must fail rather than suspend while owning the transaction.
6. Reads the shared Channel values and destroys the Channel as part of the Flat program.

The C harness starts four runtime workers, waits for the parent and child terminal states, requests DRAIN, joins workers, destroys task records, and destroys the runtime. It repeats the entire independent-runtime lifecycle 20 times in one process.

## Test

- `flvm[43]`: `test/cases/flvm/043_worker_task_wait_channel`.
- Existing `027_channel_native/program.fl` is reused as the program fixture; no duplicate language fixture is maintained.
- The C test is built by `make test-worker-task-wait-channel` and is invoked only through the existing `bin/run_tests` framework.

## Validation

- `make all -j2` — PASS.
- `bin/run_tests 'flvm[43]' -v -e` — PASS (20 complete four-worker runtime lifecycles, 4 workers per lifecycle).
- Rebuilt the dedicated test binary with GCC ThreadSanitizer and ran all 20 lifecycles with `TSAN_OPTIONS=halt_on_error=1` — PASS, no TSAN report.

The test proves the covered `task.wait`/Channel transaction scenario works under worker mode. It does not prove arbitrary wait-graph topologies, simultaneous Channel cancellation/retry races, HTTP/DOM thread safety, plugin lifecycle safety, or platform portability. Those remain separate audit items. No runtime implementation behavior changed in this archive.


## Follow-up

Archive 307 adds the adjacent wait-graph/cancellation coverage in [19_WORKER_WAIT_GRAPH_CANCELLATION_AUDIT.md](19_WORKER_WAIT_GRAPH_CANCELLATION_AUDIT.md). The two archives together cover one contended Channel transaction workflow and one multi-node wait chain plus Channel waiter cancellation; they remain focused cases rather than a general concurrency certification.
