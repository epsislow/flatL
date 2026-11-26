# Runtime logging (flvm)

`flvm` has opt-in runtime logging categories intended for task and Channel diagnostics.
Logging is disabled by default.

## CLI

```sh
flvm program.fb --show-tasks-log
flvm program.fb --show-channel-log
flvm program.fb --show-tasks-log --show-channel-log
```

The categories are independent. When enabled, their events are written to `stdout`.
The task category is reserved for task lifecycle producers. The Channel category emits
`create`, `set`, `var`, `lock`, `unlock`, `revert`, and `destroy` events from the runtime
Channel API. Records include IDs, key names, value types, action/outcome, and rejection
reason when applicable; stored values are never included. Events are emitted only when
the corresponding category is enabled.

By default, events are written live. To use bounded buffered output instead:

```sh
flvm program.fb --show-tasks-log --show-channel-log --log-buffer-size 4096
```

The buffer size is in bytes and must be between 256 bytes and 16 MiB. Buffered
records are printed when the VM run finishes. If the buffer fills, new records are
dropped rather than allocating more memory; a final `{log: dropped, count: N}`
record reports the number dropped. This is a bounded append buffer, not a ring
buffer: it preserves the earliest records in the buffer.

## Event format

The central logger formats one structured, line-oriented record per event. For
example, future task/channel producers can emit records such as:

```text
{seq: 1, taskId: 3, chanId: 21, event: "set", var: "x", type: "list", action: "stored"}
{seq: 2, chanId: 21, event: "destroy", reason: "active-task-reference", action: "rejected"}
```

`seq` is a monotonically increasing event sequence within one `flvm` process.
Optional fields are omitted when not applicable. String values are quoted and
escaped. The logger is currently designed for the cooperative, single-threaded
runtime; it must be made thread-safe before the parallel worker-pool phase.

## Implementation status

- [x] Central event formatter and category filtering.
- [x] Logging disabled by default; no event formatting or buffer allocation when disabled.
- [x] Live output and bounded optional buffering.
- [x] CLI switches for task and Channel categories.
- [ ] Task lifecycle event producers (still planned).
- [x] Channel operation/transaction event producers (archive 282).
- [x] Runtime test verifies Channel event emission (archive 282).
