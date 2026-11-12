# flatL — language types & runtime limits

Documentation index: [README.md](./README.md) (English).

Normative reference for **value types**, **STRING ref pool**, and **limits** in the VM (`flvm` v0.2+).  
Architecture overview: [SPECIFICATION.md](./SPECIFICATION.md). Tooling: [TOOLCHAIN.md](./TOOLCHAIN.md).

**Typed bind (T1–T4):** optional **`name:type`** / **`name:<schema>`** / **`name:/expr`** on **ASSIGN**, plus **path assign** — [TYPED_BIND.md](./TYPED_BIND.md). Untyped binds behave as before (logical `VAL_*` only).

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
| **OBJECT** | `{ qty: 3, name: 'x' }`, `obj.empty()` (optional native) | `VAL_OBJECT` (pool id) |

**LIST** and **OBJECT** are **distinct** types (JSON array vs object). Either may nest the other as elements or field values.

**Construction (v0.3):**

- Literals: `items = [1, 2]`, `doc = { id: 'a', n: 2 }` (keys: identifier or quoted string).
- Bootstrap helpers (legacy; prefer natives): `__list__()`, `__list_push__(list, val)`, … — **OBJECT** uses optional **`obj.*`** ([natives/obj.md](./natives/obj.md)): `empty`, `get`, `set`, `has`, `len`, `remove`, `keys`, `values`.
- **`obj.set`** / **`obj.remove`** return a **new** object (copy-on-write); the old binding is unchanged.

**`foreach`:** on a **LIST**, binds each element (any type). On a **STRING**, legacy comma-separated CSV iteration still works.

**Mixed-type comparison** (`==`, `>=`, …): if types differ → result is **false** (no implicit coercion).

**Datetimes (no `VAL_DATE`):** calendar values are **STRING**; absolute instants use **NUMBER** = **Unix seconds UTC** (`date.now()`, `date.parse_auto`, …). See [natives/date.md](./natives/date.md).

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
| `FLAT_MAX_VARS` | 32768 | Max **distinct bind names** per one VCX context (`var_slots` / `create` / `createAuto` ceiling) |
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

**VCX vs pools:** each context has its own name table (capacity `vars_max`); STRING REF, LIST, and OBJECT limits above are **per VM run** (shared by all contexts). See [natives/vcx.md](./natives/vcx.md) § *VCX capacity vs value payload* and [natives/mem.md](./natives/mem.md) § *Names vs payload*.

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

## 6. Static slot contracts (strict types)

**Normative:** [STRICT_TYPES.md](./STRICT_TYPES.md) · [TYPED_BIND.md](./TYPED_BIND.md).

### T1 (implemented)

| Form | Meaning |
|------|---------|
| `a:u8 = 3` | Contract on **`(VCX, slot)`**; value checked on every assign |
| `a = 5` | Keeps existing contract; value must still fit |
| `free(a)` | Clears value **and** contract (extends §5) |
| `a:t8 = "hi"` | Max **8** UTF-8 bytes on bind |
| `items:l = [1]` / `doc:o = {}` | Open LIST / OBJECT container |

