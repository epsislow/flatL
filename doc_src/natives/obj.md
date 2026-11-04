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
| `get(obj, key)` | any | **`obj.get`** — bad types, bad object id, or **missing key** |
| `set(obj, key, val)` | OBJECT | **`obj.set`** — bad types, unknown object, pool full |
| `has(obj, key)` | BOOL | **`obj.has`** — bad types or unknown object |
| `len(obj)` | NUMBER | **`obj.len`** — bad types or unknown object |
| `remove(obj, key)` | OBJECT | **`obj.remove`** — bad types or unknown object (missing key → copy, no panic) |
| `keys(obj)` | LIST | **`obj.keys`** — bad types or unknown object |
| `values(obj)` | LIST | **`obj.values`** — bad types or unknown object |

### Key argument

These calls accept the same key forms as object literals:

```text
obj.get(o, 'qty')
obj.get(o, qty)
```

Both refer to the field named **`qty`**, not a variable **`qty`**, when the second form is a bare identifier.

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
| `src/common/value_collection.c` | `flat_obj_*`, **`flat_obj_remove_copy`** |

## Tests

**`test/cases/obj/`** — empty, set/get, has/remove, keys/values, silent **`get`**.  
**`test/cases/values/005_object_literal`** — literal + **`obj`** COW.
