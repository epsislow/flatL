# Optional native modules

**Core natives** (`io`, `json`, `str`) are always linked into `flvm`. Call them directly; no header declaration.

**Optional natives** extend the VM with extra C modules (for example trigonometry). They must be declared before any block, and every optional call must refer to a declared module.

## Syntax

One module per line, in the header (before the first `#block`), same region as `use "file.fl" as #Prefix`:

```text
use native math.trig with trig
```

| Form | Calls allowed |
|------|----------------|
| `use native math.trig` | `math.trig.sin(...)`, `math.trig.cos(...)`, … |
| `use native math.trig with trig` | **`trig.sin(...)`** and **`math.trig.sin(...)`** |

Rules:

- Module IDs are **full names** (`math.trig`, not `math`). `use native math` is an error.
- `use native io` (or `json` / `str`) is an error — core modules need no declaration.
- Duplicate `use native` for the same module in one file is an error; the same module with the same alias merged from several linked `.fl` files is allowed.
- Conflicting aliases for the same module across files is a link error.

Declarations in **any** linked source file apply to the **whole** program after `flc` / `flatl compile` linking.

## Pilot module

See [math.trig.md](./math.trig.md).

## Runtime (lazy plugins)

When bytecode includes an optional native manifest (see [SPECIFICATION.md](../SPECIFICATION.md) §3.1), `flvm` / `flatl run`:

1. Reads declared modules from the `.fb` file.
2. On the first `OP_NATIVE` for an optional ID, **`dlopen`** the plugin (Unix) if not already loaded.
3. Rejects calls to optional IDs that are not listed in the manifest.

**Plugin file name:** module `math.trig` → `libflat_math_trig.so` (`.` → `_`).

**Search order:**

1. Directories in `FLAT_NATIVE_PATH` (`:`-separated on Unix)
2. Directory containing the `flvm` / `flatl` binary
3. `{repo}/bin/` (development default)

Build: `make all` produces `bin/libflat_math_trig.so` on Linux/macOS/BSD. Windows currently falls back to static optional handlers inside `flvm`.

**Entry symbol:** `flat_native_plugin_init` — registers handlers via a host callback (see `include/flat_native_plugin.h`).
