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

### Introspection (NUMBER)

Argument lists must be empty `()` unless noted above.

### Variables (runtime storage)

| Call | Meaning |
|------|---------|
| `vars_max()` | **Capacity** of the active variable storage (max bind slots in that context) |
| `vars_used()` | Names currently bound there (`var_count`) |
| `vars_avail()` | `vars_max - vars_used` (≥ 0) |

**Without VCX:** one storage; **`vars_max()`** = program **`var_slots`** in bytecode (distinct bind names counted at compile — see [`vcx.md`](./vcx.md) § `var_slots`).

**With [`vcx`](./vcx.md):** same three calls apply to the **active** context — global capacity = **`var_slots`**, **`create(N)`** contexts = **`N`**. Other contexts: **`vcx.varsUsed(ctx)`**, **`vcx.varsMax(ctx)`**, etc. Capacity does **not** increase at runtime when you add bindings; recompile changes **`var_slots`** (global on next run), not existing **`create`** capacities.

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
