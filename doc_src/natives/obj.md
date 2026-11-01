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

## Error behavior

On **`… -> var`** or **`var = …`**, invalid types, unknown object id, pool full, or **`get`** on a **missing key** → assign **fails silently** (target variable unchanged). No PANIC.

Use **`obj.has(o, 'key')`** before **`get`** when you need to branch.

### Example — missing key (silent fail)

```text
use native obj with O

#main
  o = { ok: true }
  sentinel = 42
  obj.get(o, 'missing') -> sentinel
  io.outln(sentinel)
  OK
```

Prints **`42`** — **`sentinel`** was not updated.

## Functions (v1)

| Call | Returns | Description |
|------|---------|-------------|
| `empty()` | OBJECT | New `{}` |
| `get(obj, key)` | any | Field value; **`key`**: quoted string or identifier token (field name, not variable lookup for bare ids) |
| `set(obj, key, val)` | OBJECT | COW set or add field |
| `has(obj, key)` | BOOL | Whether **`key`** exists |
| `len(obj)` | NUMBER | Key count (0…128) |
| `remove(obj, key)` | OBJECT | COW without **`key`**; missing key → **identical copy** |
| `keys(obj)` | LIST | LIST of STRING keys, insertion order |
| `values(obj)` | LIST | LIST of values in the same order as **`keys`** |

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
