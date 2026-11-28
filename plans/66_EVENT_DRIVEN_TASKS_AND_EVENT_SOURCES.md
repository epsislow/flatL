# Event-driven Task Dispatch and Host Event Sources — roadmap

**Status:** staged implementation; archive 359 adds the host-side event broker and archive 360 adds queue-depth inspection plus scheduler wake notification. `task.spawnFor()` dispatch remains pending.
**Baseline:** archive 358 (`flatL-main-358-event-source-kind-contract.zip`).
**Proposed next plan:** `task.spawnFor()` and a generic event-source contract, with sockets as the first transport integration.

## 1. Goal

Add an event-driven way to start FlatL task executions when an external event arrives. The event source owns event production; the Task runtime owns dispatch, task creation, scheduling, concurrency policy, queueing and handler lifecycle.

The intended public concept is `task.spawnFor()`. Its parameter order is now agreed at the conceptual level:

```flat
 task.spawnFor(sourceId, sourceKind, eventName, #handler, arg1, arg2, ...)
```

This is a proposed API contract for implementation; exact event-payload binding, options syntax, registration cancellation and lifecycle details remain to be finalized and tested. The first parameter is the ordinary numeric ID used by FlatL modules, not an object or an OO resource handle.

This mechanism must not be limited to sockets. Candidate sources include TCP/UDP/WebSocket, timers, browser UI events, SSE, filesystem watchers and asynchronous HTTP completion events, subject to host capabilities.

## 2. Existing foundations (archive 356)

- `task.spawn(#block, arg1, ...)` already creates a child task and binds positional values to the named parameters declared by the block.
- Spawn arguments are evaluated in the parent and copied into the child's private VM/VCX. Scalars, strings, LIST and OBJECT are supported by the existing deep-copy rules; VM-local file handles are not transferable.
- The cooperative scheduler already supports task lifecycle, WAITING/RUNNABLE transitions, `task.wait`, cancellation, task result metadata and scheduler-aware Channel operations.
- Channels provide explicit shared data and transaction semantics; they are useful for application-level coordination but should not be mandatory for every event handler.
- FLEditor currently uses `flat_runtime_run_scheduled()` as a **one-shot** adapter. It creates a runtime, executes runnable tasks, then cleans up the runtime before returning. It does not expose a persistent runtime session that can accept later browser events.
- The browser/WASM execution path is single-threaded cooperative execution. It does not start pthread workers or browser Web Workers.
- The existing `task.spawn()` and `task.wait()` semantics must remain unchanged.

## 3. Proposed `spawnFor` model

An event registration associates:

1. `sourceId`: a NUMBER containing the source's ordinary FlatL numeric ID (the same value model used by `vcxId`, `csvId`, `taskId`, `chanId`, and IDs accepted by `mem.infoResId()`);
2. `sourceKind`: an explicit namespace/category string used to interpret `sourceId`;
3. `eventName`: the event selected from that source;
4. a FlatL block to execute;
5. zero or more bound user arguments, following the same positional binding/deep-copy rules as `task.spawn()`;
6. an execution policy and bounded-queue policy.

There are no resource objects, object methods, or implicit type inference from an ID. The pair `(sourceKind, sourceId)` identifies the source in the same explicit way that `mem.infoResId(id, kind)` requires the caller to provide the kind.

For the `sockets` module, **all socket transports use `sourceKind = 'socket'`**. WebSocket, TCP and UDP are transport details owned by `sockets`; `task` does not need separate kinds such as `sockets.ws`, `sockets.tcp` or `sockets.udp`. `eventName` distinguishes events such as `receive`, `close` and `error`.

When an event is delivered, the runtime creates a new task execution for the handler. The event payload is supplied through a documented binding convention (to be decided: first block parameter before bound arguments, or an explicit event object in a defined parameter position). Each event execution gets its own VM/VCX as a normal spawned task. The source does not execute application handler code itself.

Illustrative syntax only:

```flat
wsId = sockets.wsConnect(...)
handlerId = task.spawnFor(wsId, 'socket', 'receive', #onMessage, userId)
```

The handler block receives the event payload plus the explicitly bound values. The exact event-payload parameter position and options representation are not frozen by this roadmap. Bound user arguments must retain the existing `task.spawn()` positional binding and deep-copy semantics.

