# Dynamic block call (`call block`)

Compile-time **`call #worker`** jumps to a block whose label is fixed in the `.fb`.  
Runtime **`call block` *expr*** jumps to whichever block matches a **numeric block id** produced by *expr*.

## Syntax

```text
call block <expression>
```

- **&lt;expression&gt;** — same family of expressions as the right-hand side of an assign (variables, numeric literals, …). Must evaluate to a **non-negative whole NUMBER** (block id).
- Optional **`-> bind`** after the expression is parsed for future use; it does not change VM behaviour today (same as empty bind on static `call`).

**Not** the same token sequence as **`call #label`** — the keyword **`block`** after **`call`** selects the dynamic form.

## Block ids

Each block in the `.fb` directory has a **`block_id`** (see [SPECIFICATION.md](./SPECIFICATION.md) §3):

| Block | `block_id` |
|-------|------------|
| **`#main`** | **0** |
| Any other **`#label`** | Stable **uint32** hash of the label string (including `#`) |

To call `#worker` dynamically, *expr* must yield that hash (e.g. store it in a variable at authoring time, or pass it from metadata).

Example (`#worker` id **2812593762** in the reference hash function):

```text
#worker
  result = 'done'
  return

#main
  wid = 2812593762
  call block wid
  io.outln(result)
  OK
```

Static call remains valid:

```text
call #worker
```

## PANIC (no line modifier)

| Reason | When |
|--------|------|
| **`call.block.arg`** | Missing/invalid expression, or value is not a NUMBER |
| **`call.block.id`** | NUMBER is not a whole unsigned block id |
| **`call.block.notfound`** | No block with that id in this program |
| **`call.block.limit`** | Call stack depth exceeded (same limit as **`call #…`**) |

Unhandled PANIC follows the usual `#panic.any` / exit rules.

## Use cases

- Executors that receive **`targetBlock`** as a NUMBER (user functions roadmap).
- Dispatch tables without one static `call #…` per target.

VM opcode: **`OP_CALL_BLOCK` (`0x0E`)**.
