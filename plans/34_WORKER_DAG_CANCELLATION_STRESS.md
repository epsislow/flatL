# Worker converging DAG cancellation stress — archive 322

This checkpoint extends the v2.0 stress matrix with `flvm[49]`.

- Eight independent DAGs per round, six tasks each; five rounds.
- Each waiter has one outgoing wait edge (the current runtime contract), but multiple waiters share a target.
- Two deterministic fan-in shapes alternate by round and graph index.
- Eight host threads issue concurrent cancellation requests, one selected interior node per graph.
- Four runtime-owned workers drain the graphs; each node's terminal state is checked, followed by worker join, task destroy and runtime destroy.

This is **deterministic topology variation**, not unconstrained randomized DAG generation. It does not test a waiter with multiple simultaneous dependencies, which the API does not support. No production scheduler change was needed.

Validation: `flvm[49]` PASS on local POSIX host. Windows execution, actual Emscripten linking, and the full suite remain separate gates.