## 4. Execution policies

Two policies are required for the initial design:

### `serial`

- At most one handler task for a registration is active at a time.
- Events arriving while a handler is active are queued in arrival order.
- The next handler task starts after the previous one reaches a terminal state.
- The queue is bounded; overflow behavior must be explicit.
- This is the default candidate because it preserves event processing order and avoids accidental concurrent mutation.

### `parallel`

- An event can create a new handler task while earlier handler tasks are still active.
- `maxActive` limits the number of active handler tasks for this registration.
- When the limit is reached, events follow the configured bounded-queue overflow policy.
- Event arrival order is preserved in dispatch order, but task completion order is not guaranteed.

The initial design should not promise unlimited task creation or unbounded event buffering.

## 5. Queue and backpressure policy

The API needs an explicit bounded queue capacity and an overflow policy. Candidates:

- `wait`: apply backpressure where the source supports pausing; otherwise the host adapter must define how pending events are retained within a bounded limit.
- `dropOldest`: discard the oldest queued event and retain the newest.
- `dropNewest`: discard the newly arriving event.
- `error`: fail/disable the registration and expose a diagnostic.

These names are candidates, not a finalized API. The design must define counters/diagnostics for dropped events and ensure that browser event callbacks never block the browser event loop waiting for FlatL work.

For sources that cannot be paused (notably browser WebSocket message delivery), a finite queue limit and deterministic overflow behavior are mandatory.

## 6. Registration lifecycle

The design must specify:

- registration handle returned by `spawnFor`;
- explicit unregister/cancel operation (candidate: `task.cancelFor(handlerId)`);
- behavior when the source closes or errors;
- whether unregister prevents new dispatch only or also cancels already-running handler tasks;
- source-handle lifetime and ownership (the registration must not outlive a destroyed source silently);
- runtime shutdown behavior and cleanup of queued event payloads;
- handler failure policy (one failed event task must not silently disable unrelated registrations);
- panic and cancellation behavior;
- whether one event name can have multiple independent registrations.

Prefer explicit lifecycle semantics over implicit destruction. Do not overload `task.cancel()` for registration handles unless the type distinction is unambiguous.

## 7. Event-source abstraction

The Task runtime should depend on a small generic host event-source contract, not on socket-specific types. The source namespace is explicit (`sourceKind`) and the source identifier is a plain NUMBER (`sourceId`). For sockets, every transport registers under the shared kind `socket`; the socket adapter resolves the numeric ID to its own internal connection record. This follows the existing FlatL convention used by `mem.infoResId(id, kind)` and avoids introducing resource objects.

A source adapter should be able to:

- register/unregister an event subscription;
- deliver an event name plus owned payload to the runtime;
- notify source close/error;
- stop delivery during runtime shutdown;
- transfer or copy payloads safely into task-owned VM values.

The runtime then applies `serial`/`parallel`, queueing and task lifecycle rules uniformly. Socket, timer, UI and filesystem modules remain responsible for their platform-specific event production.

This is a conceptual boundary; archive 356 does not yet contain a generic event-source registry or external-I/O wait/wakeup API.

## 8. Platform capability matrix (proposed)

| Event source / transport | Native hosts | FLEditor WASM/browser | Notes |
|---|---|---|---|
| TCP client | Yes, subject to OS/network policy | No raw TCP | Native socket backend only |
| TCP listener/server | Yes | No | Browser cannot bind/listen on TCP ports |
| UDP | Yes, subject to OS/network policy | No raw UDP | Native socket backend only |
| WebSocket client | Yes, via native networking backend | Yes, browser `WebSocket` API | First socket event-source target |
| WebSocket server | Possible with native implementation/library | No direct browser server | Separate server-side capability |
| SSE client | Possible via HTTP streaming backend | Yes, browser `EventSource` where permitted | Server-to-client event stream |
| HTTP Fetch completion | Native HTTP backend | Yes, browser Fetch with existing host constraints | One-shot async completion event, not a socket |
| Browser UI events | Not a generic native capability | Yes, through a future browser UI bridge | Depends on the UI Runtime/DOM bridge |
| Timer events | Yes | Yes | Host timer adapter; define cancellation and drift semantics |
| Filesystem watcher | Platform-specific | No general host filesystem watcher in browser sandbox | Native capability varies by OS |
| WebTransport | Platform/library dependent | Browser support where available and secure-context/server requirements are met | Later candidate, not initial scope |

