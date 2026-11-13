# Typed bind, schemas, and slot contracts (T0 draft)

**Status:** **T1–T6** in `flvm` (through typed **`def`**, **`O.set`**, **`O.set2`** path engine).  
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
| Float | `f32`, `f64` | Same check as `f64` today (any `VAL_NUMBER`); precision at AOT — [LANGUAGE_TYPES.md §6](./LANGUAGE_TYPES.md#6-static-slot-contracts-strict-types). `f8`, `q4p4` **deferred** |
| Bool | `b` | `true` / `false` |
| String | `t` (= **`t127`**), `tN` (**`1…8192`**) | Length at assign; REF OK peste 127 octeți |
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
| **`const name:type = expr`** | Immutable from first bind; **`:type` required** at compile; any re-assign or path write → **`type.const.assign`** |
| **`name:type = expr`** then **`freeze(name)`** | Mutable until **`freeze(name)`**; then same as frozen |
| **`freeze(name)`** | Slot flag (pair with **`free(name)`**); not `mem.freeze`; undeclared name → compile error |

**`free(var)`** clears frozen and const flags together with the slot type contract.

Copy VCX copies **frozen** and **const** flags per slot. Frozen or **const** slot used in **`io.ctxout`** / **`io.ctxerr`** → **`type.freeze.ctx`**.

---

## 9. `m.calc`, typed I/O, `def`

- **`m.calc(expr ; "u32")`** — tag tip numeric; overflow → **`type.calc.range`**; token invalid → **`type.tag.not_accepted`**. Fără `;`, **`-> sum:u32`** verifică la assign. Vezi [TYPE_TAGS.md](./TYPE_TAGS.md).
- **Homogeneous operands:** if any slot in **`expr`** is typed numeric, all names in **`expr`** must match that contract (**`type.calc.type`**).
- **`io.in_num` / `io.inln` / `io.in_bool`** — optional **`, "type"`** after prompt; **`type.io.expect`** / **`type.io.len`**.
- **`foreach(coll, item, '#block')`:** for `coll:<stock>[]`, **`item`** follows element schema `<stock>`.
- **`def f(a:u8, b) -> out:u16`:** mixed typed/untyped formals; types on the function are recorded at compile; bad values at call → **`type.call.arg`** / **`type.assign.range`** (same family as typed assign).

Sugar **`a:s16`** in native args — **deferred** (use **`m.convert`**).

### 9.1 Typed `def` at call — parameters and returns

This section is for **callers** of user functions: what flatL checks when you pass arguments or receive return values. See also [USER_FUNCTIONS.md](./USER_FUNCTIONS.md) (including multi-return).

**Rule of thumb:** the **only** type contracts enforced at a user function call are those on the **`def`** line (typed formals and typed return slots). The VM does **not** compare “caller variable type” vs “formal type” as two labels. It takes the **value** and applies the same typed-bind rules as for **`name:type =`** (numeric range, exact integers where required, **`type.assign.loss`** for **`f32`**, and so on).

There is **no** syntax for types inside the argument list — only expressions:

```text
f(expr1, expr2)          ; OK
f(x:u32, y)              ; not supported (deferred sugar)
```

A typed variable in the caller (e.g. **`x:u32 = 5`**) is just an expression. Passing **`x`** does **not** mean “bind as u32”; it means “use the **current value** of **`x`**” and, if formal **`a:u8`** is typed, check whether that value fits **`u8`** (coerce when allowed, panic otherwise).

#### Parameters

| Caller | `def` formal | What flatL does |
|--------|----------------|-----------------|
| `f(5)` | `a:u8` | Value **5** checked against **`u8`**; OK if in range |
| `x:u32 = 5`, `f(x)` | `a:u8` | Value **5** checked against **`u8`** — **not** “u32 vs u8 conflict” |
| `x:u32 = 40000`, `f(x)` | `a:u8` | Value out of range → panic (e.g. **`type.assign.range`**, reported as **`type.call.arg`**) |
| `f(big)` | `b` (untyped) | No typed check on that formal |
| Wrong kind (e.g. list into `u8`) | typed formal | **`type.assign.incompatible`** / **`type.call.arg`** |

**Not done today (and not planned):** reject a call because the **source variable** has a **wider** typed slot than the formal (e.g. forbid **`u32` → `u8`** even when the value is **5**). If the value fits the formal contract, the call proceeds.

#### Returns

| Call form | `def` return | What flatL does |
|-----------|----------------|-----------------|
| `g() -> r` | `out:u16` | Return value checked against **`out:u16`**, stored in **`r`** |
| `g() -> r:u32` | `out:u16` | **`r:u32`** is caller syntax (like typed assign on the LHS); lowering uses the name **`r`**. Enforcement is still against the **formal return type on `def`**, not a second “call-site return type” contract |
| Multi: `h() -> a, b` | `lo:u8, hi:u16` | One check per return slot on **`def`**; caller names **`a`**, **`b`** only |

**Same as parameters:** no comparison “caller slot type vs formal return type”. Only **value vs typed formal** (per return slot). Untyped return formals copy without that typed gate.

#### Parameters vs returns (behaviour)

| | **Parameters** | **Returns** |
|---|----------------|-------------|
| Types that matter at runtime | Typed formals on **`def`** | Typed return slots on **`def`** |
| What you write at call | Expressions in **`(...)`** | Names after **`->`** or left of **`=`** (multi-return); optional **`name:type`** on those names |
| Typed variable in caller | Value of var → formal check | N/A for args; on return, only the **name** binds the result |
| Formal untyped | No typed check on that parameter | No typed check on that return |
| Precision / range | Same as typed assign (no silent wrap on **`u8`**, no inexact **`f32`**, and so on) | **Same rules** when the return slot is typed |
| Global vs unique executor | Same typed rules when entering the function | **Unique:** explicit return copy with typed checks. **Global:** body runs in caller VCX; return often appears as assign (e.g. **`r = out`**) after the call — value already produced under formal names in the body |

#### Numeric values (AM-NUM)

Caller slot types such as **`u32`** store integers with **width metadata**, not “everything is **`f64`**”. At the function boundary, flatL still runs the **typed-bind** pipeline on the **value**: allowed narrowing/coercion with range and exactness rules; failure → panic. That applies **equally** to arguments and to typed returns.

**Summary:** parameters and returns are **symmetric**: **`def`** owns the contract; the call site supplies **values** (args) and **destination names** (returns). Typed slots in the caller are **not** a separate contract layer at the call boundary.

**Tests:** `test/cases/types/098`–`099`, `112`–`134` (typed `def` at call: wide caller vars, range, `u*`/`s*`/`f*` crossings, return LHS wider than formal, multi-return).

---

## 10. Implementation phases

| Phase | Scope |
|-------|--------|
| **T0** | This doc + panic table + SCHM concept |
| **T1** | **Done:** parse `:type` on ASSIGN, slot metadata on VCX, assign checks, `free` clears type (`u*`/`s*`/`f*`/`b`/`t`/`tN`/`l`/`o`) |
| **T2** | **Done:** `<schema>:` blocks, **`var:<schema>`**, SCHM in **`.fb` v6**, null-fill on `{}`, compile checks (`type.schema.cycle`, `type.schema.unknown`), runtime `type.schema.field.unknown` |
| **T3** | **Done:** `var.field = …`, `var:<schema>.field = …`, typed `O.set2`, schema on path, nested null-fill on write |
| **T4** | **Done:** `name:/expr`, `V.dSchemaAllow`, `V.dSchemaList` |
| **T5** | **Done:** `const`, `freeze()`, ctxout guard, VCX copy of flags |
| **T4+** | **Done:** `m.calc` arg 2, typed `io.in_*`, `type.calc.*` / `type.io.*` |
| **T6** | **Done:** typed `def`, `O.set`/`O.set2` path parity (`vcx.bindCallParams`, `vcx.copyCallReturn`) |

---

## 11. Panic prefix `type.*` (selection)

See plan §10 for full table. Examples: `type.assign.incompatible`, `type.assign.range`, `type.assign.range.i64`, `type.assign.list.len`, `type.schema.field.unknown`, `type.assign.retype`, `type.freeze.assign`, `type.compare.incompatible`, `type.path.index`, `type.call.arg` (typed `def` param bind, T6).

User-facing messages prefer **expected** / **assigned**, not lhs/rhs.
