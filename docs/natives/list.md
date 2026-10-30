# Native module `list`

**LIST** construction and access. **Optional** — declare before use:

```text
use native list with L
```

List literals **`[]`** and **`[1, 2, 3]`** in source remain the primary way to write lists; this module adds runtime helpers (replacing bootstrap **`__list_*__`** over time).

## Syntax

| Form | Example |
|------|---------|
| Arrow binding | `L.pack(1, 2) -> xs` (required for calls that run native code) |
| List literal | `xs = [1, 2]` or `xs = []` |
| Canonical name | `list.len(xs) -> n` |

## Functions

| Call | Returns | Description |
|------|---------|-------------|
| `empty()` | LIST | New empty list (same as `[]`) |
| `pack(v1, v2, …)` | LIST | New list from values; **`pack()`** → `[]`; max **1024** elements |
| `push(list, val)` | LIST | Copy-on-write append (new list) |
| `get(list, index)` | any | Element at **index** (0-based); out of range → I/O error |
| `len(list)` | NUMBER | Element count |

Elements may be any value type (not only numbers).

### Example

```text
use native list with L

#main
  L.pack(10, 20) -> xs
  L.push(xs, 30) -> ys
  L.get(ys, 1) -> mid
  L.len(ys) -> n
  io.outln(mid)
  io.outln(n)
  OK
```

Output:

```text
20
3
```

Use with **`math`**: `m.sum(L.pack(a, b, c))` when you do not use a literal `[a, b, c]`.

There is **no** `unpack` in v2; spread at call sites is a future language feature.

## Source layout

| File | Role |
|------|------|
| `src/lib/mod_list.c` | Handlers |
| `src/lib/plugin_list_entry.c` | Plugin entry |
| `include/flat_native.h` | IDs `FLAT_NATIVE_LIST_*` |

Build: **`bin/libflat_list.so`** on Unix (`make all`).