Capability checks must be explicit. Unsupported operations should return a stable, documented error rather than emulate raw TCP/UDP in browser code.

## 9. FLEditor/WASM architecture prerequisites

The current one-shot `flat_runtime_run_scheduled()` adapter cannot by itself support long-lived socket/UI/timer registrations: it destroys task and Channel records when the initial runnable queue drains.

Before browser event sources can work, design a persistent WASM runtime session with explicit host lifecycle, likely operations equivalent to:

- create/start a session and root task;
- run a bounded cooperative scheduler slice;
- enqueue a host event into the session;
- resume scheduling after the event;
- query completion/diagnostics;
- cancel and destroy the session deterministically.

The exact C and JavaScript APIs are not specified here. The design must address multiple editor runs, runtime isolation, reentrancy, JS callback ownership, memory growth, event payload lifetime, session replacement, and cleanup when the editor stops or recompiles a program.

Browser callbacks should enqueue owned event data and request a scheduler pump. They must not call into a currently-running VM reentrantly. No polling loop per socket and no browser Web Worker requirement is assumed for the initial single-threaded implementation.

## 10. Implementation sequence

1. **Freeze the FlatL value contract** — use `sourceId` NUMBER + explicit `sourceKind` + `eventName` + handler block + bound arguments. The socket namespace is exactly `socket` for WebSocket, TCP and UDP. No OO resources or inferred kinds.
2. **Implement generic runtime registration and event delivery** — add runtime-owned registration records and a host-facing event enqueue/delivery API. Keep source adapters outside the scheduler and ensure event payload ownership is explicit.
3. **Implement Task dispatch semantics** — create one normal isolated task per delivered event, preserving `task.spawn()` parameter binding and deep-copy behavior; add serial/parallel and bounded queue policy.
4. **Add deterministic runtime tests** using the existing `bin/run_tests` framework and `test/cases/` conventions. Cover ID/kind validation, event order, payload copying, task isolation, queue limits, cancellation and shutdown.
5. **Add the first source adapter** — sockets, initially WebSocket where supported, then native TCP/UDP as the host backend permits.
6. **Add persistent FLEditor WASM sessions** before browser event sources; the current one-shot adapter is not sufficient.

Implementation must proceed in small independently tested stages. Do not introduce a sockets-only scheduler path. Do not add Python tests or standalone Python runners.

## 11. Native host event loop

Native TCP/UDP/WebSocket adapters need a host event notification mechanism. The implementation must not create one busy polling loop per socket or one thread per connection by default.

Select a portable abstraction after an audit of current platform/build support. Candidate backends include epoll on Linux, kqueue where available, IOCP on Windows, and an appropriate fallback for other supported targets. The first implementation may deliberately support a smaller platform set, but capability reporting and unsupported behavior must be explicit.

The event loop must hand events to the runtime without executing FlatL blocks on the I/O callback thread. The scheduler remains the owner of FlatL task execution. Cross-thread event enqueue/wakeup synchronization must be specified and tested before enabling real worker concurrency for event delivery.

## 11. Phased implementation roadmap

### Phase A — contract and audit (no code until approved)

- Audit `FlatRuntime`, task creation/terminal cleanup, scheduler wakeup, Channel ownership and the FLEditor one-shot runner.
- Define the generic event-source C contract and ownership of event payloads.
- Finalize `spawnFor` argument binding, return handle and unregister semantics.
- Finalize `serial`/`parallel`, `maxActive`, queue capacity and overflow behavior.
- Define failure, panic, cancellation, source-close and runtime-shutdown semantics.
- Define native versus browser capability reporting.

Exit: API/spec and ownership/lifecycle diagrams reviewed; existing `task.spawn` behavior unchanged.

### Phase B — generic runtime dispatch (no sockets dependency)

- Implement event registration and event-to-task dispatch for an internal/test event source.
- Implement serial dispatch and FIFO bounded queue.
- Implement parallel dispatch and `maxActive`.
- Implement unregister, source close, handler failure isolation and runtime cleanup.
- Add deterministic diagnostics for queued, dispatched, dropped and active events.

