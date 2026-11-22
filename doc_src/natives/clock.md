# Native module `clock`

**Monotonic timing for elapsed-time measurements, profiling, and stopwatches.** `clock` is a core native module, always embedded in the runtime. No `use native clock` declaration is required.

Unlike [`date`](./date.md), which represents calendar time and Unix timestamps, `clock` measures elapsed time using a monotonic source. Its origin is unspecified and must not be interpreted as a date or compared with `date.now()`.

## Functions

| Call | Args | Returns | Meaning |
|------|------|---------|---------|
| `clock.now()` | — | NUMBER (milliseconds) | Monotonic time in milliseconds, including fractional milliseconds |
| `clock.now_us()` | — | NUMBER (microseconds) | Monotonic time in microseconds |
| `clock.create()` | — | NUMBER (resource ID) | Creates an independent stopwatch (`sw`) |
| `clock.start(id)` | NUMBER id | — | Starts or resumes a stopwatch |
| `clock.stop(id)` | NUMBER id | — | Stops it and preserves accumulated elapsed time |
| `clock.reset(id)` | NUMBER id | — | Resets elapsed time and lap history; leaves it stopped |
| `clock.elapsed(id)` | NUMBER id | NUMBER (milliseconds) | Returns accumulated time, including the current interval when running |
| `clock.running(id)` | NUMBER id | BOOL | Whether the stopwatch is running |
| `clock.lap(id)` | NUMBER id | NUMBER (milliseconds) | Records a lap and returns its duration |
| `clock.lapCount(id)` | NUMBER id | NUMBER | Number of recorded laps |
| `clock.lapLast(id)` | NUMBER id | NUMBER (milliseconds) | Duration of the most recently recorded lap |
| `clock.lapGet(id, index)` | NUMBER id, NUMBER index | NUMBER (milliseconds) | Cumulative elapsed time at a lap; indexes are zero-based |
| `clock.lapClear(id)` | NUMBER id | — | Clears lap history without changing elapsed time or running state |
| `clock.close(id)` | NUMBER id | — | Releases the stopwatch resource |

All stopwatch elapsed and lap values use milliseconds and may contain fractional values.

## Stopwatch example

```text
#main
  sw = clock.create()

  clock.start(sw)
  -- Code to measure
  doSomething()
  clock.stop(sw)

  elapsed = clock.elapsed(sw)
  io.outln('Elapsed: ' + elapsed + ' ms')

  clock.reset(sw)
  clock.close(sw)
```

`clock.create()` returns a numeric resource ID. Multiple stopwatches can be active independently:

```text
#main
  first = clock.create()
  second = clock.create()

  clock.start(first)
  clock.start(second)

  -- Work for both measurements

  clock.stop(first)
  clock.stop(second)

  first_ms = clock.elapsed(first)
  second_ms = clock.elapsed(second)

  clock.close(first)
  clock.close(second)
```

## Laps

`clock.lap(id)` records a split while the stopwatch is running. It returns the duration since the previous lap (or since `start` for the first lap). `clock.lapGet(id, index)` returns the cumulative elapsed time from the stopwatch start to that lap.

```text
#main
  sw = clock.create()
  clock.start(sw)

  -- First stage
  clock.lap(sw) -> first_lap

  -- Second stage
  clock.lap(sw) -> second_lap

  -- Third stage
  clock.lap(sw) -> third_lap
  clock.stop(sw)

  count = clock.lapCount(sw)
  last = clock.lapLast(sw)
  first_total = clock.lapGet(sw, 0)

  io.outln('Lap count: ' + count)
  io.outln('Last lap: ' + last + ' ms')
  io.outln('Elapsed at first lap: ' + first_total + ' ms')

  clock.close(sw)
```

Lap indexes are zero-based: `0` is the first lap, `1` the second, and so on. `lapLast()` returns the duration of the latest lap, while `lapGet()` returns the cumulative time at that lap.

## Resource lifecycle and behavior

- `clock.start(id)` starts a stopped stopwatch. Calling it on an already running stopwatch has no effect.
- `clock.stop(id)` accumulates the current interval. Calling it on an already stopped stopwatch has no effect.
- Starting after stopping resumes from the previously accumulated elapsed time; it does not reset the stopwatch or its lap history.
- `clock.reset(id)` clears elapsed time and all laps and leaves the stopwatch stopped.
- `clock.lap(id)` requires a running stopwatch. It records the cumulative elapsed time and returns the duration since the previous lap.
- `clock.lapClear(id)` clears only the lap history. It does not reset elapsed time or change the running state; the next lap duration is measured from the moment `lapClear()` was called.
- `clock.close(id)` releases the resource. It may be called while running; the stopwatch is discarded without needing a prior `stop()`.
- A closed or otherwise invalid ID causes the relevant `clock` operation to fail with a resource error. `mem.infoResId(id, 'clock')` returns `unknown` after close and `sw` while the stopwatch is live.
- Stopwatch IDs belong to the current VM execution. All remaining stopwatch resources are discarded when that execution ends.
- A stopwatch can store up to 256 lap records. `clock.lap()` fails if this limit is reached; `clock.lapClear()` allows recording again.
- The runtime supports up to 64 simultaneously live stopwatches per VM execution.

## Measuring durations directly

The stopwatch API is optional. For a one-off measurement, subtract two readings:

```text
#main
  start = clock.now()

  -- Code to measure
  doSomething()

  elapsed = clock.now() - start
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
- Only differences between `clock.now()` / `clock.now_us()` readings are meaningful.
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
| `clock` | Monotonic elapsed time | Benchmarks, profiling, timeouts, durations, stopwatches |

Do not use `date.now()` to measure short operations: it has second-level granularity. Multiplying a `date` timestamp by 1000 changes the unit, not the precision.

## Source layout

| File | Role |
|------|------|
| `src/common/flat_time.c` | Cross-platform monotonic clock |
| `src/lib/mod_date.c` | Native handlers for `clock.*` and `date.*` |
| `src/lib/native_plugin_host.c` | Embedded native registration |
| `src/common/vm.c` | Per-execution stopwatch resource lifetime |

## Tests

**`test/cases/clock/`** — monotonic readings, unit consistency, stopwatch lifecycle, lap recording, resource identification, and closed-ID validation.
