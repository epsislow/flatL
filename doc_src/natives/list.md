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

Mutators (**`push`**, **`prepend`**, **`insert`**, **`set`**, **`remove_at`**, **`slice`**, **`splice`**) return a **new** LIST. Older bindings keep the previous list until you reassign them or use the **`free(var)`** keyword.

**Memory:** LIST slots use **`bindCount`** and slot reuse (see [LANGUAGE_TYPES.md](../LANGUAGE_TYPES.md) §5). Reassigning **`xs = …`** releases the old list handle implicitly.

## Line modifiers

Recoverable errors → **PANIC** **`list.<fn>`** by default; **`?`** / **`~`** per [SPECIFICATION.md](../SPECIFICATION.md) §1.2.2.

## Functions

| Call | Returns | Panic (no modifier) |
|------|---------|---------------------|
| `empty()` | LIST | **`list.empty`** — allocation failure |
| `pack(v1, v2, …)` | LIST | **`list.pack`** — bad args or &gt;1024 elements |
| `push(list, val)` | LIST | **`list.push`** — bad types / unknown list |
| `prepend(list, val)` | LIST | **`list.prepend`** — bad types / unknown list |
| `insert(list, index, val)` | LIST | **`list.insert`** — bad index or types |
| `set(list, index, val)` | LIST | **`list.set`** — bad index or types |
| `get(list, index)` | any | **`list.get`** — out-of-range or bad types |
| `len(list)` | NUMBER | **`list.len`** — bad types / unknown list |
| `index_of(list, val)` | NUMBER | **`list.index_of`** — bad types (success may return **−1**) |
| `contains(list, val)` | BOOL | **`list.contains`** — bad types |
| `remove_at(list, index)` | LIST | **`list.remove_at`** — bad index or types |
| `slice(list, start, end)` | LIST | **`list.slice`** — bad range or types |
| `splice(list, start, count)` | LIST | **`list.splice`** — bad range or types |
| `range(stop)` | LIST | **`list.range`** — bad numeric args or overflow |
| `range(start, stop)` | LIST | **`list.range`** — same |
| `range(start, stop, step)` | LIST | **`list.range`** — same (**`step == 0`** fails) |
| `repeat(n, value)` | LIST | **`list.repeat`** — bad **`n`** or types |

**`range`** follows Python-style half-open bounds (same spirit as **`list.slice`**). Element count is capped at **`FLAT_LIST_MAX_ELEMS` (1024)**. Floating-point NUMBER elements are stored as NUMBER (whole-number arguments only at the API).

**`repeat`** does not deep-clone LIST/OBJECT internals beyond normal LIST construction (same element **`FlatValue`** repeated **`n`** times).

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

**`range`** / **`repeat`**:

```text
use native list with L
use native math with m

#main
  L.range(5) -> a
  L.range(2, 5) -> b
  L.range(0, 10, 2) -> c
  m.sum(a) -> sa
  L.len(b) -> lb
  L.repeat(3, 0) -> zeros
  io.outln(sa)
  io.outln(lb)
  io.outln(c)
  io.outln(zeros)
  OK
```

Use with **`math`**: `m.sum(L.pack(a, b, c))` or `m.sum(L.range(1, 11))` when you do not use a literal `[a, b, c]`.

There is **no** `unpack` in v2.5; spread at call sites is a future language feature.

## Source layout

| File | Role |
|------|------|
| `src/lib/mod_list.c` | Handlers |
| `src/lib/plugin_list_entry.c` | Plugin entry |
| `src/common/value_collection.c` | COW list helpers |
| `include/flat_native.h` | IDs `FLAT_NATIVE_LIST_*` |

Build: **`bin/libflat_list.so`** on Unix (`make all`).
