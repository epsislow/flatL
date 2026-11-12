# Strict types & object schemas (overview)

**Status:** **T7** — **`tN` până la `t8192`**, SCHM **`.fb` v8** (aux 16-bit). **T6:** `O.set`/`set2`, typed **`def`** (FFUN v7). **T5** freeze/const; **T4+** typed **`m.calc`** / I/O.

| Doc | Role |
|-----|------|
| **This page** | Product overview, examples, glossary |
| [TYPED_BIND.md](./TYPED_BIND.md) | Normative draft (bind rules, panic codes, phases) |
| Roadmap plan | `.cursor/plans/strict-types-schemas-roadmap.plan.md` in the repository |

**Related today:** [LANGUAGE_TYPES.md](./LANGUAGE_TYPES.md) (logical `VAL_*`), [SPECIFICATION.md](./SPECIFICATION.md) (assign / `free`), [natives/json.md](./natives/json.md), [natives/obj.md](./natives/obj.md).

---

## 1. Opt-in only

Typed contracts apply only where you write **`name:type`**, **`name:/expr`**, **`const name:type =`**, or **`-> name:type`**.

Programs without those forms keep current behaviour (NUMBER = `double`, open OBJECT/LIST).

There is **no** global `use strict types`.

---

## 2. Slot contract (per VCX)

- The **name** maps to a **slot index** at compile time.
- **Type metadata** lives on **`(VCX context, slot)`**, not on the name globally.
- **`free(var)`** clears value **and** contract (type + frozen).
- Changing static type without **`free`** → **`type.assign.retype`** (panic).

---

## 3. Type catalog (v1)

| Kind | Example | Notes |
|------|---------|--------|
| Unsigned / signed int | `u8`, `s16`, `u32`, … | Range checked on bind; explicitly suffixed literals preserve the full 64-bit integer range ([LANGUAGE_TYPES.md](./LANGUAGE_TYPES.md)) |
| Float | `f32`, `f64` | Stored as IEEE-754 `float` / `double`; explicit `f32` literals round to float, while implicit narrowing is loss-checked. `f8`, `q4p4` deferred |
| Bool | `b` | |
| String | `t` (= **`t127`**), `t8`, `t255` | Max length on bind |
| Open list | `l`, `items:l` | Any elements |
| Open object | `o` | Add fields freely on assign |
| List typed | `products:t255[]`, `selected:<stock>[4]` | `[]` = no max count; `[N]` = max **N** (fixed at bind) |
| Schema object | `row:<stock>` | Closed at root; null-fill on `{}` |

**Conversions:** use **`math.convert`** (`m.convert`), **`list.convert`** (`L.convert`), **`str.convert`** — no implicit widen/narrow on assign.

---

## 4. Schema declarations

```text
<stock>:
   location: t4
   product: t8
   qty: u16

<var>:
   userId: s16
   stocks: <stock>[10]
```

- Schemas are **non-recursive**.
- Define `<stock>` before `<var>` uses it.
- Compiled **`.fb`** will carry a **SCHM** catalog (`schema_id` 0 … n−1).

**Bind:**

```text
myStock:<stock> = {}
myStock:<stock> = { qty: 2, product: "ab" }
```

Extra keys in literals or assigns → panic. Missing schema fields → **`null`**.

---

## 5. Open `o` vs closed `<schema>`

```text
bag:o = {}
bag.extra = 1

myStock:<stock> = {}
myStock.extra = 1
```

`extra` on `<stock>` → **`type.schema.field.unknown`**.

**Nested open field** in a schema:

```text
<config>:
   db: o

myConfig:<config> = {}
myConfig.db.pass = "secret"
myConfig.user = "x"
```

Only `db` is allowed on root; arbitrary keys under **`db`** are OK.

---

## 6. Path assign & `obj.set2`

- **`stock.qty = 10`** and **`O.set2(stock.qty, 10)`** use the same checker.
- **LIST index** in a path must **exist** (empty list + `[0]` → panic — same as today).
- **`O.set2(result.s.location, "A101")`** on `result:<select>` with `s: <stock>` **materializes** `s` when it was `null`, then sets `location`.

