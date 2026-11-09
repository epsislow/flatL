# User modules (`.fmod`) — v0.4

Precompiled-style **FlatL modules** linked at compile time via `use fmod`.

## Module file (`.fmod`)

Same syntax as a `.fl` library, plus:

- `mod('id')` — manifest id (unique slug for impl block names)
- `export sym1, sym2, …` — symbols visible to importers
- `def` + `func:executor` — as in user functions (v0.3)
- **No** `#main`

## Import (entry program only)

```flat
use fmod "lib/mul2.fmod" as m
use "blocks/executor.fl" as #fl_
use native vcx with V

#main
  m.mul2(3) -> x
  io.outln(x)
  OK
```

The entry program must still pull in the **executor** blocks (`use … executor.fl`) and `use native vcx with V` when calling imported functions (same as v0.3 user `def` calls). The `.fmod` file declares its own `func:executor` label but does not link `.fl` dependencies into the final binary by itself.

- Path is resolved like `use "…" as #Prefix` (relative to the importing file).
- `as` name is a plain identifier (`m`, `image`, …), not a `#` block prefix.
- Exported defs are called as `import_name.function_name(…)`.

## Linking

`flc` parses each `.fmod`, checks `export` vs `def`, merges blocks and functions into one FGIR program, then emits a single `.fb` (same as multi-`use` for `.fl`).

## Not in v0.4

- Separate bytecode artifact-only `.fb` as `.fmod` container (source `.fmod` is the supported format)
- `use fmod` inside `.fl` modules (entry only)
- Runtime `module.load`

See roadmap §11 in `functions-executors-v0.3-roadmap.plan.md` and **AM-4** for later work.
