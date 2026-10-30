# Native module `math.trig`

Trigonometric and basic math helpers (libm). **Optional** — declare before use:

```text
use native math.trig with trig
```

## Functions

All take one **number** argument (expression inside `(...)`). Results are **numbers**.

| Call | Description |
|------|-------------|
| `sin(x)` | Sine (radians) |
| `cos(x)` | Cosine (radians) |
| `tan(x)` | Tangent (radians) |
| `sqrt(x)` | Square root |

Use the **`math.trig`** prefix or an alias from `with`:

```text
use native math.trig with trig

#main
  trig.sin(0) -> s
  math.trig.sqrt(9) -> r
  io.outln(s)
  io.outln(r)
  OK
```

Assignment form is also allowed:

```text
  x = math.trig.cos(0)
```

Invalid or non-numeric arguments set I/O error status (same family as other native parse failures).

## Source layout

| File | Role |
|------|------|
| `src/lib/mod_math_trig.c` | Handlers |
| `include/flat_native.h` | IDs `FLAT_NATIVE_MATH_TRIG_*` |
| `src/lib/native_registry.c` | Name table |

Build:

- **Plugin (Linux/macOS/BSD):** `bin/libflat_math_trig.so` — built by `make all`; loaded lazily at first call.
- **Windows:** handlers linked statically into `flvm` until plugin loading is ported.

Plugin and host link with **`-lm`** where libm is used.
