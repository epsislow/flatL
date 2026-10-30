# Native module `math`

Scalar **libm** helpers, simple **list aggregations**, and numeric constants. **Optional** — declare before use:

```text
use native math with m
```

Does **not** load `math.trig` or `math.stats`; each optional module needs its own `use native` line.

## Syntax

| Form | Example |
|------|---------|
| Arrow binding | `m.sqrt(9) -> r` |
| Assignment | `n = math.pow(2, 10)` |
| Canonical name | `math.floor_mod(-7, 3) -> r` |
| Alias (with `with m`) | `m.sum(xs) -> s` |

## Constants (no arguments)

| Call | Returns |
|------|---------|
| `num_pi()` | π |
| `num_tau()` | 2π |
| `num_e()` | Euler’s **e** |

π and τ are also available from **`math.trig`** for backward compatibility.

## Scalar libm

| Call | Description |
|------|-------------|
| `pow(x, y)` | Power |
| `sqrt(x)` | Square root (**x ≥ 0**) |
| `exp(x)` | e^x |
| `log(x)`, `log10(x)` | Natural / base-10 log (**x > 0**) |
| `floor(x)`, `ceil(x)` | Rounding toward −∞ / +∞ |
| `round(x)` | Half away from zero |
| `mod(x, y)` | **fmod** — remainder with sign of **x** |
| `floor_mod(a, b)` | Euclidean modulo (**b ≠ 0**); e.g. `floor_mod(-7, 3)` → 2 |

## Aggregations on one list

Argument **`xs`**: **LIST** of **NUMBER**, length **≥ 1**. Empty list → I/O error.

| Call | Returns |
|------|---------|
| `sum(xs)` | Sum |
| `min(xs)` | Minimum |
| `max(xs)` | Maximum |

## Two scalars (distinct names)

| Call | Returns |
|------|---------|
| `sum2(a, b)` | **a + b** |
| `min2(a, b)` | Minimum of **a**, **b** |
| `max2(a, b)` | Maximum of **a**, **b** |

### Example

```text
use native math with m
use native list with L

#main
  L.pack(1, 2, 3) -> xs
  m.sum(xs) -> s
  m.min2(3, 7) -> lo
  m.floor_mod(-7, 3) -> r
  io.outln(s)
  io.outln(lo)
  io.outln(r)
  OK
```

Output:

```text
6
3
2
```

## Source layout

| File | Role |
|------|------|
| `src/lib/mod_math.c` | Handlers |
| `src/lib/plugin_math_entry.c` | Plugin entry |
| `include/flat_native.h` | IDs `FLAT_NATIVE_MATH_*` |

Build: **`bin/libflat_math.so`** on Unix (`make all`).
