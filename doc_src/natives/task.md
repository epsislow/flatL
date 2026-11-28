# Native `task`

The built-in `task` native module exposes cooperative task operations to code that is already running as a task owned by a `FlatRuntime` scheduler.

```flat
use native task

#main
  price = 12
  label = 'hello, task'
  items = [3, 5, 7]
  child = task.spawn(#worker, price, label, items)
  task.wait(child)
  state = task.state(child)
  io.outln(state)
  OK

#worker(price, label, items)
  io.outln(price)
  io.outln(label)
  io.outln(items)
  OK
```

A native host normally creates and starts the root task through the C `FlatRuntime` / `FlatTask` API, then drives `flat_runtime_schedule()`. `flat_runtime_run()` remains a direct VM run and does not install a task bridge. FLEditor WASM uses the one-shot `flat_runtime_run_scheduled()` adapter: it creates a runtime-owned root task, drives the cooperative scheduler, captures the root result, cancels any child tasks left waiting after the runnable queue drains, and destroys all task and Channel records before returning. This gives browser examples the real Task/Channel implementation without starting OS worker threads.

## Functions

| Call | Arguments | Returns | Behavior |
|---|---|---|---|
| `task.spawn(#block, arg1, …)` | Literal block label followed by zero or more value expressions; count must exactly match the block's named parameters | NUMBER — runtime-local task ID | Evaluates arguments in the parent, deep-copies them into the child's private VM context, and makes it RUNNABLE |
| `task.spawnFor(sourceId, sourceKind, eventName, #handler, options?, arg1, …)` | Positive whole NUMBER source ID, STRING source kind and event name, literal handler block, optional options OBJECT, then zero or more bound values | NUMBER — runtime-local event subscription ID | Registers a persistent handler with per-subscription dispatch and queue policies. Each event creates an isolated task; payload is parameter 0 and bound values follow it |
| `task.spawnForMode(...)` | Compatibility helper; same source/handler arguments followed by `parallel` or `serial` | NUMBER — subscription ID | Legacy mode-selection helper; prefer `task.spawnFor()` with `mode` in its options OBJECT |
| `task.spawnForLimit(...)` | Compatibility helper; same as mode helper followed by positive whole NUMBER `maxActive` | NUMBER — subscription ID | Legacy concurrency helper; prefer `task.spawnFor()` with `mode` and `maxActive` in its options OBJECT |
| `task.stopFor(subscriptionId)` | Positive whole NUMBER subscription ID | BOOL — `true` when removed | Unregisters the handler and discards queued events; fails if dispatch is currently in progress |
| `task.wait(taskId)` | Positive whole NUMBER task ID | BOOL — `true` when the wait request is accepted | If the target is non-terminal, the current task enters WAITING and yields to the scheduler. It resumes after the target reaches COMPLETED, FAILED, or CANCELLED. If the target is already terminal, execution continues immediately |
| `task.state(taskId)` | Positive whole NUMBER task ID | NUMBER — `FlatTaskState` numeric value | Queries a task's current lifecycle state |
| `task.result(taskId)` | Positive whole NUMBER task ID | OBJECT — `{ state, exitCode, termination, diagnostic }` | Returns a terminal task's result summary. It fails while the task is non-terminal or unknown |

`task.spawn(#worker, value1, value2, …)` binds positional values to the names declared by the target block header, for example `#worker(price, label, items)`. The number of supplied values must exactly match the block's declared parameter count; a mismatch fails the spawning task with `task.spawn.args`. Argument expressions are evaluated in the parent before the child is registered.

The child receives a fresh VM and private VCX. Only the declared parameter names are initialized; the parent's other variables are not inherited. Scalars and strings are copied, while LIST and OBJECT values are recursively deep-copied into the child's independent collection/string pools. Mutations in the child therefore do not change the parent's values; normal per-VM copy-on-write behavior continues to apply inside each VM. File handles are not transferable between VM-owned file tables and cause spawn to fail. This is value transfer by copy, not a shared Channel or cross-task COW reference.



