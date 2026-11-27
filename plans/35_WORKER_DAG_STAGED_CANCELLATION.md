# Worker DAG staged cancellation stress — archive 323

Archive 323 extends the v2.0 worker stress matrix with `flvm[50]`. It complements `flvm[49]`: that case checks deterministic converging DAGs with cancellation requests published before worker execution; this case moves cancellation into live worker execution and exercises different graph shapes and cancellation phases.

## Graph shape

Each runtime round creates four independent 10-task DAGs. Seven leaf tasks fan out to two intermediate tasks (`X` and `Y`), and both intermediates wait on a shared sink (`Z`). Every task has exactly one outgoing wait edge at most, matching the current `task.wait()` API. The leaf split is deterministic but varies by round and graph: `X` receives 2–4 leaves and `Y` receives the remaining 5–3 leaves.

The harness starts eight real workers and waits until all four `Z` sinks are observably `RUNNING` before issuing cancellation. The bytecode uses a bounded fixed-count loop so RUNNING is observable without an external sleep native.

## Staged cancellation phases

1. Graph 0: cancel `Z` while RUNNING; wait for `X` to run; cancel `X`; wait for a dependent leaf to run; cancel that leaf.
2. Graph 1: cancel `Y` while it is WAITING on `Z`; wait for one newly released leaf to run; cancel that leaf while RUNNING. `Z` and the other branch continue normally.
3. Graph 2: cancel `Z` while RUNNING; wait for `Y` to run; cancel `Y` while RUNNING.
4. Graph 3: no cancellation; it must drain normally.

After the staged requests, DRAIN must complete. The test asserts the exact terminal state of all 40 tasks, joins the worker pool, destroys every task and destroys the runtime. Three rounds execute per harness invocation.

## Scope and result

This is deterministic topology/timing variation, not randomized graph generation. It covers deeper fan-out/fan-in and cancellation at RUNNING and WAITING states, but it does not add multi-edge waiter semantics, which the current API does not provide. The test-only change did not require a production scheduler or Channel modification.

Local POSIX validation for archive 323:

- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- `flvm[47]`, `[48]`, `[49]`, `[50]` — PASS individually.
- `flvm[50]` dedicated harness — six consecutive direct executions PASS.
- `flvm[50]` — PASS under AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer.
- The full discovered suite (1189 tests), Windows worker execution and actual Emscripten linking remain unverified.
