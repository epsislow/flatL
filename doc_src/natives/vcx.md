# Native module `vcx`

**Variable contexts (VCX)** — multiple isolated variable storages at runtime, selected with **`vcx.set`**. **Optional** plugin (not core):

```text
use native vcx with V
```

Compile-time variable **names** remain program-wide (no lexical scope). VCX duplicates **values** per context ID, not names.

## Quick example

```text
use native vcx with V

#main
  globalCtx = V.current()
  user = 'John'
  scratchCtx = V.create(20)
  V.set(scratchCtx)
  user = 'Alice'
  V.set(1)
  io.outln(user)
  OK
```

Output: **`John`**.

## Syntax

| Form | Example |
|------|---------|
| Arrow binding | `V.current() -> activeId` |
| Assign | `scratch = V.create(20)` |
| `set` with literal context | `V.set(1)` (global) |

**Context IDs** in arguments are **NUMBER** expressions: literals (**`1`** = global), variables holding an ID returned by **`create`**, or **`current()`**. Splitting on **top-level commas** matches other natives (commas inside quoted strings do not split).

With **`use native vcx with V`**, examples use the alias **`V.*`**; the full names are **`vcx.*`** (e.g. **`vcx.varsCopy`**, **`vcx.hasVar`**).

**IDs in variables:** a value stored as **`scratchCtx`** exists in whatever context was **active when that variable was bound**. After **`V.set`**, use a **literal** such as **`1`** or **`V.var(otherCtx, 'name')`** to read from another storage without guessing which slot holds the ID.

## Function reference

| Call | Arguments | Returns | Panic (no modifier) |
|------|-----------|---------|---------------------|
| **`V.current()`** | — | NUMBER — active context ID (global is **1**) | **`vcx.current`** — no VCX runtime |
| **`V.set(ctx)`** | **`ctx`** — context ID | — | **`vcx.set.arg`**, **`vcx.set.invalid`** |
| **`V.create(capacity)`** | **`capacity`** — positive **whole** NUMBER (≥ 1) | NUMBER — new context ID (does **not** switch active) | **`vcx.create.arg`**, **`vcx.create`**, **`vcx.create.limit`** |
| **`V.var(srcCtx, name)`** | **`srcCtx`** — ID; **`name`** — STRING (literal or variable holding name) | any value (deep copy from **`srcCtx`**) | **`vcx.var.arg`**, **`vcx.var.missing`**, **`vcx.var.copy`** |
| **`V.hasVar(ctx, name)`** | **`ctx`** — ID; **`name`** — STRING | BOOL | **`vcx.hasVar.arg`**, **`vcx.hasVar.ctx`** |
| **`V.varsCopy(src, dst)`** | two context IDs | — | **`vcx.varsCopy.arg`**, **`vcx.varsCopy`** (e.g. capacity / copy) |
| **`V.varsCopyLike(src, dst, pattern)`** | two IDs; **`pattern`** — STRING glob | — | **`vcx.varsCopyLike.arg`**, **`vcx.varsCopyLike.pattern`**, **`vcx.varsCopyLike`** |
| **`V.varsUsed(ctx)`** | context ID | NUMBER — bound names in that context | **`vcx.varsUsed.arg`**, **`vcx.varsUsed.ctx`** |
| **`V.varsAvail(ctx)`** | context ID | NUMBER — **`varsMax − varsUsed`** | **`vcx.varsAvail.arg`**, **`vcx.varsAvail.ctx`** |
| **`V.varsMax(ctx)`** | context ID | NUMBER — **capacity** of that context | **`vcx.varsMax.arg`**, **`vcx.varsMax.ctx`** |

**`varsCopy` / `varsCopyLike`:** deep copy of **values** into **`dst`** (atomic for the operation). **`src == dst`** is a no-op for **`varsCopy`**. **`varsCopyLike`** applies a **glob** to **variable names** only (`*`, `?`), case-sensitive, **whole** name match.

## `var_slots` (compile) vs context **capacity** (runtime)

At **compile** time, `flc` scans the whole program and counts **distinct bind names** (assign LHS, `io`/`call` bind targets, `foreach`/`for` item vars, …). That count is written into the `.fb` as **`var_slots`** (≤ `FLAT_MAX_VARS`). It is **fixed for one compiled program** — it does **not** grow when you bind more values at runtime.

