# Runtime Channels (C API milestone)

**Status:** archive 282 adds Channel event logging and task-held handle lifetime protection on top of archive 281 FlatL `channel.*` natives. Channels are created by `channel.create()` and handles are passed explicitly through `task.spawn()` arguments.

## Ownership and handles

A `FlatRuntime` owns its Channels. `FlatChannelId` is a positive monotone `uint64_t` handle; IDs are not reused. A runtime cannot be destroyed while any Channel remains registered, so callers must explicitly destroy Channels first. Channel storage owns independent string and collection pools and a key/value map. A Channel cannot be destroyed while a different nonterminal task holds a registered handle reference.

The public API is declared in `include/flat_channel.h`:

- `flat_runtime_channel_create(runtime, &id)`
- `flat_runtime_channel_set(runtime, id, owner_task_id, name, value, source_coll, source_pool, source_files)`
- `flat_runtime_channel_var(runtime, id, owner_task_id, name, destination_coll, destination_pool, destination_files, &value)`
- `flat_runtime_channel_lock(runtime, id, owner_task_id)`
- `flat_runtime_channel_unlock(runtime, id, owner_task_id)`
- `flat_runtime_channel_revert(runtime, id, owner_task_id)`
- `flat_runtime_channel_destroy(runtime, id)`

`owner_task_id` is zero for host-level ordinary operations. Explicit transaction ownership requires an existing, nonterminal task ID.

## Value semantics

`set` deep-copies into Channel-owned pools. `var` deep-copies into the caller's destination pools and returns an owned `FlatValue`; the caller must release it with `flat_value_release()` using those destination pools.

Numbers, booleans, null, inline/ref strings, LIST and OBJECT are supported. LIST/OBJECT graphs are recursively copied, so neither source VM nor destination VM shares pool references with the Channel. `VAL_FILE` is rejected because file handles are VM-local. A failed set leaves the previous value unchanged.

Keys must be nonempty and no longer than `FLAT_VAR_NAME_MAX` bytes. A missing key is an error. Repeated `set` on a key replaces its previous value.

## C1 operation synchronization

Channel operations use the platform `FlatMutex` abstraction and the runtime registry/scheduler synchronization protocol. The cooperative scheduler mode (including FLEditor WASM) drives tasks on one host thread. Native hosts may separately enable the runtime-owned worker pool on supported platforms, subject to native-module capability gates and the documented concurrency audit. This does not certify every VM/native module as generally thread-safe.

## C2 explicit transactions

`lock` creates a deep snapshot and assigns logical ownership to the supplied nonzero task ID. The owner may perform multiple `set`/`var` operations. Operations by another owner return `-2` (busy) while the transaction is held.

- `unlock` commits the current working state and releases the logical lock.
- `revert` restores the original snapshot but keeps the transaction lock held, allowing further changes before commit.
- Only the owner may unlock or revert.
- A Channel cannot be destroyed while a transaction is active.

A task's active Channel transactions are automatically rolled back and unlocked when that task is completed, fails, is cancelled, or is destroyed. Rollback during task cleanup uses the retained snapshot without allocating. At the FlatL native layer, runnable transaction owners receive scheduler priority until release, and `task.wait()` is rejected while the current task owns a transaction; see [natives/channel.md](./natives/channel.md).

## Task-held handle lifetime

When a direct numeric argument to `task.spawn(#block, ...)` equals a currently registered Channel ID, the child task acquires a runtime-side handle reference before it becomes visible to the scheduler. A Channel cannot be destroyed while any other task holds such a reference. References are released automatically when the task completes, fails, is cancelled, or is destroyed. The task that is the sole remaining holder may destroy the Channel itself. Host-level `flat_runtime_channel_destroy()` has no owner exemption and therefore rejects while any task reference remains.

Because Channel IDs are ordinary NUMBER values rather than a distinct value tag, the runtime conservatively treats a direct spawn argument equal to a live Channel ID as a Channel reference. This may pin a Channel if an unrelated numeric argument happens to equal its ID. Only direct positional arguments are recognized; nested IDs inside LIST/OBJECT values are not scanned.

## Current limits

- Handle references are tracked for direct numeric `task.spawn()` arguments and for task-level Channel operations; this is not a distinct typed handle or general recursive reference-counting system.
- A non-owner that encounters an active transaction receives busy rather than entering a Channel-specific WAITING state.
- `--show-channel-log` records Channel create/set/var/lock/unlock/revert/destroy operations and their outcome, but does not log stored values.
- FLEditor WASM uses cooperative single-thread scheduling; it does not start pthreads or browser Web Workers.
- Native worker-pool execution is a separate host option and remains subject to platform validation and module capability policies; this is not a blanket thread-safety guarantee.

`flvm[26]` validates the C-level registry, copying, transactions, and rollback. `flvm[28]` validates event emission and that a spawned child pins its Channel until completion, after which the parent can destroy it.