Exit: all behavior is exercised through the existing `bin/run_tests` harness and `test/cases/` conventions.

### Phase C — native event source and sockets

- Implement a first native socket event source (recommend WebSocket client or TCP stream, after transport/backend selection).
- Add TCP/UDP only where the native backend and test environment support them.
- Validate close/error, partial reads, message boundaries, backpressure, queue overflow and cleanup.

Exit: focused native tests plus full relevant suites; no Python tests or standalone Python test runners.

### Phase D — persistent FLEditor WASM session

- Replace the one-shot-only limitation with an explicitly managed persistent session API while preserving the existing one-shot `flat_wasm_run()` compatibility path.
- Add browser event enqueue and non-reentrant scheduler pumping.
- Add WebSocket client, SSE, timer and (after UI Runtime design) UI event adapters.
- Validate session restart/stop, multiple sequential runs, queued events, close/error, memory limits and cleanup.

Exit: Node/browser smoke tests integrated with existing project test/build targets and manual FLEditor examples; no Python-based tests.

### Phase E — additional sources

- Filesystem watcher on native hosts.
- HTTP async completion integration if the existing HTTP module is extended beyond its current request model.
- WebTransport where platform support and practical use justify it.

## 12. Testing policy

All automated tests must use the repository's existing `bin/run_tests` framework and established `test/cases/<category>/<id>/` case layout, including the existing case metadata/fixtures conventions. Do not create Python tests, Python runners, or a parallel test framework.

Test layers should include:

- focused task/event runtime cases for registration, argument binding, serial order, parallel concurrency, maxActive, queue overflow and cancellation;
- source adapter cases for message/open/close/error and payload ownership;
- lifecycle/failure cases for panic, handler failure, source close, runtime cancel/destroy and repeated sessions;
- WASM smoke tests integrated with existing Makefile targets and the current Node/browser harness conventions;
- manual FLEditor examples for interactive behavior.

Run focused categories first, then broader relevant suites. A full-suite result is reported only when `bin/run_tests` completes and reports its final totals.

## 13. Out of scope for the first implementation

- browser raw TCP/UDP sockets;
- browser WebSocket server;
- mandatory Channel use for every event handler;
- callbacks executed directly inside host I/O callbacks;
- unbounded event queues or unbounded parallel task creation;
- automatic shared mutable state between event tasks;
- browser Web Workers or pthreads as a prerequisite;
- promise/callback syntax changes to the FlatL language;
- silently changing the existing `task.spawn`, `task.wait` or `task.result` contract.

## 14. Current decisions and open questions

Agreed direction from design discussion:

- Public API direction: `task.spawnFor(sourceId, sourceKind, eventName, #handler, ...args)`.
- `sourceId` is a NUMBER; it is not an object or an OO resource.
- `sourceKind` is explicit, following the `mem.infoResId(id, kind)` convention.
- Every socket transport uses the common `sourceKind = 'socket'`; transport differences stay inside the sockets adapter.
- It is generic over event sources, not specific to WebSocket or sockets.
- A new event creates a new FlatL task execution.
- Initial execution modes: `serial` and `parallel`.
- `maxActive` is needed to bound parallel handler executions.
- Event queues must be bounded and overflow behavior explicit.
- Native and browser backends expose different capabilities; raw TCP/UDP are not available in browser WASM.
- WebSocket is a first-class browser event source candidate.
- Tests must use `bin/run_tests` and the existing `test/cases/` format; no Python tests.

Still open:

- exact `spawnFor` native syntax and options representation;
- whether subscription cancellation is named `task.cancelFor` or uses another explicit API;
- default mode, queue capacity and overflow policy;
- whether `wait` overflow can pause each source or must degrade to bounded host buffering;
- registration cancellation versus cancellation of already-running handler tasks;
- source-handle ownership and event payload type mapping;
- native event-loop backend and supported OS matrix;
- persistent FLEditor WASM session C/JS API;
- whether event source adapters should be exposed as `sockets`, `timer`, `ui`, `fs.watch`, or other module APIs.

## 10. Archive 359 — host-side event broker foundation

Archive 359 adds `include/flat_runtime_event.h` and a runtime-owned event subscription registry/queue:

