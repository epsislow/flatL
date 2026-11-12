# Native module `mem`

Runtime memory introspection for tests and debugging. **Always available** in `flvm` / `flatl` — no `use native mem` required (optional alias `use native mem with M` → `M.vars_used()`, etc.).

## STRING vs STRING REF

| Kind | In source | Pool slot? | `mem` counters |
|------|-----------|------------|----------------|
| **STRING** (inline) | Short literals (≤127 bytes), many `io` results | No | `str_used` only |
| **STRING REF** | Long literals, slices, some builtins | Yes (≤1024 live slots) | `strref_used`, `strref_max`, `strref_avail` |

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
| `file` | open file handle |
| `empty` | unassigned slot (rare in normal programs) |

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

Full ceilings: [LANGUAGE_TYPES.md](../LANGUAGE_TYPES.md) §5 (`FLAT_POOL_MAX_TOTAL` **64 MiB** owned string data, **1024** REF slots, **256** live lists/objects, **1024** list elements each, …). See also [`vcx.md`](./vcx.md) § *VCX capacity vs value payload*.

### LIST / OBJECT pools

| Call | Max constant |
|------|----------------|
| `list_max()`, `list_used()`, `list_avail()` | 256 live lists |
| `object_max()`, `object_used()`, `object_avail()` | 256 live objects |

### STRING REF pool

| Call | Max constant |
|------|----------------|
| `strref_max()`, `strref_used()`, `strref_avail()` | 1024 live REF slots |

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
- When the STRING REF pool is full, assignment of a new REF may **fail silently** (same as pre-`mem` behavior); short literals still use inline STRING.
- See [LANGUAGE_TYPES.md](../LANGUAGE_TYPES.md) for retain/release and `free(var)`.
