# Native module `list`

**LIST** construction and access. **Optional** — declare before use:

```text
use native list with L
```

List literals **`[]`** and **`[1, 2, 3]`** in source remain the primary way to write lists; this module adds runtime helpers (replacing bootstrap **`__list_*__`** over time).

## Syntax

| Form | Example |
|------|---------|
| Arrow binding | `L.push(xs, v) -> ys` |
| Assign (same runtime) | `ys = L.push(xs, v)` |
| List literal | `xs = [1, 2]` or `xs = []` |
| Canonical name | `list.len(xs) -> n` |

Native calls on the **right-hand side** of **`=`** use the same handlers as **`->`** (including **`with`** aliases such as **`L.*`**).

## Copy-on-write

Mutators (**`push`**, **`prepend`**, **`insert`**, **`set`**, **`remove_at`**, **`slice`**, **`splice`**) return a **new** LIST. Older bindings keep the previous list until you reassign them. The VM list pool is append-only for the run (see [LANGUAGE_TYPES.md](../LANGUAGE_TYPES.md)).

## Functions

| Call | Returns | Description |
|------|---------|-------------|
| `empty()` | LIST | New empty list (same as `[]`) |
| `pack(v1, v2, …)` | LIST | New list from values; **`pack()`** → `[]`; max **1024** elements |
| `push(list, val)` | LIST | Append at end (COW) |
| `prepend(list, val)` | LIST | Insert at index **0** (COW) |
| `insert(list, index, val)` | LIST | Insert at **index**; **index** ∈ [0, len]; **index == len** appends |
| `set(list, index, val)` | LIST | Replace element at **index** (no length change) |
| `get(list, index)` | any | Element at **index** (0-based); out of range → I/O error |
| `len(list)` | NUMBER | Element count |
| `index_of(list, val)` | NUMBER | First index where value equals **`val`** (`flat_value_equals`); **−1** if missing |
| `contains(list, val)` | BOOL | **`index_of >= 0`** |
| `remove_at(list, index)` | LIST | LIST without element at **index** |
| `slice(list, start, end)` | LIST | Sublist **[start, end)** (half-open) |
| `splice(list, start, count)` | LIST | Remove **`count`** elements starting at **`start`** (delete-only; no variadic insert yet) |

Elements may be any value type (not only numbers).

### Example

```text
use native list with L

#main
  L.pack(2, 3) -> xs
  L.prepend(xs, 1) -> ys
  ys = L.push(ys, 4)
  L.set(ys, 1, 99) -> zs
  L.contains(zs, 99) -> ok
  io.outln(ok)
  OK
```

Use with **`math`**: `m.sum(L.pack(a, b, c))` when you do not use a literal `[a, b, c]`.

There is **no** `unpack` in v2.5; spread at call sites is a future language feature.

## Source layout

| File | Role |
|------|------|
| `src/lib/mod_list.c` | Handlers |
| `src/lib/plugin_list_entry.c` | Plugin entry |
| `src/common/value_collection.c` | COW list helpers |
| `include/flat_native.h` | IDs `FLAT_NATIVE_LIST_*` |

Build: **`bin/libflat_list.so`** on Unix (`make all`).
