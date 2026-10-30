# flatL — language types & runtime limits

Documentation index: [README.md](./README.md) (English).

Normative reference for **value types**, **STRING ref pool**, and **limits** in the VM (`flvm` v0.2+).  
Architecture overview: [SPECIFICATION.md](./SPECIFICATION.md). Tooling: [TOOLCHAIN.md](./TOOLCHAIN.md).

---

## 1. Value types (logical)

The language exposes five logical value kinds. There is **no separate BLOB type** — large byte content uses **STRING / REF**.

| Type | Examples in source | VM tag |
|------|-------------------|--------|
| **BOOL** | `ok = true`, `ok = false` | `VAL_BOOL` |
| **NUMBER** | `n = 42`, `n = -3.5` | `VAL_NUMBER` (IEEE-754 `double`) |
| **STRING** | `'hello'`, `"path"`, long data via ref | `VAL_STR_INLINE` or `VAL_STR_REF` |
| **LIST** | `[1, 'a', { x: 1 }]`, `__list__()` | `VAL_LIST` (pool id) |
| **NULL** | literal **`null`**, `json.parse('null')`, `json.parse('')` | **`VAL_NULL`** (not `VAL_EMPTY`; guards `x == null`) |
| **OBJECT** | `{ qty: 3, name: 'x' }`, `__obj__()` | `VAL_OBJECT` (pool id) |

**LIST** and **OBJECT** are **distinct** types (JSON array vs object). Either may nest the other as elements or field values.

**Construction (v0.3):**

- Literals: `items = [1, 2]`, `doc = { id: 'a', n: 2 }` (keys: identifier or quoted string).
- Bootstrap helpers (replaced later by `list.*` / `obj.*` natives): `__list__()`, `__list_push__(list, val)`, `__list_len__(list)`, `__list_get__(list, index)`, `__obj__()`, `__obj_set__(obj, 'key', val)`, `__obj_get__(obj, 'key')`.
- `__obj_set__` returns a **new** object (copy-on-write); the old binding is unchanged.

**`foreach`:** on a **LIST**, binds each element (any type). On a **STRING**, legacy comma-separated CSV iteration still works.

**Mixed-type comparison** (`==`, `>=`, …): if types differ → result is **false** (no implicit coercion).

---

## 2. STRING — INLINE vs REF

| Storage | When | Limit |
|---------|------|--------|
| **INLINE** | Quoted literal length ≤ **127** UTF-8 bytes | `FLAT_INLINE_STR_MAX` |
| **REF** | Literal >127 bytes (auto-promote), `__ref__(...)`, future `file.read`, natives | Pool limits below |

Program text always uses **string** syntax; INLINE vs REF is internal.

### String pool

- Entries: owned buffer **or** **slice view** (variant A): `{ parent_id, offset, len }`.
- **Refcount** on parent while views exist.
- Freed when the VM run ends (`flat_pool_destroy`).

### Slicing strings

Use the core native **`str.slice(s, start, end)`** — half-open byte range **`[start, end)`**, same convention as **`list.slice`**.

```text
head = str.slice(body, 0, 4)
```

- **`s`** may be INLINE STRING or STRING REF (including **`__ref__(...)`** bodies).
- Result is a new STRING (INLINE or REF); large slices may allocate in the pool.
- Requires **`0 <= start <= end <= len(s)`**; otherwise assign / `->` fails silently.
- The old parser helper **`__slice__(s, start, len)`** is **removed**; use **`str.slice(s, start, start + len)`**.

### Test / bootstrap helper

```text
body = __ref__('...long content...')
```

Allocates pool copy. Replaced by natives (e.g. `file.read`) later — not for production `.fl`.

---

## 3. BOOL

| Rule | Detail |
|------|--------|
| Literals | `true`, `false` (unquoted) |
| Guards | `ok == true`, `ok == false` |
| Storage | 0 / 1 |

---

## 4. NUMBER

| Rule | Detail |
|------|--------|
| Literals | Decimal integer or float (`strtod`) |
| Guards | `==`, `>=`, `<=`, `>`, `<` (both sides must be NUMBER) |
| Invalid | NaN, Inf rejected at parse |
| Internal | `double` |

---

## 5. Limits (v0.2)

