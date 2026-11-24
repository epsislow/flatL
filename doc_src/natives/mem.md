# Native module `mem`

Runtime memory introspection for tests and debugging. **Always available** in `flvm` / `flatl` — no `use native mem` required (optional alias `use native mem with M` → `M.vars_used()`, etc.).

## STRING vs STRING REF

| Kind | In source | Pool slot? | `mem` counters |
|------|-----------|------------|----------------|
| **STRING** (inline) | Short literals (≤127 bytes), many `io` results | No | `str_used` only |
| **STRING REF** | Long literals, slices, some builtins | Yes (≤4096 live slots) | `strref_used`, `strref_max`, `strref_avail` |

Inline tags **BOOL**, **NUMBER**, **NULL** are counted separately (`bool_used`, `num_used`, `null_used`) with **no** `*_max` / `*_avail`.

## Functions

### STRING REF helpers

| Call | Returns | Notes |
|------|---------|--------|
| `ref_empty()` | STRING REF | Zero-length pool slot; use with **`io.ctxout`** / **`io.ctxerr`** |
| `ref_from(s)` | STRING REF | Copy of string content (inline or REF) |
| `type_of(v)` | STRING | Logical type name of value **`v`** (see below) |

Prefer these over test-only **`__ref__('…')`** for capture buffers.

### Value type (`type_of`)

| Call | Args | Returns | Panic (no modifier) |
|------|------|---------|---------------------|
| `type_of(v)` | Any expression / variable | STRING | **`mem.type_of`** — missing/invalid argument expression |

Returns the **logical** type (not VM storage detail). Inline STRING and STRING REF both report **`string`**.

| Result | When |
|--------|------|
| `bool` | `true` / `false` |
| `number` | numeric values |
| `string` | inline or REF text |
| `list` | LIST values |
| `object` | OBJECT values |
| `null` | **`null`** |
| `empty` | unassigned slot (rare in normal programs) |

Open file ids from **`file.readCtx`** and related calls are **`number`** values (`mem.type_of` reports **`number`**).

### Resource id kind (`infoResId`)

| Call | Args | Returns |
|------|------|---------|
| `infoResId(id, kind)` | NUMBER id, STRING kind | STRING |

**`kind`** selects which **resource namespace** to query (`"csv"`, `"file"`, `"vcx"`, or `"clock"`). File, CSV session, VCX context, and clock stopwatch ids are independent counters; the hint is required so the same numeric id is not ambiguous.

| `kind` | Live id → result |
|--------|------------------|
| `"csv"` | `csv.reader` / `csv.writer`, else `unknown` |
| `"file"` | `file` (slot **0 … max−1**), else `unknown` |
| `"vcx"` | `vcx`, else `unknown` |
| `"clock"` | `sw` (stopwatch), else `unknown` |

#### What `unknown` means

`unknown` is a **normal return** (no panic): you passed a valid **`kind`** and a numeric **`id`** in range for that family, but the runtime has **no live resource** there **right now**. It does **not** mean “this number is ambiguous across modules”; you already fixed the family with **`kind`**.

Typical cases:

| Situation | Example | Result |
|-----------|---------|--------|
| Id never allocated in that family | `mem.infoResId(99999, 'vcx')` | `unknown` |
| Handle / session already closed | after `file.close(fp)`, `mem.infoResId(fp, 'file')` | `unknown` |
| Number from another API | `mem.infoResId(fp, 'csv')` when `fp` is a **file** slot, not a CSV session id | `unknown` (no CSV session with that session id) |

The **same integer** can be live in one family and `unknown` in another (e.g. file slot `2` vs VCX context `2`). That is why **`kind`** is required.

#### Panic vs `unknown`

| Call problem | Outcome |
|--------------|---------|
| One argument only, e.g. `mem.infoResId(1)` | **PANIC** `mem.infoResId.kind` |
| Invalid **`kind`** (not `csv` / `file` / `vcx` / `clock`) | **PANIC** `mem.infoResId.kind` |
| **`id`** not a whole number, or out of range for **`kind`** | **PANIC** `mem.infoResId.id` |
| Valid **`kind`** + **`id`**, but nothing live in that table | STRING **`unknown`** |

#### Examples

```text
mem.infoResId(fp, 'file') -> kind
kind == 'file'
```

```text
use native vcx
#main
  mem.infoResId(1, 'vcx') -> g
  g == 'vcx'
  fp = file.createCtx('x.dat')
  mem.infoResId(1, 'file') -> f
  ; 'file' only if slot 1 is an open handle; else 'unknown'
  file.close(fp)
  OK
```

```text
#main
  xs = [1, 2]
  mem.type_of(xs) -> kind
  kind == 'list'
  io.outln(kind)
  OK
```

There is **no** separate **`date`** type — datetimes are **`string`** or epoch **`number`**.

### Slot contract (`slot_type`, `slot_frozen`, `slot_const`)

These read **bind metadata** on the **active VCX** (same storage as assign / `O.set2`), not the logical value type. Compare with **`type_of(v)`** above.

