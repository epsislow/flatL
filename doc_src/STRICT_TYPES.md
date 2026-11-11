# Strict types & object schemas (overview)

**Status:** **T1** — `name:type` on **ASSIGN** is enforced in `flvm` for scalars, `b`, `t` / `tN`, `l`, `o`. Schemas (`<name>`), dynamic (`:/expr`), `const`, and `freeze` remain **T2+**.

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
| Unsigned / signed int | `u8`, `s16`, `u32`, … | Range checked on bind |
| Float | `f32`, `f64` | `f8`, `q4p4` deferred |
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

The STRING is read **each time** the line runs. Schemes in STRING are filtered by **`V.dSchemaAllow(ctx, …)`** and **`V.dSchemaList(ctx, list)`** (entries like `"<var>"`).

---

## 8. Comparison & JSON

| Case | Rule |
|------|------|
| `a:u8` vs `b:u16` in `==` | Panic **`type.compare.incompatible`** |
| Two LIST/OBJECT values | Compare **`json.stringify(a)`** to **`json.stringify(b)`** |
| **`===` (type + value)** | Deferred |

**Decode:** `row:<stock> = json.parse_object(s)` — validation on **assign** to typed slot; missing keys → `null`.

---

## 9. `const` and `freeze()`

```text
const limit:u16 = 64
cfg:<config> = {}
freeze(cfg)
```

- **`const`** — immutable from first bind (not `:=:`).
- **`freeze(name)`** — pair with **`free(name)`**; flag on **slot**; copy VCX copies frozen bit.

---

## 10. Implementation roadmap

| Phase | Deliverable |
|-------|-------------|
| **T0** | Docs, specimens, doc-site (this release) |
| **T1** | Parse `:type`, slot metadata, assign checks |
| **T2+** | SCHM, schema objects, `:/`, `m.calc` typed, `def` typed |

Specimens and T0 tests: `test/cases/types_t0/`.