`task.spawnFor(sourceId, sourceKind, eventName, #handler, options?, arg1, …)` registers a persistent event handler owned by the current runtime, not by the lifetime of the task that registers it. `sourceId` is an ordinary positive NUMBER; `sourceKind` is the explicit namespace discriminator (for example, all WebSocket/TCP/UDP connections use `'socket'`); `eventName` identifies the event (for example, `'receive'`, `'close'`, or `'error'`). The handler block must declare one parameter for the event payload plus one parameter for each bound argument. Bound arguments are deep-copied at registration. The runtime keeps an owned bytecode snapshot, so the registering task/VM may finish and be destroyed while the subscription remains active.

Each queued event is dispatched by the runtime scheduler as a new ordinary task with a fresh VM/VCX. The event payload is passed first, followed by the registration-time bound arguments. The producer only publishes an owned event value; it never executes FlatL code directly.

The recommended API is a single `task.spawnFor()` call with an optional options OBJECT immediately after the handler block:

```flat
options = { mode: 'parallel', maxActive: 4, queueCapacity: 100, overflow: 'dropOldest' }
subscriptionId = task.spawnFor(wsId, 'socket', 'receive', #onMessage, options, userId)

#onMessage(event, userId)
  -- process the event payload
  OK
```

Supported options and defaults:

| Option | Type / values | Default | Behavior |
|---|---|---|---|
| `mode` | STRING: `parallel` or `serial` | `parallel` | `serial` runs at most one non-terminal handler task per subscription and preserves FIFO dispatch; `parallel` allows concurrent handler tasks |
| `maxActive` | Positive whole NUMBER | No additional concurrency cap | Maximum number of non-terminal handler tasks for a `parallel` subscription. `1` gives bounded single-handler concurrency. `serial` always caps at one, regardless of this value |
| `queueCapacity` | Positive whole NUMBER | `256` | Maximum number of queued, not-yet-dispatched events for this subscription. Active handler tasks do not occupy queue slots |
| `overflow` | STRING: `wait`, `dropOldest`, `dropNewest`, or `error` | `wait` | Defines what happens when the pending-event queue is full |

Overflow behavior:

- `wait`: the publisher receives the backpressure result (`-2` from the C publish API) and must retain/retry the event after queue capacity becomes available. It does not block the FlatL scheduler or host callback.
- `dropOldest`: discard the oldest event still waiting in this subscription's queue, then enqueue the new event. Already-dispatched/running handlers are not affected.
- `dropNewest`: discard the new event for this full subscription; other matching subscriptions may still receive it.
- `error`: reject publication with the hard-overflow result (`-3` from the C publish API). Fan-out is atomic for WAIT/ERROR subscriptions: no matching queue is modified when one of them rejects the publication.

If no options OBJECT is supplied, `task.spawnFor()` uses `parallel`, the default concurrency setting, a queue capacity of 256 and `wait` overflow. The options OBJECT is recognized when it contains one or more of `mode`, `maxActive`, `queueCapacity` or `overflow`; unknown keys are rejected. Place the options OBJECT before bound arguments. For compatibility, objects without any recognized option key remain ordinary bound arguments.

`task.spawnForMode()` and `task.spawnForLimit()` remain available as compatibility helpers for existing code, but new code should use `task.spawnFor()` with the options OBJECT. The mode helper accepts `parallel` or `serial`; the limit helper additionally accepts a positive whole `maxActive` NUMBER. These helpers use the same runtime policy implementation.

`task.stopFor(subscriptionId)` removes a subscription and releases its queued events and copied bound arguments. It returns BOOL true on success. Removal is rejected while a dispatch is in progress or any handler task created by the subscription is still non-terminal. The subscription ID is distinct from task IDs created for individual event-handler executions.

