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

## Implementation notes

- v1: optional modules are **statically** linked into `flvm` (same binary as core). The compiler still rejects undeclared optional calls.
- Future: lazy load from shared libraries and optional manifest bytes in `.fb` (see `.cursor/plans/native-modules.plan.md`).
