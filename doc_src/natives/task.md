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

The host must create and start the root task through the C `FlatRuntime` / `FlatTask` API, then drive `flat_runtime_schedule()`. Calling task natives from `flat_vm_run()` or `flat_runtime_run()` (the non-task one-shot paths) fails with a task-context diagnostic because there is no owning scheduler task.

## Functions

| Call | Arguments | Returns | Behavior |
|---|---|---|---|
| `task.spawn(#block, arg1, …)` | Literal block label followed by zero or more value expressions; count must exactly match the block's named parameters | NUMBER — runtime-local task ID | Evaluates arguments in the parent, deep-copies them into the child's private VM context, and makes it RUNNABLE |
| `task.wait(taskId)` | Positive whole NUMBER task ID | BOOL — `true` when the wait request is accepted | If the target is non-terminal, the current task enters WAITING and yields to the scheduler. It resumes after the target reaches COMPLETED, FAILED, or CANCELLED. If the target is already terminal, execution continues immediately |
| `task.state(taskId)` | Positive whole NUMBER task ID | NUMBER — `FlatTaskState` numeric value | Queries a task's current lifecycle state |
| `task.result(taskId)` | Positive whole NUMBER task ID | OBJECT — `{ state, exitCode, termination, diagnostic }` | Returns a terminal task's result summary. It fails while the task is non-terminal or unknown |

`task.spawn(#worker, value1, value2, …)` binds positional values to the names declared by the target block header, for example `#worker(price, label, items)`. The number of supplied values must exactly match the block's declared parameter count; a mismatch fails the spawning task with `task.spawn.args`. Argument expressions are evaluated in the parent before the child is registered.

The child receives a fresh VM and private VCX. Only the declared parameter names are initialized; the parent's other variables are not inherited. Scalars and strings are copied, while LIST and OBJECT values are recursively deep-copied into the child's independent collection/string pools. Mutations in the child therefore do not change the parent's values; normal per-VM copy-on-write behavior continues to apply inside each VM. File handles are not transferable between VM-owned file tables and cause spawn to fail. This is value transfer by copy, not a shared Channel or cross-task COW reference.

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

Task natives are only available when the VM has a task bridge installed by a runtime-owned task. The first scheduler remains single-threaded and cooperative. Waiting-cycle detection and pthread workers remain out of scope. Runtime Channels are available through `use native channel`; see [channel.md](./channel.md). A Channel ID can be passed as a normal numeric argument to `task.spawn()`.
