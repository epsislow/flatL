# flatL — language types & runtime limits

Documentation index: [README.md](./README.md) (English).

Normative reference for **value types**, **STRING ref pool**, and **limits** in the VM (`flvm` v0.2+).  
Architecture overview: [SPECIFICATION.md](./SPECIFICATION.md). Tooling: [TOOLCHAIN.md](./TOOLCHAIN.md).

---

## 1. Value types (logical)

The language exposes three logical types. There is **no separate BLOB type** — large byte content uses **STRING / REF**.

| Type | Examples in source | VM tag |
|------|-------------------|--------|
| **BOOL** | `ok = true`, `ok = false` | `VAL_BOOL` |
| **NUMBER** | `n = 42`, `n = -3.5` | `VAL_NUMBER` (IEEE-754 `double`) |
| **STRING** | `'hello'`, `"path"`, long data via ref | `VAL_STR_INLINE` or `VAL_STR_REF` |

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

### Slice views

Assign:

```text
head = __slice__(body, 0, 4)
```

- `body` must be **STRING / REF**.
- No copy; guard compares bytes at offset.
- Production name will be `str.slice(...)` via native library; `__slice__` is the v0.2 VM helper.

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

Overflow → assign/alloc fails silently in VM today; future: compile-time or panic.

---

## 6. C API (for native libraries)

Header: `include/flat_value.h`

| Function | Purpose |
|----------|---------|
| `flat_pool_create` / `flat_pool_destroy` | Pool lifetime (VM owns one per run) |
| `flat_pool_alloc_copy` | Insert bytes → ref id |
| `flat_pool_slice` | View into existing ref |
| `flat_value_set_*` | Build `FlatValue` |
| `flat_value_equals` | Guard equality (STRING cross INLINE/REF) |

Future: VM handle passed into natives for binding outputs.

---

## 7. Related keywords

`OK`, `FAIL`, `call`, `foreach`, etc. are **language keywords**, not types.  
Dotted calls (`kv.get`) will return STRING REF via natives — see SPECIFICATION §1.1.1.
