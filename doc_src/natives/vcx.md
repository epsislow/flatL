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
| `varsMax(ctx)` | NUMBER | Fixed capacity (global = program `var_slots`) |

Wildcards match the **whole** variable name (case-sensitive). Pattern `*` selects all names (similar to full **`varsCopy`** source set).

## `mem.*` vs `vcx.*`

**`mem.vars_used`**, **`mem.vars_max`**, **`mem.vars_avail`**, **`mem.free`**, and other **`mem.*`** counters refer to the **active** context (same as **`vcx.current()`**). Use **`vcx.varsUsed(ctx)`** etc. to inspect another context without **`set`**.

## Control flow

**`vcx.set`** is **not** restored on **`return`** from a `#sub`. Save **`V.current()`**, restore explicitly after **`call`** if needed.

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