| Constant | Value | Meaning |
|----------|-------|---------|
| `FLAT_INLINE_STR_MAX` | 127 | Max INLINE string bytes |
| `FLAT_MAX_VARS` | 128 | Variables per run |
| `FLAT_POOL_MAX_SLOTS` | 1024 | Max pool entries (owned + views) |
| `FLAT_POOL_MAX_CHUNK` | 16 MiB | Max single allocation |
| `FLAT_POOL_MAX_TOTAL` | 64 MiB | Max sum of owned bytes |
| `FLAT_VAR_NAME_MAX` | 63 | Identifier length |
| `FLAT_COLL_MAX_LISTS` | 256 | Max LIST values in pool |
| `FLAT_COLL_MAX_OBJECTS` | 256 | Max OBJECT values in pool |
| `FLAT_LIST_MAX_ELEMS` | 1024 | Max elements per LIST |
| `FLAT_OBJ_MAX_KEYS` | 128 | Max keys per OBJECT |
| `FLAT_OBJ_KEY_MAX` | 127 | Max key string bytes |

Overflow → assign/alloc fails silently in VM today; future: compile-time or panic.

---

## 5. Collection memory (`bindCount`, `free`)

LIST and OBJECT slots in the VM collection pool use **reference counting** (**`bindCount`**) per slot id. The limit **`FLAT_COLL_MAX_LISTS` / `FLAT_COLL_MAX_OBJECTS` (256)** applies to **live** values (**`bindCount > 0`**), not total ids ever allocated — freed slots may be **reused**.

| Mechanism | Behavior |
|-----------|----------|
| **`var = …` / `… -> var`** | **Unbind** previous value of **`var`** (decrement **`bindCount`**); bind new value (**increment**) |
| **`free(var)`** | Keyword — unbind **`var`**; set **`var = null`**. **No-op** if **`var`** is already **`null`** or holds a scalar (NUMBER / BOOL / INLINE STRING). Idempotent. |
| **Nested values** | A LIST/OBJECT that stores another LIST/OBJECT/REF in an element or field increments that slot’s **`bindCount`**. |
| **COW natives** (`list.push`, …) | New slot for the result; old bindings unchanged until reassigned or **`free`**. |

**STRING REF** pool: slice views and parent pins follow §2; full **`bindCount`** alignment for REF is in progress.

**Compile:** **`free(ghost)`** when **`ghost`** was never assigned in that block → **compile error**.

---

## 6. C API (for native libraries)

Header: `include/flat_value.h`

| Function | Purpose |
|----------|---------|
| `flat_pool_create` / `flat_pool_destroy` | Pool lifetime (VM owns one per run) |
| `flat_pool_alloc_copy` | Insert bytes → ref id |
| `flat_pool_slice` | View into existing ref |
| `flat_value_set_*` | Build `FlatValue` |
| `flat_value_equals` | Guard equality (STRING cross INLINE/REF; LIST/OBJECT by pool id) |
| `flat_coll_*`, `flat_list_*`, `flat_obj_*` | Collection pool (LIST / OBJECT) |
| `flat_value_retain` / `flat_value_release` | **`bindCount`** adjust for LIST/OBJECT handles |

Future: VM handle passed into natives for binding outputs; `obj.get` / `list.push` replace `__obj_*` / `__list_*` helpers.

---

## 7. Related keywords

`OK`, `FAIL`, `call`, `foreach`, etc. are **language keywords**, not types.  
Dotted calls (`kv.get`) will return STRING REF via natives — see SPECIFICATION §1.1.1.

---

## 8. I/O (output & debug)

**Full normative spec:** [IO.md](./IO.md) (prompts, `->` binding, cancel/EOF, BOOL tokens, PANIC rules).

Summary:

| API | Effect |
|-----|--------|
| **`io.out` / `io.outln` / `io.outvar`** | Print to **stdout** (STRINGs raw; LIST/OBJECT debug form) |
| **`io.in` / `io.inln` / `io.in_num` / `io.in_bool`** | Read **stdin**; optional prompt STRING |
| **`str.to_num` / `str.to_bool`** | Parse STRING → NUMBER / BOOL (no stdin) |
| **`str.len` / `str.slice` / …** | Full **`str.*`** catalog: [natives/str.md](./natives/str.md) |
| **`… -> var`** or **`var = …`** | Bind result of `io.*` / `str.*` |

Host: `flvm --trace` / `--trace-vars` → **stderr**. Unhandled PANIC → stderr; recovered PANIC → silent on stderr.

Implementation: **`OP_NATIVE`** via **`src/lib/`** module registry (`io`, `str`, `json`).
