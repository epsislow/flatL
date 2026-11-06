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

## API

| Call | Returns | Notes |
|------|---------|--------|
| `current()` | NUMBER | Active context ID (global starts at **1**) |

Context IDs stored in variables exist only in the **context where that variable was bound**. After **`set`**, use a numeric literal (e.g. **`1`** for the global context) or **`vcx.var(otherCtx, 'name')`** to recover a value from another storage.
| `set(ctx)` | — | Switch active storage; invalid ID → PANIC **`vcx.set.invalid`** |
| `create(capacity)` | NUMBER | New empty context; does not become active |
| `var(srcCtx, name)` | value | Read from **`srcCtx`**; missing → **`vcx.var.missing`**; use **`=` / `->`** to bind in active context |
| `hasVar(ctx, name)` | BOOL | No panic if name absent |
| `varsCopy(src, dst)` | — | Deep copy all vars; atomic; **`src == dst`** no-op |
| `varsCopyLike(src, dst, pattern)` | — | Glob on **variable names** only (`*`, `?`) |
| `varsUsed(ctx)` | NUMBER | Bound variables in that context |
| `varsAvail(ctx)` | NUMBER | `capacity - used` |
| `varsMax(ctx)` | NUMBER | **Capacity** of that context (see below) |

Wildcards match the **whole** variable name (case-sensitive). Pattern `*` selects all names (similar to full **`varsCopy`** source set).

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

## PANIC sub-reasons

| Situation | Reason |
|-----------|--------|
| Bad context ID on **`set`** | **`vcx.set.invalid`** |
| Bad args / alloc | **`vcx.create`**, **`vcx.create.limit`**, **`vcx.create.arg`** |
| Missing var on **`var`** | **`vcx.var.missing`** |
| Copy capacity / failure | **`vcx.varsCopy.capacity`**, **`vcx.varsCopy.copy`** |
| Bad pattern | **`vcx.varsCopyLike.pattern`** |
| Bad ctx on introspection | **`vcx.varsUsed.ctx`** (same family for **`varsAvail`**, **`varsMax`**, **`hasVar`**) |

Line modifiers **`?`** / **`~`** follow the usual native rules ([SPECIFICATION.md](../SPECIFICATION.md) §1.2.2).
