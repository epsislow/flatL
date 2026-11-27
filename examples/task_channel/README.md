# Task and Channel examples

These programs are standalone FlatL entry points and can be opened in FLEditor.

- `parallel_inventory.fl` — starts three cooperative workers that reserve stock from one shared Channel. Each read/modify/write sequence is protected by a Channel transaction.
- `transaction_rollback.fl` — demonstrates `channel.lock()`, `channel.revert()` and `channel.unlock()`; the parent sees only the committed value.
- `task_results.fl` — waits for one successful and one failed child, then inspects `task.state()` and `task.result()`.

FLEditor WASM runs these examples with the cooperative, single-thread scheduler. It does not start a native worker pool or browser Web Workers.
