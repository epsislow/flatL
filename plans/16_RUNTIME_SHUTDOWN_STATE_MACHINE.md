# Runtime DRAIN/CANCEL State Machine — archive 304

## Scope

Archive 304 implements the admission and shutdown state machine on top of the existing cooperative scheduler. It does **not** create background worker threads and does not claim pthread worker join semantics. This is the pre-worker contract layer: worker startup, condition-variable dispatch, worker-owned cleanup queues, and actual join remain future work.

## Public API

`include/flat_runtime.h` exposes:

- `flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_DRAIN)`
- `flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL)`
- `flat_runtime_shutdown_status(runtime)`
- `flat_runtime_lifecycle(runtime)`

Lifecycle states are `RUNNING`, `DRAINING`, `CANCELLING`, and `STOPPED`. Status values are `IN_PROGRESS`, `BLOCKED`, and `COMPLETE`.

## Semantics implemented

- DRAIN closes task admission: host task creation, task start, native `task.spawn`, and new direct runtime runs are rejected after the transition.
- DRAIN leaves CREATED tasks untouched and does not auto-start them. They remain host-owned records and must be explicitly destroyed (or cancelled) before runtime destruction.
- Started RUNNABLE work continues through `flat_runtime_schedule()`. Existing waits are retained. If no runnable/owned work remains but one or more tasks remain WAITING, status is `BLOCKED`, not complete.
- CANCEL escalates RUNNING or DRAINING, requests cancellation for every nonterminal task including CREATED, and relies on the existing scheduler safe-point cleanup path.
- DRAIN cannot downgrade CANCELLING. Repeated requests are idempotent.
- CANCEL may be requested after a DRAIN reached STOPPED if CREATED records remain; the cooperative scheduler can then process their cancellation cleanup.
- STOPPED is published only when the relevant work set is terminal and no scheduler turn/cleanup ownership remains.
- Runtime destroy is rejected until a requested shutdown has reached STOPPED; it still requires all task and Channel records to have been explicitly destroyed and host-side lifetime exclusion.

## Important boundary

`flat_runtime_shutdown_status()` is a cooperative progress report, not a join. The host drives work by calling `flat_runtime_schedule()`. No worker threads exist in this archive, so no pthread is joined and no worker condition-variable loop is present. The future worker-pool implementation must preserve these admission, DRAIN, CANCEL, blocked-wait, escalation, and destruction rules while replacing cooperative driving with worker wakeup and join.

## Validation

`flvm[41]` verifies DRAIN completion, rejection of new submissions, CREATED-task retention, blocked DRAIN on an unresolved wait edge, DRAIN-to-CANCEL escalation, no downgrade, cancellation of CREATED and RUNNABLE tasks, and completion status after cleanup.