**Supported type tokens (T1):** see table below and [TYPED_BIND.md §3](./TYPED_BIND.md#3-type-catalog-v1).

#### Strict type tokens (T1) — limits

| Token | Meaning | Limit / range at assign |
|-------|---------|-------------------------|
| `u8` | unsigned 8-bit | `0` … `255` |
| `u16` | unsigned 16-bit | `0` … `65535` |
| `u32` | unsigned 32-bit | `0` … `4294967295` |
| `u64` | unsigned 64-bit (interim) | exact integers `0` … `2^53−1` in `double` |
| `s8` | signed 8-bit | `−128` … `127` |
| `s16` | signed 16-bit | `−32768` … `32767` |
| `s32` | signed 32-bit | `−2^31` … `2^31−1` |
| `s64` | signed 64-bit (interim) | exact integers `±(2^53−1)` in `double` |
| `f32`, `f64` | float | any `VAL_NUMBER` at assign (see below) |
| `b` | bool | `true` / `false` |
| `t` | short string | alias **`t127`** — max **127** UTF-8 bytes |
| `tX` | bounded string | **`X`** = max UTF-8 bytes, **`1` ≤ X ≤ `127`** (`t8`, `t255` invalid) |
| `l` | open list | any `VAL_LIST` (no element check) |
| `o` | open object | any `VAL_OBJECT` |
| `<schema>` | closed object | keys ⊆ schema; missing → `null` on `{}` |
| `<schema>[N]` | schema list **field** | max **`N`** elements (`0` = unbounded `[]` in spec) |
| `xs:<schema>[N]` | list **slot** | variable holds `VAL_LIST` of `<schema>`; max length **`N`** |

**`f32` / `f64` (interim):** slots are **logical** float contracts only. Storage is still one IEEE-754 **`double`** per `VAL_NUMBER`; assign checks **`tag == VAL_NUMBER`** only — no range, no `float` rounding, and no distinction between **`f32`** and **`f64`** until a future VM/AOT split (`num_stor` / narrowing rules in the types roadmap). Use integers (`u*`/`s*`) when you need hard bounds today.

**String alias:** `t` without a number is **`t127`** (same cap as `FLAT_INLINE_STR_MAX` for quoted literals).

**List slots vs paths:** in `name:<stock>[4] = …`, the **`[4]`** is part of the **type** (capacity), not `name[4]` path syntax. Real paths use `.` or `[` **after** the type, e.g. `row.stocks[0]`, `xs[0] = s` when `xs` is already `:<stock>[N]`.

**Panics (unhandled → exit 1):** `type.assign.range`, `type.assign.range.i64` (`u64`/`s64` outside exact-integer `double` range), `type.assign.incompatible`, `type.assign.string.len`, `type.assign.list.len`, `type.assign.retype` (change `:type` without `free`), `type.compare.incompatible` (typed scalar compare).

**Compile:** **`:/expr`** on ASSIGN lhs is allowed; runtime reads STRING each run. **`<schema>`** on static lhs requires a **`<schema>:`** declaration; unknown schema → `type.schema.unknown` (compile) or `type.assign.schema.unknown` (dynamic STRING).

Logical kinds in §1 still apply; **NUMBER** remains IEEE-754 **`double`** — integers are range-checked, not narrowed in storage.

**`u64` / `s64` (interim):** only integers in **`±(2^53−1)`** (`9007199254740991`) that are **exact** in `double`; larger nominal `u64`/`s64` literals or values → **`type.assign.range.i64`** until dedicated 64-bit storage (AOT).

**Future (plan AM-NUM, not implemented):** stay on single tag **`VAL_NUMBER`**; add internal **`num_class`** (`whole` / `decimal`) and **`num_stor`** (`uint64_t`, `int64_t`, `float`, `double`, …). No separate `VAL_WHOLE` / `VAL_DECIMAL`. See repository plan `strict-types-schemas-roadmap.plan.md` §13.

### T2 (schema objects) — done

### T3 (schema paths)
| Form | Meaning |
|------|---------|
| `myStock.qty = 9` | Validates field `qty` on slot `<stock>` |
| `myStock.extra = 1` | **`type.schema.field.unknown`** |
| `row:<var>.userId = 3` | Defines typed slot + sets path in one line |
| `O.set2(row.s.location, "A1")` | Same path engine as assign (materializes nested schema) |

### T2 recap (schema objects)

| Form | Meaning |
|------|---------|
| `<stock>:` … | Schema declaration (indented fields) |
| `row:<stock> = {}` | Closed object; missing fields → **`null`** |
| `row:<stock> = { qty: 1 }` | Extra keys → **`type.schema.field.unknown`** |

**GUARD** expressions may use **dotted paths** (e.g. `row.qty == null`).

### Planned (T3+)

| Form | Meaning |
|------|---------|
| `s:/typeStr` | Dynamic type |
| `const n:u16 = 1` | Immutable slot |
| `freeze(cfg)` | Frozen until `free` |

**Comparison (T4):** mixed logical types → `==` is **false** without panic. Two typed **scalars** with different contracts → **`type.compare.incompatible`**. Two **LIST** / **OBJECT** values → structural compare via **`json.stringify`** (no panic for shape mismatch).

**Phases:** T0 docs → **T1** → **T2** (schemas) → … — [TYPED_BIND.md §10](./TYPED_BIND.md#10-implementation-phases).

---

## 7. C API (for native libraries)

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

Future: VM handle passed into natives for binding outputs; remaining `__list_*` helpers → `list.*` (partially done).

---

## 8. Related keywords

`OK`, `FAIL`, `call`, `foreach`, etc. are **language keywords**, not types.  
Dotted calls (`kv.get`) will return STRING REF via natives — see SPECIFICATION §1.1.1.

---

## 9. I/O (output & debug)

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