- subscriptions match the explicit `(sourceId, sourceKind, eventName)` tuple;
- `sourceId` is a plain numeric ID and `sourceKind` is explicit (`"socket"` for every sockets transport);
- host producers can publish `FlatValue` payloads to all matching subscriptions;
- each subscription receives an independent deep copy, so producer VM pools may be released after publish;
- each subscription queue is bounded at 256 events; an overflow rejects the publication atomically with `-2`;
- subscriptions can be removed explicitly, and queued values are released on unsubscribe; runtime destruction is rejected while subscriptions remain active.

The C API currently exposes `flat_runtime_event_subscribe`, `flat_runtime_event_unsubscribe`, `flat_runtime_event_publish`, and `flat_runtime_event_next`. `flvm[58]` covers namespace separation, fan-out, deep-copy delivery, empty queues and unsubscribe. Existing task behavior is unchanged.

**Not implemented in archive 359:** FlatL `task.spawnFor()`, automatic event-to-task dispatch, event payload binding to handler parameters, serial/parallel execution, `maxActive`, configurable overflow strategies, socket adapters, and persistent FLEditor WASM sessions. The broker API is the first isolated increment; these remain next steps and must not be described as complete.

Validation for archive 359: `make -j2 bin/flc bin/flvm` PASS; `bin/run_tests 'flvm[58]' -v -e` PASS (1/1). Full suite and sanitizers have not yet been run.


## 11. Archive 360 — event queue wake integration

Archive 360 adds `flat_runtime_event_pending()` for observing one subscription's queue depth. A successful `flat_runtime_event_publish()` now wakes the runtime ready-queue wait primitive after releasing `scheduler_mutex`. This allows host event loops and worker waiters to be notified that event work is available without running VM code in the producer.

This increment does not yet create FlatL handler tasks or provide `task.spawnFor()`. Queue consumption and dispatch remain host-driven until the scheduler-owned subscription integration is implemented.

Validation: `make -j2 bin/flc bin/flvm` PASS; `make -s test-runtime-event-source-broker TEST_BIN=/tmp/runtime_event_source_broker_test` and the resulting test binary PASS. The repository `bin/run_tests` executable is not included in the supplied archive, so the suite selector could not be invoked in this workspace.


## 11. Archive 361 — event-to-task dispatch primitive

Archive 361 adds `flat_runtime_event_spawn_next()`. It consumes at most one queued event for a subscription and creates a normal runnable runtime task at a selected bytecode block. The event payload is deep-copied as handler parameter 0; bound arguments are deep-copied after it using the same positional parameter-count validation as `task.spawn()`.

The queued event is retained if argument validation, deep-copying or task creation fails. An empty queue returns 0; successful task creation returns 1 and the new numeric `taskId`. A per-subscription dispatch guard prevents concurrent consumers from dispatching the same head event, and unsubscribe is rejected while a dispatch operation is in progress. Each created handler runs in the standard task VM/VCX isolation path.

Validation added as `flvm[59]`: the C integration harness publishes a socket-kind receive event, dispatches it to `#onEvent(event, user)`, runs the scheduler, and verifies event-first argument binding and task completion. Existing broker test `flvm[58]` remains passing.

**Still pending:** public FlatL `task.spawnFor()` registration from a running task, persistent handler/block and bound-argument storage, automatic scheduler draining of registered event queues, serial/parallel policies, maxActive/overflow configuration, and socket adapters. Archive 361 is the runtime dispatch primitive, not the complete public API.

## Archive 362 — multiple-event parallel dispatch validation

Archive 362 adds `flvm[60]`, an integration test that publishes two events to one subscription, converts both queued events into distinct runnable tasks before the scheduler runs, and verifies FIFO payload delivery plus independent task completion. This establishes the low-level parallel-dispatch primitive expected by the future `task.spawnFor(..., mode='parallel')` behavior.

This is validation of the C dispatch primitive only. It does not add the public FlatL `task.spawnFor()` API, automatic scheduler draining, serial mode, or subscription cancellation from FlatL.


## Archive 363 — persistent runtime event handlers and scheduler dispatch

Archive 363 adds `flat_runtime_event_subscribe_handler()`, a runtime-owned persistent registration that stores the handler bytecode pointer, block label and deep-copied bound arguments. Registration validates that the handler declares exactly one event parameter plus the bound argument count. The bytecode must remain alive until unsubscribe.

