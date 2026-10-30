# Native module `mem`

Runtime memory introspection for tests and debugging. **Always available** in `flvm` / `flatl` — no `use native mem` required (optional alias `use native mem with M` → `M.vars_used()`, etc.).

## STRING vs STRING REF

| Kind | In source | Pool slot? | `mem` counters |
|------|-----------|------------|----------------|
| **STRING** (inline) | Short literals (≤127 bytes), many `io` results | No | `str_used` only |
| **STRING REF** | Long literals, slices, some builtins | Yes (≤1024 live slots) | `strref_used`, `strref_max`, `strref_avail` |

Inline tags **BOOL**, **NUMBER**, **NULL** are counted separately (`bool_used`, `num_used`, `null_used`) with **no** `*_max` / `*_avail`.

## Functions

All calls return **NUMBER**. Argument lists must be empty `()`.

### Variables (from `.fb` `var_slots`)

| Call | Meaning |
|------|---------|
| `vars_max()` | Upper bound on distinct variable names for this program (`var_slots` in bytecode) |
| `vars_used()` | Current `var_count` |
| `vars_avail()` | `vars_max - vars_used` (≥ 0) |

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
