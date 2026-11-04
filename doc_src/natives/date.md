# Native module `date`

**Calendar, clock, and formatting** aligned with sside **`fdate`** / **`parseRelativeDate`**. **Core** — always linked into `flvm` / `flatl`; call `date.*` directly. Optional alias:

```text
use native date with D
```

Human-facing **string profiles** (`DD.MM.YYYY`, …) live on **`str.to_date`** / **`str.to_time`** / **`str.to_datetime`** — see [str.md](./str.md). **`json.*`** does **not** parse datetimes; JSON fields stay **STRING** until you call **`date.*`** or **`str.to_*`**.

## Representations

| Concept | VM type | Example |
|---------|---------|---------|
| Calendar day (naive) | STRING | `"2026-09-30"` |
| Time of day (naive) | STRING | `"15:30:00"` |
| Datetime (naive or with offset in text) | STRING | `"2026-09-30T15:30"`, `"2026-09-30T12:00:00+03:00"` |
| Absolute instant | NUMBER | Unix **seconds UTC** (e.g. `1790769600`) |
| User display | STRING | Output of **`date.format`** |

There is **no** `VAL_DATE` tag. Use **NUMBER** seconds for compare, add, and epoch math.

## Line modifiers and errors

See [SPECIFICATION.md](../SPECIFICATION.md) §1.2.2. On recoverable failure (invalid parse, bad offset, bad format input, …):

| Modifier | Effect |
|----------|--------|
| *(none)* | **PANIC** — reason **`date.<fn>`** (e.g. `date.format`) |
| **`?`** | No bind; next line runs |
| **`~`** | No bind; **`#or.*`** for that line |

## Run offset (`±HH:MM`)

| Mechanism | Role |
|-----------|------|
| **`date.run_offset('+03:00')`** | Sets **local wall** offset for **`date.format`** tokens without **`U`**, and for **`date.today()`** |
| **`date.run_offset('Z')`** / **`'+00:00'`** / **`''`** | Reset to UTC wall (no run offset) |
| **`FLAT_DATE_RUN_OFFSET=+03:00`** | Same as run offset at VM start (before first **`date.run_offset`**) |
| **`FLAT_DATE_NOW_SEC=<n>`** | Fixed **now** for tests (`date.now()`, **`parse_auto('now')`**, **`+Nd`** anchors) |

**No IANA** time zones (`Europe/…`) — only fixed offsets in strings or run offset.

Token **`Z`** in a format string returns **UTC − local** offset in minutes (same convention as sside **`getTimezoneOffset`**), using the active run offset as “local”.

## Function reference

### Format (`date.format`)

| Call | Args | Returns | Panic (no modifier) |
|------|------|---------|---------------------|
| `format(src)` | STRING or NUMBER (sec/ms) | STRING | **`date.format`** — invalid `src` |
| `format(src, fmt)` | + format pattern | STRING | **`date.format`** — invalid `src` |

Default format when the second argument is omitted: **`YYYY-MM-DD`**.

Format tokens (longest match; **`U`** = UTC component):

| Token | Meaning |
|-------|---------|
| `ISO` | `YYYY-MM-DDTHH:mm:ss.sssZ` (UTC) |
| `YYYY`, `MM`, `DD`, `HH`, `hh`, `mm`, `ss`, `A` | Local wall (run offset or UTC if unset) |
| `YYYYU`, `MMU`, `DDU`, `HHU`, `hhU`, `mmU`, `ssU`, `AU` | UTC components |
| `Z` | Offset minutes (see above) |

```text
#main
  date.format(1790769600000, 'DD.MM.YYYY') -> label
  io.outln(label)
  OK
```

### Parse

| Call | Args | Returns | Panic (no modifier) |
|------|------|---------|---------------------|
| `parse_auto(s)` | STRING or NUMBER | NUMBER (sec UTC) | **`date.parse_auto`** |
| `parse(s, fmt)` | STRING + strict pattern | NUMBER (sec UTC) | **`date.parse`** |

**`parse_auto`** accepts **`now`**, **`+2d`**, **`+2d+3h`**, ISO day/datetime, **`Z` / `±HH:MM`**, and numeric **sec** or **ms** (large magnitudes treated as ms). Anchored day + suffix: **`2026-09-30-20s`**.

**`parse`** matches **`fmt`** literally (tokens **`YYYY`**, **`MM`**, **`DD`**, **`HH`**, **`mm`**, **`ss`**, fixed punctuation).

### Clock

| Call | Args | Returns | Panic (no modifier) |
|------|------|---------|---------------------|
| `now()` | — | NUMBER (sec UTC) | — |
| `today()` | — | STRING `YYYY-MM-DD` | — |

### Arithmetic and compare

| Call | Args | Returns | Panic (no modifier) |
|------|------|---------|---------------------|
| `add(base, mod)` | STRING or NUMBER, modifier (`+2d`, …) | Same kind as `base` | **`date.add`** |
| `cmp(a, b)` | STRING or NUMBER | **−1**, **0**, **1** | **`date.cmp`** |

### Seconds and time-of-day

| Call | Args | Returns | Panic (no modifier) |
|------|------|---------|---------------------|
| `to_seconds(x)` | STRING or NUMBER | NUMBER (sec) | **`date.to_seconds`** |
| `from_seconds(n)` | NUMBER | STRING ISO **`…Z`** | **`date.from_seconds`** |
| `to_ms(x)` | STRING or NUMBER | NUMBER (ms) | **`date.to_ms`** |
| `from_ms(n)` | NUMBER | STRING ISO **`…Z`** | **`date.from_ms`** |
| `time_to_sec(t)` | STRING `HH:mm` / `HH:mm:ss` | NUMBER (0…86399) | **`date.time_to_sec`** |
| `sec_to_time(n)` | NUMBER (0…86399) | STRING `HH:mm:ss` | **`date.sec_to_time`** |

### Run configuration

| Call | Args | Returns | Panic (no modifier) |
|------|------|---------|---------------------|
| `run_offset(offset)` | STRING `±HH:MM` or **`Z`** | — (no bind) | **`date.run_offset`** — invalid offset |

## Who does what

| Module | Role |
|--------|------|
| **`json.*`** | Transport JSON; datetime fields remain **STRING** |
| **`str.to_date` / `to_time` / `to_datetime`** | Fixed human profiles → canonical **STRING** |
| **`date.*`** | Parse, format, clock, epoch seconds, offset |

## Source layout

| File | Role |
|------|------|
| `src/lib/flat_date.c` | Parse/format core |
| `src/lib/mod_date.c` | Native handlers |
| `include/flat_date.h` | Shared date API |
| `include/flat_native.h` | IDs `FLAT_NATIVE_DATE_*` |

## Tests

**`test/cases/date/`** — 75 cases (001–075). Regenerate scaffold: **`python3 scripts/gen_date_tests.py`**. See [test/README.md](../../test/README.md).
