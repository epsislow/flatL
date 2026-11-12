# Typed bind, schemas, and slot contracts (T0 draft)

**Status:** **T1–T4 implemented** in `flc`/`flvm` (typed `m.calc` / typed I/O remain T5+).  
**Overview:** [STRICT_TYPES.md](./STRICT_TYPES.md)  
**Roadmap:** [`.cursor/plans/strict-types-schemas-roadmap.plan.md`](../.cursor/plans/strict-types-schemas-roadmap.plan.md)  
**Ideas sketch:** [`.cursor/my_ideas/strict_types_in_flatl_and_schemas_sketch.txt`](../.cursor/my_ideas/strict_types_in_flatl_and_schemas_sketch.txt) (aligned)  
**Current runtime:** [LANGUAGE_TYPES.md](./LANGUAGE_TYPES.md) §1 and §6 (logical `VAL_*` + T1/T2 typed bind)

---

## 1. Activation (opt-in)

Typed checks apply **only** where the source uses **`name:type`**, **`name:/expr`**, **`const name:type =`**, or **`-> name:type`**.

Programs **without** those forms behave as today (any `VAL_*` on bind, no `type.*` panics).

There is **no** global `use strict types` directive.

---

## 2. Where the contract lives

- **Name** → compile-time **slot index** (as today).
- **Value** → per **VCX** in the pool.
- **Type / frozen / dynamic `:/`** → metadata on **`(VCX, slot)`**, not on the name globally.

Two contexts may bind the same name to different contracts without conflict.

**`free(var)`** clears value **and** slot contract (type + frozen). **Retype** without `free` → **`type.assign.retype`** (panic).

---

## 3. Type catalog (v1)

| Kind | Forms | Notes |
|------|--------|--------|
| Integer | `u8`…`u64`, `s8`…`s64` | Range at assign; **`u64`/`s64`**: exact integers only up to **`2^53−1`** (`type.assign.range.i64` beyond) |
| Float | `f32`, `f64` | `f8`, `q4p4` **deferred** |
| Bool | `b` | `true` / `false` |
| String | `t` (= **`t127`**), `tN` (N ≤ 127) | Length at assign |
| List | `l`, `o[]`, `l[]`, `elemType[]`, `elemType[N]` | `[N]` = max length, snapshot at bind |
| Object open | `o` | Add fields on assign; read missing field → panic |
| Schema | `<name>` | Closed at root; null-fill on `{}` |

**Lists:** `products:t255[]` = unbounded count, elements `t255`. `selected:<stock>[4] = []` = max 4, may start empty.

**List slot (variable):** `xs:<stock>[4] = []` — the slot type is a **list of `<stock>`** capped at **4** items (`list_max` snapshot at bind). Assign `xs = [a, b, …]` checks length → **`type.assign.list.len`**. This is **not** path index syntax; `xs[0] = row` is a path on an existing list slot.

**No implicit conversion** at assign/COW — use **`math.convert`** / **`list.convert`** / **`str.convert`** (`m.` / `L.` / `str.` aliases).

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

- **Non-recursive** schemas only (`type.schema.cycle` at compile).
- **Order:** define `<stock>` before `<var>` references it.
- **`.fb` SCHM:** schemas stored **`schema_id` 0 … n−1**; source still uses `<stock>` names.

---

## 5. Bind rules (summary)

| Form | Behaviour |
|------|-----------|
| `myStock:<stock> = {}` | All schema fields present, values **`null`** |
| `myStock:<stock> = { qty: 2 }` | Valid subset; missing fields → **`null`**; extra keys → panic |
| `bag:o = {}` | Open object |
| `myStock.campExtra = 1` | panic — unknown field on `<stock>` |
| `o:a = 3` on `o:o` | OK — open assign |
| `io.out(o.a)` on `o:o` without prior `o.a = …` | panic — unknown field on read |
| `<config>` with `db: o` | Only declared root fields; under **`db`** any keys OK |