| Context | Capacity (`varsMax` / `mem.vars_max` when active) |
|---------|---------------------------------------------------|
| **Global** (ID **1**) | **`var_slots`** from bytecode |
| **`create(N)`** | **`N`** (chosen at the `create` call; unchanged for that ID) |

Example: only `globalCtx`, `user`, `scratchCtx` in source → **`var_slots = 3`**, global **`mem.vars_max() = 3`**. After **`V.create(20)`** and **`V.set(scratchCtx)`**, **`mem.vars_max() = 20`** — that is the **`create`** size, not `var_slots`.

**`mem.vars_used()`** / **`vars_avail()`** on the active context: `used` = names bound **in that storage**; `avail = capacity − used`.

### Can capacity become 21?

- **At runtime, no.** Global stays **`var_slots`**. An existing **`create(N)`** context keeps **N**. When **`used == capacity`**, another **new** bind in that context fails; bytecode **`var_slots`** is unchanged.
- **After recompile:** adding an 11th distinct bind name in source sets **`var_slots = 11`** in a **new** `.fb`; global **`varsMax`** on the next run is **11**. Contexts already created with **`create(20)`** still cap at **20** unless you **`create(21)`** (new ID) and copy.

Compile-time **names** are program-wide: the slot table is the universe of names the program may bind.

## `mem.*` vs `vcx.*`

**`mem.vars_used`**, **`mem.vars_max`**, **`mem.vars_avail`**, **`mem.free`**, and other **`mem.*`** counters refer to the **active** context (same as **`vcx.current()`**). Use **`vcx.varsUsed(ctx)`**, **`vcx.varsMax(ctx)`**, … to inspect another context without **`set`**.

With **VCX off**, **`mem.vars_max()`** is always program **`var_slots`** (single storage).

## Control flow

**`vcx.set`** is **not** restored on **`return`** from a `#sub`. Save **`V.current()`**, restore explicitly after **`call`** if needed.

## `io.ctx*` (capture) and **`V.set`**

**`io.ctxout`**, **`io.ctxerr`**, and **`io.ctxin`** bind to a variable **by name** and **`STR_REF` id** (see [IO_CONTEXTS.md](../IO_CONTEXTS.md) § `io.ctx_stale`). When you **`V.set(otherCtx)`**, the **active** storage changes: a name such as **`buf`** may refer to **nothing** or a **different** ref in that context.

- **Safe:** capture to **`buf`** in global, **`V.set(scratch)`**, then **`V.set(1)`** back — global **`buf`** still holds the same ref; output continues to append to that buffer.
- **Unsafe without fixing the binding:** capture in global, switch to scratch, then **`io.out`** / **`io.err`** while **`buf`** is not the same ref in scratch — **PANIC** **`io.ctx_stale`**.

Restore **`tout`** / **`terr`** when you need terminal output again; captured text is in the REF until you print it (e.g. `io.outln(buf)`).

## PANIC sub-reasons (summary)

| Situation | Typical reason string |
|-----------|------------------------|
| Bad context ID on **`set`** | **`vcx.set.invalid`** |
| Bad args / alloc for **`create`** | **`vcx.create.arg`**, **`vcx.create.limit`**, **`vcx.create`** |
| Missing var on **`var`** | **`vcx.var.missing`** |
| Deep copy failure on **`var`** | **`vcx.var.copy`** |
| Bad **`ctx`** on **`hasVar` / `varsUsed` / …** | **`vcx.<fn>.ctx`** (e.g. **`vcx.varsMax.ctx`**) |
| Bad / missing arg on **`vars*`** stats | **`vcx.<fn>.arg`** |
| Copy capacity / failure | **`vcx.varsCopy.capacity`**, **`vcx.varsCopy.copy`** (and same for **`varsCopyLike`**) |
| Empty or bad glob pattern | **`vcx.varsCopyLike.pattern`** |

Line modifiers **`?`** / **`~`** follow the usual native rules ([SPECIFICATION.md](../SPECIFICATION.md) §1.2.2).