| Call | Args | Returns | Panic |
|------|------|---------|-------|
| `slot_type(expr)` | Variable or **lvalue path** (`row`, `row.hold.qty`, `xs[0]`) | STRING | **`mem.slot_type`** — empty arg; **`mem.slot_type.missing`** — unknown root name; **`mem.slot_type.path`** — bad path or unknown schema field |
| `slot_frozen(expr)` | Same lvalue forms | bool | **`mem.slot_frozen.missing`**, **`mem.slot_frozen.path`** |
| `slot_const(expr)` | Same lvalue forms | bool | **`mem.slot_const.missing`**, **`mem.slot_const.path`** |

**`slot_type`** results (contract text, not `VAL_*`):

| Result | Meaning |
|--------|---------|
| `none` | No `:type` on that slot (untyped variable, or nestrict object field without schema) |
| `u8`, `t127`, `t8192`, … | Scalar / string caps (alias **`t`** → **`t127`**) |
| `<stock>`, `<bag>[10]` | Schema bind or schema list slot |
| `/expr` | Dynamic type (`:/expr` in source) when still stored as dynamic |

Path introspection walks **schema** only (read-only): missing nested values do **not** need to exist (e.g. `row.hold.qty` before `hold` is assigned). Unknown field on a typed schema → panic (same as assign).

**`slot_frozen`** / **`slot_const`** reflect flags on the **root** variable of the expression (e.g. `freeze(row)` makes `mem.slot_frozen(row.hold.qty)` true). Nested schema fields have no separate freeze/const flags.

```text
<stock>:
  qty: u16
#main
  row:<stock> = {}
  mem.slot_type(row.hold.qty) == 'u16'
  mem.slot_type(row.hold.qty) -> s
  io.outln(s)
  OK
```

### Introspection (NUMBER)

Argument lists must be empty `()` unless noted above.

### Variables (runtime storage)

| Call | Meaning |
|------|---------|
| `vars_max()` | **Capacity** of the active variable storage (max bind slots in that context) |
| `vars_used()` | Names currently bound there (`var_count`) |
| `vars_avail()` | `vars_max - vars_used` (≥ 0) |

**Without VCX:** one storage; **`vars_max()`** = program **`var_slots`** in bytecode (distinct bind names counted at compile — see [`vcx.md`](./vcx.md) § `var_slots`).

**With [`vcx`](./vcx.md):** same three calls apply to the **active** context — global **`vars_max()`** = program **`var_slots`** (fixed); **`create(N)`** = **`N`** (fixed); **`createAuto`** can **grow** `vars_max` on that context up to **`FLAT_MAX_VARS`**. Inspect other contexts with **`vcx.varsUsed(ctx)`**, **`vcx.varsMax(ctx)`**, etc.

#### Names vs payload

**`vars_*`** counts **bindings** (how many names exist in the active VCX, and how many slots the table can hold). It does **not** measure string bytes or list element count.

| You care about | Use |
|----------------|-----|
| Variable **name** slots in the active context | `mem.vars_used()`, `mem.vars_max()`, `mem.vars_avail()` |
| STRING REF **pool** (shared, all contexts) | `mem.strref_used()`, `mem.strref_max()`, `mem.strref_avail()` |
| LIST / OBJECT **live** values (shared) | `mem.list_*`, `mem.object_*` |

Full ceilings: [LANGUAGE_TYPES.md](../LANGUAGE_TYPES.md) §4–5 (`FLAT_POOL_MAX_TOTAL` **64 MiB** owned string data, **4096** REF slots, **2048** live lists / **256** objects, **1024** list elements each, …). See also [`vcx.md`](./vcx.md) § *VCX capacity vs value payload*.

### LIST / OBJECT pools

| Call | Max constant |
|------|----------------|
| `list_max()`, `list_used()`, `list_avail()` | 2048 live lists |
| `object_max()`, `object_used()`, `object_avail()` | 256 live objects |

### STRING REF pool

| Call | Max constant |
|------|----------------|
| `strref_max()`, `strref_used()`, `strref_avail()` | 4096 live REF slots |

### Inline live values (variables + nested list/object elements)

| Call | Tag |
|------|-----|
| `bool_used()` | BOOL |
| `num_used()` | NUMBER |
| `str_used()` | STRING inline |
| `null_used()` | NULL |

Counters update incrementally on assign, `free`, and collection mutations — there is no full heap scan.

## Notes

- **`list_used`** counts live LIST **slots**, not element count. Copy-on-write: `L.push(xs, v) -> ys` typically bumps `list_used` until `free(ys)`.
- When the STRING REF pool is full, `mem.ref_empty` / `mem.ref_from` / long REF assign → **PANIC** **`pool.full`** (use **`?`** / **`~`** to soft-fail). Short literals still use inline STRING.
- LIST element cap → **`list.elems`**; list/object slot caps → **`list.full`** / **`object.full`**.
- See [LANGUAGE_TYPES.md](../LANGUAGE_TYPES.md) for retain/release and `free(var)`.
