# Native module `math.trig`

Trigonometric helpers (libm). **Optional** — declare before use:

```text
use native math.trig with trig
```

For **`sqrt`**, **`pow`**, and constants **e** / full scalar math, use module **`math`**.

## Syntax

| Form | Example |
|------|---------|
| Arrow binding | `trig.sin(0) -> s` |
| Assignment | `x = math.trig.cos(0)` |
| Canonical name | `math.trig.sin_deg(90) -> s` |
| Alias (with `with trig`) | `trig.tan(0) -> t` |

Both **`trig.*`** and **`math.trig.*`** work when `with trig` is declared.

## Numeric constants (no arguments)

Call with empty parentheses `()`. Extra arguments are an error.

| Call | Returns |
|------|---------|
| `num_pi()` | π |
| `num_tau()` | 2π (tau) |

The same π and τ are also on **`math`** (`num_pi`, `num_tau`). For **e**, use **`math.num_e()`**.

### Example

```text
  trig.num_pi() -> p
  trig.num_tau() -> t
```

## Radians (default)

| Call | Description |
|------|-------------|
| `sin(x)` | Sine (x in radians) |
| `cos(x)` | Cosine (radians) |
| `tan(x)` | Tangent (radians) |

## Degrees and conversion

| Call | Description |
|------|-------------|
| `sin_deg(x)` | Sine of **x degrees** |
| `cos_deg(x)` | Cosine of **x degrees** |
| `tan_deg(x)` | Tangent of **x degrees** |
| `deg_to_rad(d)` | Degrees → radians |
| `rad_to_deg(r)` | Radians → degrees |

### Example (degrees)

```text
  trig.sin_deg(90) -> s
  trig.deg_to_rad(180) -> r
  io.outln(s)
  io.outln(r)
```

(`s` ≈ 1; `r` ≈ π.)

### Example (radians)

```text
use native math.trig with trig

#main
  trig.sin(0) -> s
  io.outln(s)
  OK
```

Output:

```text
0
```

Invalid or non-numeric arguments set I/O error status (same family as other native parse failures).

## Source layout

| File | Role |
|------|------|
| `src/lib/mod_math_trig.c` | Handlers |
| `src/lib/plugin_math_trig_entry.c` | Plugin entry |
| `include/flat_native.h` | IDs `FLAT_NATIVE_MATH_TRIG_*` |
| `src/lib/native_registry.c` | Name table |

Build:

- **Plugin (Linux/macOS/BSD):** `bin/libflat_math_trig.so` — built by `make all`; loaded lazily at first call.
- **Windows:** handlers linked statically into `flvm` until plugin loading is ported.

Plugin and host link with **`-lm`** where libm is used.