Sugar **`expr:targetType`** in native calls (e.g. `L.push(xs, a:s16)`) is **deferred**; use **`m.convert`**.

---

## 7. Dynamic type from STRING

```text
payloadSchema = "<var>"
payload:/payloadSchema = {}
```

The STRING is read **each time** the line runs. Schemes in STRING are filtered by **`V.dSchemaAllow(ctx, …)`** and **`V.dSchemaList(ctx, list)`** (entries like `"<var>"`). Once **`dSchemaList`** has run, an **empty** list **`[]`** is a **fail-closed** whitelist (no schema permitted); see [natives/vcx.md](./natives/vcx.md).

---

## 8. Comparison & JSON

| Case | Rule |
|------|------|
| `a:u8` vs `b:u16` in `==` | Panic **`type.compare.incompatible`** |
| Two LIST/OBJECT values | Compare **`json.stringify(a)`** to **`json.stringify(b)`** |
| **`===` (type + value)** | Deferred |

**Decode:** `row:<stock> = json.parse_object(s)` — validation on **assign** to typed slot; missing keys → `null`.

---

## 8.1 Typed `m.calc` and I/O (T4+)

**`m.calc(expr)`** — unchanged (NUMBER). **`m.calc(expr, "u32")`** — result checked in the native (overflow → **`type.calc.range`**). Without arg 2, use **`-> slot:u32`** for the same check at assign.

If any variable in **`expr`** has a **numeric** slot contract, **every** variable in **`expr`** must share the **same** contract (else **`type.calc.type`**). Untyped-only expressions keep legacy rules.

**I/O:** optional second argument type string — **`io.in_num('Price:', 'u8')`**, **`io.inln('Name:', 't8')`**, **`io.in_bool('ok?', 'b')`**. After read, value is validated (`type.io.expect`, **`type.io.len`** for `tN`). Assign to a typed **`->`** target still uses assign checks.

---

## 9. `const` and `freeze()`

```text
const limit:u16 = 64
cfg:<config> = {}
freeze(cfg)
```

- **`const name:type = expr`** — immutable from first bind (compile-time requires **`:type`**; not `:=:`). Re-assign or path write → **`type.const.assign`**.
- **`freeze(name)`** — pair with **`free(name)`** at the same language level (not `mem.freeze`); flag on **(VCX, slot)**. After freeze, assign / path write → **`type.freeze.assign`**.
- **`free(var)`** — clears value, static type contract, **`slot_frozen`**, and **`slot_const`** (re-bind allowed).
- **I/O context** — binding frozen or **const** variables into **`io.ctxout`** / **`io.ctxerr`** → **`type.freeze.ctx`** (same code for both flags).
- **VCX copy** — `V.copy` / stage restore copies **frozen** and **const** bits per slot.

---

## 10. Implementation roadmap

| Phase | Deliverable |
|-------|-------------|
| **T0** | Docs, specimens, doc-site (this release) |
| **T1** | Parse `:type`, slot metadata, assign checks |
| **T2** | **Done:** SCHM, `<schema>:`, `var:<schema>`, null-fill on `{}`, `type.schema.*` panics |
| **T3** | **Done:** path assign, compound LHS, typed `O.set2`, nested schema materialize |
| **T4** | **Done:** `:/expr`, `dSchema*` |
| **T5** | **Done:** `const`, `freeze()`, `free` clears flags, ctxout guard, VCX copy |
| **T4+** | **Done:** `m.calc` arg 2, `io.in_*` type arg, `type.calc.*` / `type.io.*` |
| **T6** | **Done:** typed **`def`** (mixed formals, typed return), **`O.set`** on schema root var + dot path, **`O.set2`** full path |
| **T7** | **Done:** **`tN` 1…8192**, SCHM **v8** aux 16-bit; **`io.inln`** REF peste 127 octeți |

Specimens and T0 tests: `test/cases/types_t0/`.