The cooperative scheduler now checks for pending events on registered handler subscriptions and converts one event at a time into a normal runnable task using the existing `flat_runtime_event_spawn_next()` path. The event payload is parameter 0 and the registered arguments follow. The event remains queued if task creation fails; dispatch is protected against concurrent unsubscribe. The scheduler stops dispatching external events once runtime shutdown leaves RUNNING.

Added `flvm[61]` to validate automatic scheduler dispatch, event-first argument binding, completion, pending-queue drain and unsubscribe. Existing `flvm[58-60]` regression harnesses remain passing.

**Not yet implemented:** the public FlatL `task.spawnFor()` native wrapper, a FlatL cancellation native, serial mode, `maxActive`, configurable overflow policies, and sockets adapters. This increment establishes persistent C-level handler registration and automatic scheduler dispatch, not the complete user-facing API.

Validation: `make -j2 bin/flc bin/flvm` PASS; `flvm[58]`, `flvm[59]`, `flvm[60]`, and new `flvm[61]` C integration harnesses PASS; `make docs` PASS. The supplied workspace does not contain `bin/run_tests`, so the test selector and full suite were not run. Sanitizers were not run.


### FlatL native registration (archive 364)

The initial FlatL surface is now implemented:

```flat
subscriptionId = task.spawnFor(sourceId, sourceKind, eventName, #handler, arg1, ...)
task.stopFor(subscriptionId)

#handler(event, arg1, ...)
  -- process one event
  OK
```

`spawnFor` returns a runtime-local subscription ID, not a task ID. Each event creates a distinct ordinary task ID. The subscription owns a bytecode snapshot and deep-copied bound arguments, so it outlives the registering task. `stopFor` unregisters it and drops queued events; it is rejected while dispatch is in progress. The initial native integration uses the fixed broker queue capacity (256) and parallel dispatch. `serial`, `maxActive`, configurable queue/overflow behavior, source adapters and persistent WASM sessions remain unimplemented. `flvm[62]` covers native registration/unregistration and delivery after the registering root task is destroyed.


## Archive 365 — serial event-handler dispatch in the runtime C API

Archive 365 adds `FlatEventDispatchMode` with `FLAT_EVENT_DISPATCH_PARALLEL` and `FLAT_EVENT_DISPATCH_SERIAL`, plus `flat_runtime_event_subscribe_handler_mode()`. The existing `flat_runtime_event_subscribe_handler()` remains source-compatible and selects parallel mode by default.

For serial subscriptions, the scheduler dispatches the next FIFO event only when the previous handler task reaches a terminal state (COMPLETED, FAILED, or CANCELLED). The runtime tracks active handler tasks by subscription; task completion and both cancellation paths release that accounting. `unsubscribe` is rejected while any handler task is still active. The lower-level `flat_runtime_event_spawn_next()` also enforces the serial gate, so callers cannot bypass the policy.

Added `flvm[63]` to publish two events and verify that the second remains queued until the first handler finishes, then verify FIFO output. The FlatL `task.spawnFor()` native still defaults to parallel mode; native mode selection syntax, `maxActive`, configurable queue/overflow policy and source adapters remain pending.

## Archive 366 — FlatL native dispatch-mode selection

Added `task.spawnForMode(sourceId, sourceKind, eventName, #handler, mode, ...args)` as an explicit, backward-compatible mode-selecting companion to `task.spawnFor()`. `mode` must be `'parallel'` or `'serial'`. The original `task.spawnFor()` signature and behavior remain unchanged, avoiding ambiguity between a mode string and a legitimate first bound argument.

Both native entry points use the same runtime registration bridge. `parallel` allows multiple handler tasks from one subscription to be active; `serial` permits only one non-terminal handler task and preserves FIFO delivery. Added `flvm[64]` to verify the FlatL native serial mode with two published events, including event-first and bound-argument delivery.

Still pending: `maxActive`, configurable queue capacity/overflow policy, explicit source-adapter APIs, and persistent FLEditor WASM sessions.

Validation: `make -j2 bin/flc bin/flvm` PASS; the new serial C integration harness PASS. `bin/run_tests` is not present in the supplied archive, so the repository test selector and full suite were not run.
