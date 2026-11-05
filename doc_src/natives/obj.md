# Native module `obj`

**OBJECT** construction and field access. **Optional** — declare before use:

```text
use native obj with O
```

Object literals **`{ qty: 3, name: 'x' }`** work **without** this line; only **`obj.*`** calls require the declaration (same pattern as **`list`**).

## Syntax

| Form | Example |
|------|---------|
| Arrow binding | `obj.set(o, 'k', v) -> o2` |
| Assign | `v = obj.get(o, 'k')` |
| Literal | `o = { id: 1, label: 'x' }` |
| Alias | `O.get(o, 'id')` when using `use native obj with O` |

Native calls on the **right-hand side** of **`=`** use the same handlers as **`->`**.

Commas inside quoted strings in argument lists are **not** argument separators (same rules as **`str.*`** / **`list.*`**).

## Copy-on-write

**`set`** and **`remove`** return a **new** OBJECT. Older bindings keep the previous object until reassigned or **`free(var)`**.

**`get`**, **`has`**, **`len`**, **`keys`**, **`values`** do not mutate the input object.

## Line modifiers and errors

See [SPECIFICATION.md](../SPECIFICATION.md) §1.2.2. Recoverable failures → **PANIC** with reason **`obj.<fn>`** unless the line has **`?`** (silent, no bind) or **`~`** (**`#or.*`**).

Use **`obj.has(o, 'key')`** before **`get`** when you want a guard instead of panic/`#or`.

### Example — missing key with **`?`**

```text
use native obj with O

#main
  o = { ok: true }
  sentinel = 42
  ? O.get(o, 'missing') -> sentinel
  io.outln(sentinel)
  OK
```

Prints **`42`** — **`sentinel`** was not updated.

## Functions (v1)

| Call | Returns | Panic (no modifier) |
|------|---------|---------------------|
| `empty()` | OBJECT | **`obj.empty`** — pool / allocation failure |
| `get(obj, pathString)` | any | **`obj.get.missing`**, **`obj.path.syntax`**, **`obj.get.arg`** — see sub-reasons below |
| `set(obj, pathString, val)` | OBJECT | **`obj.set.path`**, **`obj.set.arg`**, pool errors |
| `has(obj, pathString)` | BOOL | **`obj.has.arg`** — missing path → **`false`** |
| `len(obj)` | NUMBER | **`obj.len`** — bad types or unknown object |
| `remove(obj, pathString)` | OBJECT | **`obj.remove.path`** — missing leaf → copy, no panic |
| `keys(obj)` | LIST | **`obj.keys`** — root object only (no path) |
| `values(obj)` | LIST | **`obj.values`** — root object only |
| `set2(pathExpr, val)` | OBJECT | **`obj.set2.root`**, **`obj.set2.path`** — COW on OBJECT root |

### Path argument (STRING) — `get` / `set` / `has` / `remove`

Argument 2 is always a **STRING value** (literal or variable), then parsed as a **path DSL**:

- Segments separated by **`.`** outside quotes: `'user.contact'`
- One segment: **`ident`** or **quoted** key name: `'stocks'`, `'user."contact.name"'`
- **No trim** — spaces inside the path string → **`obj.path.syntax`**
- **No `[index]`** in path strings — use **`set2`** or expression paths for lists

```text
O.get(o, 'qty')
path = 'user."contact.name"'
O.set(o, path, 'Bob') -> o
O.get(o, path) -> x
```

**Bare identifier** as arg 2 (`O.get(o, stocks)`) means the **variable** `stocks` (must hold STRING path), **not** a field named `stocks`. Field → **`O.get(o, 'stocks')`**.

### Expression paths (read / `set2`)

In assign RHS and native arguments:

```text
o.qty
o.stocks[2].qty
myList[idx]
accounts.user."contact.name"
```

- **OBJECT** → **`.segment`** (ident or quoted key)
- **LIST** → **`[index]`** only (0-based; index **NUMBER**, fractional → **floor**)
- **`O.set2(o.stocks[2].qty, 9) -> o`** — root must be **OBJECT** (`obj.set2.root` if root is LIST)

### Example — nested path STRING

```text
use native obj with O

#main
  o = {}
  O.set(o, 'user."contact.name"', 'Bob') -> o
  O.get(o, 'user."contact.name"') -> name
  io.outln(name)
  OK
```

### Example — COW and fields

```text
use native obj with O

#main
  o = { qty: 3, name: 'box' }
  O.get(o, 'qty') -> q
  O.set(o, 'qty', 5) -> o2
  O.get(o2, 'qty') -> q2
  io.outln(q)
  io.outln(q2)
  OK
```

### Example — keys / values

```text
use native obj with O
use native list with L

#main
  o = { first: 10, second: 20 }
  O.keys(o) -> ks
  O.values(o) -> vs
  L.len(ks) -> n
  io.outln(n)
  io.outln(ks)
  io.outln(vs)
  OK
```

## Limits

| Limit | Value |
|-------|-------|
| Keys per object | 128 (`FLAT_OBJ_MAX_KEYS`) |
| Key name bytes | 127 (`FLAT_OBJ_KEY_MAX`) |
| Live OBJECT slots | 256 (`FLAT_COLL_MAX_OBJECTS`) |

See [LANGUAGE_TYPES.md](../LANGUAGE_TYPES.md) §5 for **`bindCount`** and **`free(var)`**.

## Related APIs

| API | Role |
|-----|------|
| **`json.parse_object` / `json.stringify`** | JSON text ↔ values ([IO_COLLECTIONS.md](../IO_COLLECTIONS.md)) |
| **`io.in_object`** | Read object from stdin |
| **`mem.object_*`** | Pool introspection |

VM helpers **`__obj__`**, **`__obj_get__`**, **`__obj_set__`** are **removed** — use **`obj.*`**.

## Runtime

Plugin: **`bin/libflat_obj.so`** (module id **`obj`**). Search path: [natives/README.md](./README.md) ( **`FLAT_NATIVE_PATH`**, VM directory, **`bin/`**).

| File | Role |
|------|------|
| `src/lib/mod_obj.c` | Handlers |
| `src/lib/plugin_obj_entry.c` | Plugin entry |
| `src/common/value_collection.c` | `flat_obj_*`, COW |
| `src/common/flat_obj_path.c` | Path STRING parse, suffix read, **`set2`** |

## PANIC sub-reasons (path)

| Sub-reason | Typical cause |
|------------|----------------|
| `obj.path.syntax` | Path STRING gol / invalid |
| `obj.get.arg` / `obj.set.arg` / `obj.has.arg` | Arg path not STRING |
| `obj.get.missing` | Missing segment on **`get`** (without `?`/`~`) |
| `obj.path.type` | `.` on LIST, `[` on OBJECT, etc. |
| `obj.set.path` | Wrong type on **`set`** / **`set2`** write |
| `obj.set2.root` | **`set2`** primary not OBJECT |
| `obj.remove.path` | Wrong type on **`remove`** path |

With **`?`** / **`~`**, missing **`get`** behaves like before (silent / `#or`).

## Tests

**`test/cases/obj/`** — paths, **`set2`**, expression suffix, **`foreach(o.field, …)`**, literal list **`foreach`**.  
**`test/cases/math/030_calc_obj_path`** — **`math.calc`** with object paths.
