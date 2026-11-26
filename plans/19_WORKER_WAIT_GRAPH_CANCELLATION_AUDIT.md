# Worker Wait-Graph and Cancellation Audit — archive 307

## Scope

Archive 307 adds focused regression coverage for wait-graph cleanup and Channel waiter cancellation while the real runtime-owned worker pool is active. It does not change runtime synchronization semantics.

## Covered behavior

### Wait-graph chain

The C harness creates four runnable tasks and publishes the dependency chain:

`A -> B -> C -> D`

It then attempts `D -> A`, which must be rejected as a cycle. A request-only cancellation is published for middle task B before workers start. Four workers start, DRAIN is requested, and the expected outcome is:

- B reaches `CANCELLED` and its outgoing dependency is removed;
- A is woken by B's terminal transition and completes;
- C remains waiting on D, then wakes and completes when D completes;
- D completes normally;
- DRAIN reaches STOPPED and workers join, proving no stale wait edge keeps the runtime blocked.

### Channel waiter cancellation

A runtime-owned Channel is locked by a CREATED owner task. A second task executes the Channel waiter native and is driven by one cooperative quantum into `WAITING`. The harness publishes request-only cancellation for that waiter, starts two workers, and requests CANCEL. Both the owner and waiter must reach `CANCELLED`; owner cleanup aborts the transaction, waiter cleanup releases its Channel reference, and after join both task records and the Channel can be destroyed before the runtime.

## Test

- `flvm[44]`: `test/cases/flvm/044_worker_wait_graph_cancel`.
- The language fixtures use the existing simple VM stepping program plus a small Channel waiter program.
- The C harness is built by `make test-worker-wait-graph-cancel` and invoked through `bin/run_tests`; no standalone test runner is introduced.

## Validation

- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- `bin/run_tests 'flvm[40]' -v -e` — PASS.
- `bin/run_tests 'flvm[41]' -v -e` — PASS.
- `bin/run_tests 'flvm[42]' -v -e` — PASS.
- `bin/run_tests 'flvm[43]' -v -e` — PASS.
- `bin/run_tests 'flvm[44]' -v -e` — PASS.
- Dedicated test binary, 100 consecutive plain executions — PASS.
- Dedicated test binary, six consecutive GCC ThreadSanitizer executions with `halt_on_error=1` — PASS, no TSAN report.
- Test discovery reports 1179 cases; full FLVM/default suite — NOT RUN.
- ASan/UBSan, Windows worker execution and actual Emscripten linking — NOT RUN.

## Limits and next audit

This is a deterministic focused regression for one four-task wait chain and one Channel waiter cancellation scenario. It does not prove arbitrary wait graph topologies, cancellation of multiple owners under arbitrary contention, HTTP/DOM lifecycle safety, plugin loading races, startup failure rollback, or cross-platform worker correctness. No runtime implementation behavior changed in archive 307.

Next priorities remain HTTP/DOM worker-mode lifecycle, plugin preparation/load ordering, deterministic partial worker-start failure injection, and Windows/Emscripten validation.
