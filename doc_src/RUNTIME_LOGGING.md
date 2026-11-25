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
The flags are accepted by the VM now; task and Channel event producers will be added
with the task runtime and Channel implementations. Therefore, a program that does
not use those features currently emits no task or Channel events.

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
{seq: 1, taskId: 3, event: "started", block: "#worker1"}
{seq: 2, chanId: 21, taskId: 3, event: "read", var: "x", type: "list"}
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
- [ ] Task lifecycle event producers (planned with Task Runtime).
- [ ] Channel operation/transaction event producers (planned with Channels).
- [ ] `case.toml` event assertions (to be designed with the test runner changes).