The state numbers match the public C enum in `include/flat_task.h`:

| Value | State |
|---:|---|
| 1 | CREATED |
| 2 | RUNNABLE |
| 3 | RUNNING |
| 4 | WAITING |
| 5 | COMPLETED |
| 6 | FAILED |
| 7 | CANCELLED |

`task.wait()` does not block the OS thread. Its native handler asks the runtime to record the dependency, and the VM yields after the native instruction has completed and advanced its instruction pointer. The native call is not repeated after wakeup. A failed or cancelled target also wakes its waiters; use `task.state(id)` after the wait to distinguish its outcome. A task cannot call `task.wait()` while it owns an explicit Channel transaction; the request fails rather than suspending while holding the transaction. See [channel.md](./channel.md) for the scheduler transaction-priority rule.

`task.result(taskId)` is available only after the target reaches a terminal state. The returned object contains the numeric task `state`, VM `exitCode`, numeric `termination` (`FlatTermination`), and a `diagnostic` string. The diagnostic is truncated to `FLAT_INLINE_STR_MAX` bytes for this native object. This is execution metadata, not a user-defined block return value; FlatL blocks do not gain a new return-value convention from this API. The C API `flat_runtime_task_result()` continues to expose the complete fixed-size `FlatVmResult` diagnostic.

Task natives are only available when the VM has a task bridge installed by a runtime-owned task. The FLEditor WASM adapter uses the single-threaded cooperative scheduler; it does not enable `flat_runtime_workers_start()` or browser Web Workers. Native runtime worker mode is a separate host capability and is subject to platform and native-module capability checks. Wait-cycle detection, Channel contention retry, cancellation, and worker-pool lifecycle are implemented and covered by the FLVM integration suite. Runtime Channels are available through `use native channel`; see [channel.md](./channel.md). A Channel ID can be passed as a normal numeric argument to `task.spawn()`.

Runnable FLEditor examples are shipped under `docs/fleditor_site/examples/task_channel/`: `parallel_inventory.fl`, `transaction_rollback.fl`, and `task_results.fl`. The Node.js WASM smoke test executes all three through the same browser-facing run API.

## Runtime event-handler registration (C API)

The runtime event broker now also exposes `flat_runtime_event_subscribe_handler()`. It registers a persistent handler against `(sourceId, sourceKind, eventName)`, validates that the target block accepts one event parameter plus the bound arguments, and deep-copies those bound arguments into runtime-owned storage. The scheduler automatically converts queued events for registered handlers into ordinary runnable tasks. The event payload is parameter 0; bound arguments follow it.

The C API clones the supplied bytecode into subscription-owned storage; the caller's bytecode may be released after registration returns. `flat_runtime_event_unsubscribe()` rejects removal while a handler dispatch is in progress and releases the bytecode snapshot, stored argument copies and queued events. Archive 364 exposes the FlatL `task.spawnFor()` and `task.stopFor()` native wrappers over this runtime API. Archive 366 adds `task.spawnForMode()` and archive 367 adds `task.spawnForLimit()` as compatibility helpers. Archive 368 consolidates their configuration in the `task.spawnFor()` options OBJECT and exposes bounded queue capacity plus overflow policies. Archive 369 adds the shared C-side socket event adapter boundary; concrete transport modules remain future work.

### Socket event sources

Socket event producers use the shared `sourceKind` value `'socket'` for WebSocket, TCP and UDP connections. The connection ID is an ordinary NUMBER. Standard event names are `open`, `receive`, `close` and `error`; see [Socket event adapter contract](../SOCKET_EVENT_ADAPTER.md). The C-side adapter boundary and a synchronous native libcurl WebSocket transport foundation are available. The transport is not yet exposed as `sockets.wsConnect()` and is not yet connected to asynchronous `task.spawnFor()` event delivery; see [Native WebSocket transport foundation](../WEBSOCKET_TRANSPORT.md).
