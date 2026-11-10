# FlatL modules (`.fmod`) — v0.4

Compile-time **user modules**: a `.fmod` file is a library unit (like `.fl`) with a manifest and **exported** `def` symbols. The entry program links them with **`use fmod "path" as alias`**.

**Prerequisites:** [USER_FUNCTIONS.md](./USER_FUNCTIONS.md) (`def`, `func:executor`, call lowering).  
**Contrast:** [SPECIFICATION.md §1.2](./SPECIFICATION.md#12-source-units-use) (`use "…" as #Prefix` for `.fl` block libraries).

---

## 1. When to use `.fmod` vs `.fl`

| Mechanism | Unit | Imports | Callable surface |
|-----------|------|---------|------------------|
| `use "lib/x.fl" as #H_` | `.fl` module | Blocks (`#H_add`, …) | `call #H_add(…) -> r` |
| `use fmod "lib/mul2.fmod" as m` | `.fmod` module | User **functions** | `m.mul2(…) -> x` |

Use **`.fmod`** when you want a small **API of named functions** (`export`) without exposing internal block labels to callers.

---

## 2. `.fmod` file format

Same body rules as a `.fl` library, plus header lines:

| Header | Required | Meaning |
|--------|----------|---------|
| `mod('id')` | Yes | Stable slug for internal impl block names (unique in the link) |
| `export sym1, sym2, …` | Yes (≥1) | `def` names visible to importers |
| `func:executor #…` | Yes | Executor block for calls inside the module (v0.3) |
| `func:varSlots N` | Optional | Unique executor VCX size hint |
| `func:callVM 0\|1` | Optional | **`0`** = desugar (default); **`1`** = **`OP_CALL_FUNCTION`** (AM-1). See [USER_FUNCTIONS.md](./USER_FUNCTIONS.md). |
| `use native …` | Optional | Same as entry/module `.fl` |
| `use "…" as #…` | Optional (parsed only) | **Not** linked into the consumer binary from the `.fmod` alone |

**Forbidden in `.fmod`:** `#main` (entry only).

### Tests (`func:callVM` + `.fmod`)

| Case | What it checks |
|------|----------------|
| `fmod/001` | Baseline `use fmod`, desugar (A) |
| `fmod/002` | `func:callVM 1` inside `.fmod` |
| `fmod/003` | FMOD B + entry B (`bump`), sequential calls |
| `fmod/004` | Two exported `def`s, both B, chained from `#main` |
| `fmod/005` | `fbdump` shows `CALL_FUNCTION` after link |
| `fmod/006` | FMOD A + entry B (mixed) |

### Minimal module (repo test)

`test/cases/fmod/001_mul2/lib/mul2.fmod`:

```flat
mod('mul2')
use "../../../../../blocks/executor.fl" as #fl_
use native math with m

func:executor #fl_executor.global.scope
export mul2

def mul2(a) -> r
  m.calc('a * 2') -> r
  return
```

---

## 3. Importing (entry program only)

**v0.4:** `use fmod` is allowed only in the **entry** `.fl` (the file passed to `flc`), not inside linked `.fl` modules.

```flat
use fmod "lib/mul2.fmod" as m
use "blocks/executor.fl" as #fl_
use native vcx with V

#main
  m.mul2(3) -> x
  io.outln(x)
  OK
```

| Rule | Detail |
|------|--------|
| Path | Resolved **relative to the importing file** (same as `use "…"`) |
| `as name` | Plain identifier (`m`, `image`, …), **not** `#Prefix` |
| Call syntax | `alias.exported_name(args) -> var` or `var = alias.exported_name(args)` |
| Qualified name | After link, the function is `m.mul2` in FGIR (not a native module) |

### Executor and VCX on the entry file

Lowering a function call uses **`V.current`**, **`V.create`**, and **`call #…_executor.…`**. The **entry** program must therefore include:

1. `use "…/blocks/executor.fl" as #fl_` (or your prefix) so `#fl_executor.global.scope` exists in the linked program.
2. `use native vcx with V` (or equivalent alias) for the desugared VCX natives.

The `.fmod` file sets its own `func:executor` for **calls inside** the module body; it does **not** pull `executor.fl` into the final binary for the **caller**. Duplicate `use executor` on entry and inside `.fmod` is normal (each unit’s header is parsed separately; only merged blocks/functions from `.fmod` affect the consumer).

---

## 4. Linking (`flc`)

```bash
bin/flc -o out.fb test/cases/fmod/001_mul2/program.fl
bin/flvm out.fb
```

Pipeline:

1. Scan entry header for `use fmod "…" as alias`.
2. Parse each `.fmod`, validate `mod('id')`, `export` ⊆ `def`, no `#main`.
3. Rename exported defs to `alias.name`; internal defs to `__<modId>_<name>`.
4. Merge functions and impl blocks into one `FgirProgram`.
5. Link entry `#main`, merge optional native units, **reclassify** `m.fn(…) ->` as `FGIR_FUNC_CALL`.
6. Validate natives, lower function calls, emit `.fb`.

`flc --dump-ir` on the entry file shows desugared `#main` and `#__fn…` impl blocks after link.

---

## 5. Errors (common)

| Message | Cause |
|---------|--------|
| `fmod: mod('id') required` | Missing `mod('…')` in `.fmod` |
| `fmod: export X has no def` | `export` lists a symbol with no `def` |
| `fmod: #main not allowed` | `#main` inside `.fmod` |
| `undeclared optional native m.mul2` | Usually fixed in v0.4: call must be classified as user function after link (update `flc`) |
| `unknown call target #fl_executor…` | Entry missing `use … executor.fl` |
| `undeclared optional native V.current` | Entry missing `use native vcx with V` |

---

## 6. Tests

Suite **`test/cases/fmod/`**:

| Case | Checks |
|------|--------|
| `001_mul2` | `m.mul2(3) -> x`, stdout `6` |

Run: `bin/run_tests fmod -v` or full `make test`.

---

## 7. Not in v0.4 (roadmap)

- `.fmod` as **binary-only** artifact (no source in repo)
- `use fmod` inside intermediate `.fl` modules (not only entry)
- Runtime `module.load` / dynamic plugins

See **AM-4** in the functions/modules roadmap (`.cursor/plans/`).

---

## 8. Related

- [USER_FUNCTIONS.md](./USER_FUNCTIONS.md) — `def`, executors, call syntax
- [TOOLCHAIN.md](./TOOLCHAIN.md) — `flc`, `use`, `make docs`
- [FLEDITOR.md](./FLEDITOR.md) — browser playground; **Docs** opens `doc_site` (includes this page after `make docs`)
