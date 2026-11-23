# Optional native modules

**Core natives** (`io`, `json`, `str`, `date`, `clock`, `mem`, `file`) are always linked into `flvm`. Call them directly; no header declaration required. API tables: [io.md](./io.md), [json.md](./json.md), [str.md](./str.md), [date.md](./date.md), [clock.md](./clock.md), [mem.md](./mem.md), [file.md](./file.md). Normative I/O: [IO.md](../IO.md), contexts: [IO_CONTEXTS.md](../IO_CONTEXTS.md). Optional alias: `use native mem with M` → **`mem.*` and `M.*` in that file only**.

**Optional natives** extend the VM (for example trigonometry, HTTP, CSV). Declare them in the **header** of each `.fl` file (before the first `#block`), in the same region as `use "file.fl" as #Prefix`.

## Syntax

```text
use native math.trig with trig
use native math.trig
use native mem with M
```

| Form | In that `.fl` file |
|------|---------------------|
| `use native math.trig` | Only **`math.trig.*`** |
| `use native math.trig with trig` | Only **`trig.*`** (not `math.trig.*`) |
| `use native mem with M` | **`mem.*` and `M.*`** (core keeps canonical) |

## Scope (per file)

- **`use native … with …` applies only in the source file** where it appears. Blocks defined in that file use its declarations; other linked files do **not** see your alias.
- Two libraries may both use **`with m`** for **different** modules — no conflict (`lib/a.fl`: `list with m`, `lib/b.fl`: `obj with m`).
- **FNAT** in `.fb` is still the union of optional modules used anywhere (for plugin load). Aliases are resolved at compile time per file.

## Shared alias in one file

You may attach several optional modules to the **same** alias in **one** file:

```text
use native list with kit
use native obj with kit
```

Then use **`kit.push`**, **`kit.get`**, etc. If two modules expose the same function name under **`kit`** (e.g. **`kit.len`** from `list` and `obj`), compilation fails: **`kit.len already exists (list, obj)`**.

## Reserved alias names

The token after **`with`** must **not** equal any **module id**: `io`, `str`, `json`, `date`, `clock`, `http`, `mem`, `file`, `math`, `math.trig`, `math.stats`, `math.mdim`, `list`, `obj`, `vcx`, `executor`, `csv`. Example: `use native list with math` → error.

## Core vs optional

- **`use native io`** (or `json` / `str` / `date` / `clock` / `mem` / `file` without `with`) is an error — use **`use native mem with Alias`** only when you want a short prefix; canonical names always work.
- Duplicate **`use native`** for the **same module** in one file is an error.

## `io.docs`

Uses the **`use native` table of the file where the call appears** (not program-wide). **`io.docs('mem')`**, **`io.docs('list')`**, or a **local alias** (`io.docs('trig')`) when declared in that file. Same-file union for shared **`kit`**. Alias unknown in that file → **`unknown module`**. See [IO.md §2.1](../IO.md#21-introspection-iodocs).

## Core modules (documented)

| Module | Doc |
|--------|-----|
| `io` | [io.md](./io.md) — behavior [IO.md](../IO.md) |
| `io.ctx*` | [IO_CONTEXTS.md](../IO_CONTEXTS.md) |
| `json` | [json.md](./json.md) — [IO_COLLECTIONS.md](../IO_COLLECTIONS.md) |
| `file` | [file.md](./file.md) |
| `str` | [str.md](./str.md) |
| `date` | [date.md](./date.md) |
| `clock` | [clock.md](./clock.md) — monotonic elapsed-time measurements |
| `mem` | [mem.md](./mem.md) |

## Optional modules (documented)

| Module | Doc |
|--------|-----|
| `http` | [http.md](./http.md) — synchronous HTTP/HTTPS requests |
| `env` | [env.md](./env.md) — process / override env + program argv |
| `math` | [math.md](./math.md) |
| `math.trig` | [math.trig.md](./math.trig.md) |
| `math.stats` | [math.stats.md](./math.stats.md) |
| `math.mdim` | [math_mdim.md](./math_mdim.md) |
| `list` | [list.md](./list.md) |
| `obj` | [obj.md](./obj.md) |
| `vcx` | [vcx.md](./vcx.md) |
| `executor` | [executor.md](./executor.md) |
| `csv` | [csv.md](./csv.md) |

## Native handlers: parsing call arguments

When a native splits `inner` into several RHS expressions and parses them **one after another**, ephemeral **LIST** / **OBJECT** literals from the first parse can lose their slot before the next parse runs. Handlers live in `src/lib/mod_*.c`; shared helpers are in [`flat_native_util.h`](../../include/flat_native_util.h) and [`native_parse_rhs.c`](../../src/lib/native_parse_rhs.c).

| Helper | Use when |
|--------|----------|
| `flat_native_parse_expr` | Single operand, last operand, or only scalars (STRING/NUMBER/BOOL). |
| `flat_native_parse_rhs` | Operand parsed **before** another `parse_*` in the same call. |
| `flat_native_parse_rhs_release` | On every exit path after a pinned operand (including errors). |

Rule of thumb: if a later argument can be a literal `[…]` or `{…}` and you still need the earlier collection id, **pin** the earlier operands with `parse_rhs` until the handler finishes. Variadic builders (e.g. `list.pack`) pin each element in the loop. Tests: `test/cases/vm_fixes/006_*`, `007_*`, `math_mdim/007_*`.

User **`def` calls** with multiple inline literals are lowered to sequential `__fl_tmp_i = …` assignments (retain per slot). **`flc`** must split call arguments with bracket-aware commas (`()`, `[]`, `{}`) in `flat_func_split_param_list` / `split_call_param_list` (`flat_func.c`, `flat_func_tag.c`) — otherwise `[1, 2], [3, 4]` is parsed as too many args. Regression: `test/cases/func/032_*` … `035_*`.

