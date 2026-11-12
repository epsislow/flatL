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

## `calc(expr)` — arithmetic expressions

Evaluate a **STRING** expression and return a **NUMBER**. Use this when you need infix operators (for example `B = math.calc('A * 2')`); plain assignment RHS still accepts only a single literal or variable, not `A * 2`.

| Call | Returns |
|------|---------|
| `calc(expr)` | **NUMBER** — result of evaluating **expr** |
| `calc(expr, "u8")` | **NUMBER** — same, but range-checked for the given **numeric** type (`type.calc.range` on overflow) |

**Argument `expr`:** a **STRING** (literal `'…'` / `"…"`, or a variable holding STRING / REF). The string body is the expression text (no surrounding quotes).

**Variables** in the expression must evaluate to **NUMBER**, including **object paths** (same rules as expression suffixes elsewhere):

```text
M.calc('o.qty + o.stocks[1].qty + idx')
```

Path tokens: `ident` + ( `.segment` | `[index]` )*; leaf must be **NUMBER** or **`math.calc.type`**.

### Operators (precedence, high → low)

| Level | Operators | Notes |
|-------|-----------|--------|
| 1 | `^` | Power, **right-associative** (`2 ^ 3 ^ 2` → `2 ^ (3 ^ 2)`). Same as `math.pow`. |
| 2 | Unary `+` / `-` | Unary minus binds **below** `^` (`-2 ^ 2` → **-4**, not 4). |
| 3 | `*`, `/`, `%`, `%%` | `%` = **fmod** (like `math.mod`); `%%` = **floor_mod** (like `math.floor_mod`). |
| 4 | `+`, `-` | Addition / subtraction. |
| Grouping | `( … )` | Overrides precedence. |

Whitespace between tokens is ignored. **Identifiers** (`A`, `foo_bar`) refer to program variables and must be **NUMBER**.

### Examples

```text
use native math with m

#main
  A = 6
  m.calc('(A + 2) * 3') -> r1
  m.calc('-2 ^ 2') -> r2
  m.calc('-7 %% 3') -> r3
  io.outln(r1)
  io.outln(r2)
  io.outln(r3)
  OK
```

Output:

```text
24
-4
2
```

### PANIC sub-reasons (no `?` / `~`)

On failure without line modifiers, the VM reports **`unhandled PANIC:`** with a dotted reason:

| Situation | Reason |
|-----------|--------|
| Invalid expression (bad token, unbalanced `)`, trailing garbage) | `math.calc.syntax` |
| Unknown identifier in the expression | `math.calc.unknown_var` |
| Variable exists but is not **NUMBER** | `math.calc.type` |
| Division or modulo by zero (`/`, `%`, `%%`) | `math.calc.divzero` |
| Bad `calc` argument (not STRING, parse error, empty string) | `math.calc.arg` |
| Non-finite result (NaN / infinity from evaluation) | `math.calc.range` |

With **`?`** or **`~`**, behavior matches other natives (silent skip or `#or` branch); sub-reasons appear only when a panic would actually run (trace / stderr).

Implementation: `src/lib/flat_math_calc.c`, handler in `src/lib/mod_math.c`.

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
| `src/lib/flat_math_calc.c` | `calc` expression parser |
| `src/lib/plugin_math_entry.c` | Plugin entry |
| `include/flat_native.h` | IDs `FLAT_NATIVE_MATH_*` |

Build: **`bin/libflat_math.so`** on Unix (`make all`).