**LHS compound:** `o:<var>.a = 3` may create slot + schema + null-fill + set.  
**Dynamic:** `payload:/payloadSchema` — STRING read each run; scheme policy via **`V.dSchemaAllow(ctx, …)`**, **`V.dSchemaList(ctx, list)`** with `"<schema>"` entries. After **`dSchemaList`**, **`[]`** means **deny all** schema names (not “allow all”). See [natives/vcx.md](./natives/vcx.md) § Typed dynamic binds.

---

## 6. Assign, COW, natives

- **`L.push`**, internals of mutators — no schema panic.
- **Bind to typed slot** (`=`, `->`, COW write-back, **`O.set2(path, val)`**) — **`flat_assign_typed_slot`** (same as path assign).
- **`O.set2`:** validate **full path**; LIST index must exist (general list rule); OBJECT last segment create/update; null **`<stock>`** field → materialize sub-object then set.

---

## 7. `==`, JSON

- **Typed scalars**, different slot contracts → **`type.compare.incompatible`** (panic).
- **LIST / OBJECT** → compare **`json.stringify(a) == json.stringify(b)`** (boolean, no panic for `o` vs `<schema>` shape).
- **`===` (type + value):** deferred.
- **Decode:** `json.parse_object(s)` then assign to **`name:<schema>`** — validate + null-fill missing keys.

API today: **`json.stringify`**, **`json.parse_object`** ([natives/json.md](./natives/json.md)).

---

## 8. `freeze()` and `const`

| Mechanism | Meaning |
|-----------|---------|
| **`const name:type = expr`** | Immutable from first bind |
| **`name:type = expr`** then **`freeze(name)`** | Mutable until **`freeze(name)`** |
| **`freeze(name)`** | Slot flag (pair with **`free(name)`**); not `mem.freeze` |

Copy VCX copies frozen flag per slot. Frozen + **`io.ctxout`** → panic.

---

## 9. `foreach`, `m.calc`, `def` (planned)

- **`foreach(coll, item, '#block')`:** for `coll:<stock>[]`, **`item`** follows element schema `<stock>`.
- **`m.calc(expr)`** optional 2nd arg type; without it result is NUMBER; **`-> sum:u32`** checks at assign; typed vars in expr must share same numeric type.
- **`def f(a:u8, b) -> out:u16`:** mixed typed/untyped formals — future + `.fmod` metadata.

Sugar **`a:s16`** in native args — **deferred** (use **`m.convert`**).

---

## 10. Implementation phases

| Phase | Scope |
|-------|--------|
| **T0** | This doc + panic table + SCHM concept |
| **T1** | **Done:** parse `:type` on ASSIGN, slot metadata on VCX, assign checks, `free` clears type (`u*`/`s*`/`f*`/`b`/`t`/`tN`/`l`/`o`) |
| **T2** | **Done:** `<schema>:` blocks, **`var:<schema>`**, SCHM in **`.fb` v6**, null-fill on `{}`, compile checks (`type.schema.cycle`, `type.schema.unknown`), runtime `type.schema.field.unknown` |
| **T3** | **Done:** `var.field = …`, `var:<schema>.field = …`, typed `O.set2`, schema on path, nested null-fill on write |
| **T4** | **Done:** `name:/expr`, `V.dSchemaAllow`, `V.dSchemaList` |
| **T4+** | typed `m.calc` / I/O |
| **T5** | `const`, `freeze()` |
| **T6** | Typed `def`, optional native sugar |

---

## 11. Panic prefix `type.*` (selection)

See plan §10 for full table. Examples: `type.assign.incompatible`, `type.assign.range`, `type.assign.range.i64`, `type.assign.list.len`, `type.schema.field.unknown`, `type.assign.retype`, `type.freeze.assign`, `type.compare.incompatible`, `type.path.index`, `type.call.arg` (future).

User-facing messages prefer **expected** / **assigned**, not lhs/rhs.
