# Jump entry limits (`jump:blockMax`)

Programs may cap how many times a block may be **entered via `jump`** during one run. This is separate from the VM-wide jump budget (`vm.jump_limit`; see [Specification](./SPECIFICATION.md)).

## Global directive

Place in the file header (with `use`, `<schema>:`, `func:…`, before the first `#block`):

```text
jump:blockMax 100
```

| Value | Meaning |
|-------|---------|
| omitted | no per-target limit from source (only `vm.jump_limit` may apply at runtime) |
| `0` | unlimited entries via `jump` for every target block (unless overridden) |
| `N > 0` | at most `N` successful `jump` entries per target block |

Duplicate `jump:blockMax` lines in one file → compile error.

## Per-block override

On the block declaration line:

```text
#turn jump:blockMax 50
#wait jump:blockMax 0
```

| Form | Meaning |
|------|---------|
| no suffix | use global limit |
| `jump:blockMax 0` | this block is unlimited, regardless of global |
| `jump:blockMax N`, `N > 0` | limit entries into **this** block via `jump` |

## What counts

Only **`jump #block`** (compiled `OP_JUMP` to that block):

- Each such transfer increments the counter for the **target** block **before** entry.
- Entries **1…N** succeed; attempt **N+1** triggers PANIC **`jump.block.reached.max`** (you do not enter the block).

**Not counted:** `call #block`, `next` / `for` / `foreach` body entry, `#or` handler entry, `return`, initial `#main` start.

Counters live for one `flvm` run and are not reset by `panicReset()` or recovered `#panic` handlers.

## Panic and handlers

| | |
|-|-|
| Reason | `jump.block.reached.max` |
| Class | infrastructure PANIC (not logical `FAIL`) |
| Routing | same as other panics: `#panic.#<block>.<n>` on the `jump` line, line label if any, else `#panic.any` |
| Introspection | `panicReason()` |

Unhandled → `flvm: unhandled PANIC: jump.block.reached.max`.

## Interaction with `vm.jump_limit`

`jump:blockMax` is **per target block** from source. **`vm.jump_limit`** still counts every `OP_JUMP` in the run (default 1000). Use `flvm --no-jump-limit` or `--max-jumps N` for long tests. Both limits may apply in the same run.

## Limits (by design)

A cycle across several blocks (`#a` → `#b` → `#a`) is not detected by per-target limits alone; each block may stay under its cap while the cycle continues.

## Binary

`.fb` format **v11** stores per-block limits in a **JBMX** section. Older `.fb` files behave as unlimited for `jump:blockMax`.
