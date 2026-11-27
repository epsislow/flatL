# Native `channel`

The built-in `channel` module exposes explicit runtime-owned shared data containers to VMs executing as tasks under one `FlatRuntime` scheduler.

```flat
use native task
use native channel
use native math

#main
  shared = channel.create()
  channel.set(shared, 'counter', 10)
  child = task.spawn(#worker, shared)
  task.wait(child)

  value = channel.var(shared, 'counter')
  io.outln(value)
  channel.destroy(shared)
  OK

#worker(shared)
  channel.lock(shared)
  value = channel.var(shared, 'counter')
  math.calc('value + 1') -> value
  channel.set(shared, 'counter', value)
  channel.unlock(shared)
  OK
```

Arithmetic remains explicit in FlatL: calculate with `math.calc(...) -> variable`, then pass the variable to `channel.set()`. Infix expressions are not accepted as arbitrary native arguments.

A native host must create/start a root `FlatTask` and drive `flat_runtime_schedule()`. Channel natives are unavailable to the direct `flat_vm_run_opts()` and `flat_runtime_run()` paths because those paths do not install a task bridge. FLEditor WASM uses `flat_runtime_run_scheduled()` to create a runtime-owned root task, drive the cooperative scheduler, and clean up tasks and Channels after the run. The browser path is single-threaded; it does not start an OS worker pool.

## Functions

| Call | Arguments | Returns | Behavior |
|---|---|---|---|
| `channel.create()` | None | NUMBER — runtime-local Channel ID | Creates an empty Channel owned by the current `FlatRuntime` |
| `channel.set(channelId, name, value)` | Positive whole Channel ID, string key, supported value | BOOL — `true` | Deep-copies the value into Channel-owned storage; if another task owns a transaction, the native call waits and retries |
| `channel.var(channelId, name)` | Positive whole Channel ID and string key | Copy of stored value | Deep-copies the value into the current VM's pools; if another task owns a transaction, the native call waits and retries |
| `channel.lock(channelId)` | Channel ID | BOOL — `true` | Begins a transaction owned by the current task and snapshots the Channel; if another task owns it, the native call waits and retries |
| `channel.unlock(channelId)` | Channel ID | BOOL — `true` | Commits the current transaction and releases its lock |
| `channel.revert(channelId)` | Channel ID | BOOL — `true` | Restores the transaction snapshot but keeps the lock held |
| `channel.destroy(channelId)` | Channel ID | BOOL — `true` | Destroys an unlocked Channel when no other task holds a registered handle reference |

Channel IDs are numeric handles, not a new FlatL value type. They can be passed through `task.spawn(#worker, channelId)` as ordinary NUMBER arguments. The ID is copied; the Channel itself remains owned by the runtime and is shared explicitly by all tasks that receive the handle.

`set` and `var` support NUMBER, BOOL, NULL, STRING, LIST, and OBJECT. LIST and OBJECT graphs are recursively deep-copied, including nested collections. FILE handles are rejected because they belong to a VM-local file table. A failed set leaves the previous value unchanged. Keys must be nonempty and no longer than `FLAT_VAR_NAME_MAX` bytes.

## Transaction and scheduler behavior

Each ordinary operation is serialized by the Channel's platform `FlatMutex` gate. A transaction is owned by a nonzero task ID:

- The owner can call `set` and `var` while the transaction is held.
- A different task accessing that Channel through the FlatL task natives enters `WAITING` on the transaction owner and retries the same native instruction after the owner commits or aborts. The C API continues to return `-2` for a busy operation because it has no implicit scheduler context.
- `unlock` commits and releases ownership.
- `revert` restores the original snapshot and keeps ownership, so more edits can be made before commit.
- A Channel cannot be destroyed while a transaction is active.
- If the owner completes, fails, or is cancelled, its open transactions are automatically rolled back and unlocked.

To keep a transaction atomic across multiple VM instructions, the cooperative scheduler gives runnable transaction owners priority until they release their transactions. A contending task records a dependency on the current transaction owner. This edge participates in the same wait-cycle detection as `task.wait()`, so a transaction owner cannot create a cycle by waiting (directly or indirectly) on a task that is waiting for its Channel. `task.wait()` is rejected while the current task owns any Channel transaction; this avoids suspending a task while it holds a lock. In cooperative mode (including FLEditor WASM), the scheduler is single-threaded. Supported native hosts may separately enable the runtime worker pool; that mode uses the platform synchronization layer and native-module capability gates. This priority rule is not, by itself, a general thread-safety guarantee.

`channel.destroy()` is explicit. A direct numeric Channel ID passed as a `task.spawn()` argument is registered as a child-task reference before the child becomes runnable. References are released when that task completes, fails, is cancelled, or is destroyed. A task may destroy a Channel when it is the only remaining holder; a host-level destroy is rejected while any task reference exists. Since handles are ordinary NUMBER values, any direct numeric spawn argument equal to a live Channel ID is conservatively treated as a handle (even if the caller intended an unrelated number). Nested IDs inside LIST/OBJECT arguments are not discovered. Runtime destruction returns busy while any Channel remains registered.

## Event logging

When `flvm` is started with `--show-channel-log`, the runtime emits structured events for `create`, `set`, `var`, `lock`, `unlock`, `revert`, and `destroy`. Records include task/Channel IDs when available, variable name, value type, action, and a short rejection reason. Stored values are deliberately never logged. Logging is disabled by default; `--log-buffer-size N` enables bounded buffering.

## Diagnostics

Invalid IDs, missing keys, invalid values, and transaction misuse use `channel.*` panic details. A task-native set/var/lock blocked by another task's transaction is not a failure: it enters `WAITING` and retries after wakeup. Direct C API callers still receive the documented busy result when they do not have a scheduler continuation.
