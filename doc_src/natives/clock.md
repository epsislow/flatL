# Native module `clock`

**Monotonic timing for elapsed-time measurements and profiling.** `clock` is a core native module, always embedded in the runtime. No `use native clock` declaration is required.

Unlike [`date`](./date.md), which represents calendar time and Unix timestamps, `clock` measures elapsed time using a monotonic source. Its origin is unspecified and must not be interpreted as a date or compared with `date.now()`.

## Functions

| Call | Args | Returns | Meaning |
|------|------|---------|---------|
| `clock.now()` | — | NUMBER (milliseconds) | Monotonic time in milliseconds, including fractional milliseconds |
| `clock.now_us()` | — | NUMBER (microseconds) | Monotonic time in microseconds |

Both functions read the same monotonic clock. The values are useful for measuring durations by subtraction:

```text
#main
  start = clock.now()

  -- Code to measure
  doSomething()

  finish = clock.now()
  elapsed = finish - start

  io.outln('Elapsed: ' + elapsed + ' ms')
```

For finer reporting, use microseconds:

```text
#main
  start = clock.now_us()

  -- Code to measure
  doSomething()

  elapsed = clock.now_us() - start
  io.outln('Elapsed: ' + elapsed + ' us')
```

## Important behavior

- The clock is **monotonic**: it does not follow wall-clock adjustments, daylight-saving changes, or `date.run_offset`.
- The starting value is arbitrary and may differ between processes and machines.
- Only differences between readings are meaningful.
- `clock.now()` returns milliseconds as a NUMBER and may contain a fractional part (for example, `12.375`).
- `clock.now_us()` returns microseconds as a NUMBER.
- Microsecond units do not guarantee one-microsecond hardware or operating-system resolution. Actual precision depends on the platform and runtime.

## Platform implementation

| Platform | Source |
|----------|--------|
| Windows | `QueryPerformanceCounter` |
| POSIX platforms | `clock_gettime(CLOCK_MONOTONIC)` |
| WebAssembly | Monotonic source provided by the host runtime where available |

## `date` versus `clock`

| Module | Purpose | Typical use |
|--------|---------|-------------|
| `date` | Calendar time / Unix timestamps | Dates, formatting, parsing, scheduling |
| `clock` | Monotonic elapsed time | Benchmarks, profiling, timeouts, durations |

Do not use `date.now()` to measure short operations: it has second-level granularity. Multiplying a `date` timestamp by 1000 changes the unit, not the precision.

## Source layout

| File | Role |
|------|------|
| `src/common/flat_time.c` | Cross-platform monotonic clock |
| `src/lib/mod_date.c` | Native handlers for `clock.*` and `date.*` |
| `src/lib/native_plugin_host.c` | Embedded native registration |

## Tests

**`test/cases/clock/`** — monotonic ordering and API smoke tests.
