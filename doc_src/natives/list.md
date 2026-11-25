# Native module `list`

**LIST** construction and access. **Optional** — declare before use:

```text
use native list with L
```

**Availability:** native ✅ · WASM ✅ (embedded; Linux may use `libflat_list.so`). Matrix: [README.md § WASM](./README.md#wasm-availability).

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

**Memory:** LIST slots use **`bindCount`** and slot reuse (see [LANGUAGE_TYPES.md](../LANGUAGE_TYPES.md) §5–6). Reassigning **`xs = …`** releases the old list handle implicitly. Overflow → **PANIC** **`list.elems`** (per-list element cap) or **`list.full`** (live LIST slot cap).

## Line modifiers

Recoverable errors → **PANIC** **`list.<fn>`** by default; **`?`** / **`~`** per [SPECIFICATION.md](../SPECIFICATION.md) §1.2.2.

## Functions

Common suffixes: **`.args`** (arity/type of non-list args), **`.type`** (operand not LIST), **`.range`** (index OOB), plus **`list.elems`** / **`list.full`**.

| Call | Returns | Panic (no modifier) |
|------|---------|---------------------|
| `empty()` | LIST | **`list.empty.args`** / **`list.full`** |
| `pack(v1, v2, …)` | LIST | **`list.pack.args`** / **`list.full`** / **`list.elems`** |
| `push(list, val)` | LIST | **`list.push.type`** / **`.args`** / **`list.elems`** / **`list.full`** |
| `prepend(list, val)` | LIST | **`list.prepend.type`** / **`.args`** / **`list.elems`** / **`list.full`** |
| `insert(list, index, val)` | LIST | **`list.insert.type`** / **`.args`** / **`.range`** / **`list.elems`** / **`list.full`** |
| `set(list, index, val)` | LIST | **`list.set.type`** / **`.args`** / **`.range`** / **`list.full`** |
| `get(list, index)` | any | **`list.get.type`** / **`.args`** / **`.range`** |
| `len(list)` | NUMBER | **`list.len.type`** |
| `index_of(list, val)` | NUMBER | **`list.index_of.type`** / **`.args`** (success may return **−1**) |
| `contains(list, val)` | BOOL | **`list.contains.type`** / **`.args`** |
| `remove_at(list, index)` | LIST | **`list.remove_at.type`** / **`.args`** / **`.range`** |
| `slice(list, start, end)` | LIST | **`list.slice.type`** / **`.args`** / **`list.full`** |
| `splice(list, start, count)` | LIST | **`list.splice.type`** / **`.args`** / **`list.full`** |
| `range(stop)` | LIST | **`list.range.args`** / **`list.elems`** / **`list.full`** |
| `range(start, stop)` | LIST | same |
| `range(start, stop, step)` | LIST | **`list.range.step`** when **`step == 0`**; else same |
| `repeat(n, value)` | LIST | **`list.repeat.args`** / **`list.elems`** / **`list.full`** |
| `is_rect(list)` | BOOL | **`list.shape`** — bad operand; walk/stamp § AM-RECT-MD |
| `dims(list)` | LIST | **`type.list.not_rect`** dacă !rect; else lungimi pe axe |
| `rank(list)` | NUMBER | 0 dacă !rect; altfel 1=vector, 2=matrix, … |
| `is_matrix(list)` | BOOL | rect + rank 2 + numeric omogen |
| `is_vector(list)` | BOOL | rect + rank 1 + numeric omogen |
| `values_to(list ; "type all\\any")` | LIST | Tag obligatoriu: tip (`u8`…`f64`, `b`, `tN`/`t255`) + mod `all` sau `any`. Conversie frunze pe arbore LIST (rank ≤4); structură păstrată (COW). **`all`:** prima frunză neconvertibilă → `type.list.values_to.fail`. **`any`:** parțial; zero conversii → `type.list.values_to.none`. Operand non-LIST → `type.list.values_to.not_list`. |

Shape: la assign tipizat / literal cu sufix se pune **stamp** pe `list_id`; altfel **walk** (memo pe id). După mutatori COW (`push`, …) listă nouă fără stamp — walk la primul `list.*`.

**`range`** follows Python-style half-open bounds (same spirit as **`list.slice`**). Element count is capped at **`FLAT_LIST_MAX_ELEMS` (1024)**. Floating-point NUMBER elements are stored as NUMBER (whole-number arguments only at the API).

**`repeat`** does not deep-clone LIST/OBJECT internals beyond normal LIST construction (same element **`FlatValue`** repeated **`n`** times).

Elements may be any value type (not only numbers).

### `values_to` (tag)

```text
L.values_to(G ; "f64 all") -> M    # STRING→AM-NUM pe toate frunzele
L.values_to(row ; "f64 any") -> row # păstrează neconvertibile (ex. OBJECT)
```

Debug: `FLAT_LIST_DEBUG=1` pe `flvm` → trace walk pe stderr (`[list] …`).

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
