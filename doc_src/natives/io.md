# Native module `io`

**Stdout/stdin**, optional **stderr** output, line/char input, and stream **redirects**. **Core** — always linked into `flvm` / `flatl`; call `io.*` directly (no `use native io`).

Normative rules (formatting, PANIC on cancel/EOF, `->` binding, host **`flvm --trace`**): [IO.md](../IO.md).  
**Redirects** (`io.ctx*`): [IO_CONTEXTS.md](../IO_CONTEXTS.md).  
**Collection reads** (`io.in_list` / …): [IO_COLLECTIONS.md](../IO_COLLECTIONS.md).

Implementation: `src/lib/mod_io.c`, registry `src/lib/native_registry.c`.

## Output (stdout / ctxout)

| Call | Behavior |
|------|----------|
| `io.out(expr)` | Print value, no newline |
| `io.outln(expr)` | Print value + `\n` |
| `io.outvar(name)` | Print variable value + `\n` |

Formatting: [IO.md §2](../IO.md#2-output-ioout).

## Program stderr (ctxerr)

| Call | Behavior |
|------|----------|
| `io.err(expr)` | Print on **ctxerr** (default **terr**), no `\n` |
| `io.errln(expr)` | Print on **ctxerr** + `\n` |

Separate from host **`flvm --trace`** on stderr. See [IO.md §1](../IO.md#1-streams).

## Input (stdin / ctxin)

Optional **prompt**: one STRING argument written to **stdout** before the read.

| Call | Reads | Returns |
|------|--------|---------|
| `io.in(prompt?)` | One character | STRING (len 1) |
| `io.inln(prompt?)` | One line (no `\n` in value) | STRING |
| `io.in_num(prompt?)` | One line + parse | NUMBER |
| `io.in_bool(prompt?)` | One line + parse | BOOL |
| `io.in_list(prompt?)` | One line + parse | LIST |
| `io.in_object(prompt?)` | One line + parse | OBJECT |
| `io.in_bools(prompt?)` | One line + parse | LIST of BOOL |

Parsing for NUMBER/BOOL: [str.md](./str.md). JSON-shaped lines: [json.md](./json.md), [IO_COLLECTIONS.md](../IO_COLLECTIONS.md).

Cancel / EOF / `io.parse`: [IO.md §5](../IO.md#5-cancel--eof-ioin-only).

```text
io.inln('Name:') -> name
io.in_num('Price:') -> price
io.in_list('Items:') -> xs
```

## Stream redirects

| Call | Affects |
|------|---------|
| `io.ctxin(source)` | All `io.in*` |
| `io.ctxout(destination)` | `io.out*` / `io.outvar` |
| `io.ctxerr(destination)` | `io.err*` |

Tokens **`tin`** / **`tout`** / **`terr`** reset to the terminal. Details: [IO_CONTEXTS.md](../IO_CONTEXTS.md).

## Introspection

| Call | Behavior |
|------|----------|
| `io.docs(query)` | Print API hints to **stdout** (signatures only; no type-check) |

Query shapes and listing limits: [IO.md §2.1](../IO.md#21-introspection-iodocs).

## Related

- [file.md](./file.md) — handles as `ctxin` / `ctxout` targets  
- [mem.md](./mem.md) — `mem.ref_empty()` for in-memory capture  
- Tests: `test/cases/io/`, `test/cases/io_ctx/`
