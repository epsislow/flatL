# Native module `math.stats`

Descriptive statistics on **lists of numbers**. **Optional** — declare before use:

```text
use native math.stats with stats
```

For **sum**, **min**, **max**, and **e**, use module **`math`** (`sum`, `min`, `max`, `num_e`). For list helpers, use **`list`**.

## Syntax

Calls use the usual native forms:

| Form | Example |
|------|---------|
| Arrow binding | `stats.mean(xs) -> m` |
| Assignment | `m = math.stats.mean(xs)` |
| Canonical name | `math.stats.median(xs) -> med` |
| Alias (with `with stats`) | `stats.var(xs, 3) -> v` |

**Input:** a **LIST** whose elements are **NUMBER**. Empty lists, non-numeric elements, or bad divisors produce I/O error (same family as other natives).

## Functions (one list argument)

| Call | Returns | Description |
|------|---------|-------------|
| `mean(xs)` | NUMBER | Arithmetic mean |
| `median(xs)` | NUMBER | Middle value (sorted; average of two middles if even length) |

### Example

```text
use native math.stats with stats

#main
  xs = [1, 2, 3, 4]
  stats.mean(xs) -> m
  stats.median(xs) -> med
  io.outln(m)
  io.outln(med)
  OK
```

Output:

```text
2.5
2.5
```

## Functions (list + divisor)

| Call | Returns | Description |
|------|---------|-------------|
| `var(xs, n)` | NUMBER | Variance: Σ(xᵢ − mean)² / **n** |
| `stdev(xs, n)` | NUMBER | √(var(xs, n)) |

**`n`** is the **divisor you choose** (must be > 0). The VM does not infer list length.

| Goal | Typical `n` |
|------|-------------|
| Population variance | number of elements |
| Sample variance | number of elements − 1 |

### Example (sample variance, 4 values)

```text
use native math.stats with stats

#main
  xs = [1, 2, 3, 4]
  stats.var(xs, 3) -> v
  stats.stdev(xs, 3) -> s
  io.outln(v)
  io.outln(s)
  OK
```

## Source layout

| File | Role |
|------|------|
| `src/lib/mod_math_stats.c` | Handlers |
| `src/lib/plugin_math_stats_entry.c` | Plugin entry |
| `include/flat_native.h` | IDs `FLAT_NATIVE_MATH_STATS_*` |

Build: **`bin/libflat_math_stats.so`** on Unix (`make all`); Windows links handlers statically into `flvm` until plugin load is ported.
