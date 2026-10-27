# flatL — architecture reference (draft)

Consolidated from project design notes. Authoritative details will move here as `flc` and `flvm` land in the repo.

## 1. Flat graph philosophy (FGIR v1.0)

Programs are **not** syntax trees. They are a **flat graph** of **basic blocks**:

- Each block is a contiguous sequence of instructions (`#main`, `#verifica_balanta`, …).
- Control flow is **explicit**: guards, backtrack labels, panic labels, and `call` targets are known at compile time.
- **Logical failure** (`FAIL`, failed guards) is separate from **infrastructure panic** (I/O errors, OOM, timeouts).

### Backtrack addressing

When guard at instruction index *n* in block `#target` fails, runtime seeks:

```text
#backtrack.#target.n
```

If no block exists, the enclosing flow aborts with `FAIL`.

### Panic addressing

On infrastructure fault at index *n* in `#target`:

```text
#panic.#target.n
```

Fallback: `#panic.any` (e.g. transaction rollback via `tstop()`).

## 2. Iteration

`foreach(collection, key_var, '#loop_body')`:

1. Bind iterator over `collection`.
2. Jump to `#loop_body` for each element.
3. At end of loop block, resume `foreach` cursor; when exhausted, continue sequentially below `foreach`.

Empty backtrack body on a loop guard implies **continue** to next item.

## 3. Binary format (v1.1)

See [README](../README.md#fb-binary-layout-v11) for header and opcode table.

Instruction packets in the payload segment:

```text
+--------+--------------+---------------------------+
| Opcode | Payload size | Metadata / jump indices   |
| 1 byte | 1 byte       | variable                  |
+--------+--------------+---------------------------+
```

Compilation example — source:

```text
#main
  is_empty == true
  end.msg('Succes')
  OK
```

Bytecode sequence (conceptual):

1. `OP_GUARD` — payload: backtrack jump address  
2. `OP_ASSIGN` — payload: `end.msg('Succes')`  
3. `OP_OK`

## 4. Planned C IR shapes

- `Instruction` — statement text + index (parser phase)  
- `BasicBlock` — label, instructions[], parameters[]  
- `ExecutableProgramIR` — block array  
- Software call stack: `return_block_label`, `return_instruction_index`

VM: load `.fB`, directory lookup by hash/offset, `switch(opcode)` dispatch loop with instruction pointer and call stack.

## 5. Future: dynamic modules

Independent modules compiled to native shared libraries; block lookup via `dlopen` / `dlsym`; rebinding without full process restart.
