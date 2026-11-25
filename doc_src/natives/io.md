# Native module `io`

**Stdout/stdin**, optional **stderr** output, line/char input, and stream **redirects**. **Core** — always linked into `flvm` / `flatl`; call `io.*` directly (no `use native io`).

Normative rules (formatting, PANIC on cancel/EOF, `->` binding, host **`flvm --trace`**): [IO.md](../IO.md).  
**Redirects** (`io.ctx*`): [IO_CONTEXTS.md](../IO_CONTEXTS.md).  
**Collection reads** (`io.in_list` / …): [IO_COLLECTIONS.md](../IO_COLLECTIONS.md).  
**Line modifiers** (`?` / `~`): [SPECIFICATION.md](../SPECIFICATION.md) §1.2.2.

Implementation: `src/lib/mod_io.c`, registry `src/lib/native_registry.c`.

## Output (stdout / ctxout)

| Call | Behavior | Panic (no modifier) |
|------|----------|---------------------|
| `io.out(expr)` | Print value, no newline | **`io.out.args`** — bad expr; context → **`io.context`** / **`io.ctx_stale`** |
| `io.outln(expr?)` | Print value + `\n`; **`io.outln()`** writes only `\n` | **`io.outln.args`** (bad expr) / context reasons |
| `io.outvar(name)` | Print variable value + `\n` | **`io.outvar.args`** — unknown variable |
| `io.outcls()` | Clear **`ctxout`** sink (see below) | **`io.outcls`**, **`io.context`**, **`io.ctx_stale`** |

**`io.outcls()`** — no arguments. Effect depends on the active **`ctxout`** target ([IO_CONTEXTS.md](../IO_CONTEXTS.md)): **`tout`** / terminal → ANSI clear screen; **STRING REF** → empty body (same `ref_id`); **inline** STRING snapshot → empty; **`file`** handle → truncate to 0 bytes at position 0. Invalid / unsupported target → **`io.context`**. Default **`tout`** when no redirect matches direct `io.out*`.

**Availability:** native ✅ · WASM **partial** (stdout/stderr → xterm; `ctx*` redirects OK). Matrix: [README.md § WASM](./README.md#wasm-availability).

**Fleditor / WASM:** terminal clear uses the same ANSI bytes; stdout in the editor is the xterm pane (see [FLEDITOR.md](../FLEDITOR.md)). Non-terminal sinks are unchanged in WASM builds.

Formatting: [IO.md §2](../IO.md#2-output-ioout).

## Program stderr (ctxerr)

| Call | Behavior | Panic (no modifier) |
|------|----------|---------------------|
| `io.err(expr)` | Print on **ctxerr** (default **terr**), no `\n` | **`io.err.args`** / context reasons |
| `io.errln(expr)` | Print on **ctxerr** + `\n` | **`io.errln.args`** / context reasons |

Separate from host **`flvm --trace`** on stderr. See [IO.md §1](../IO.md#1-streams).

## Input (stdin / ctxin)

Optional **prompt**: one STRING argument written to **stdout** before the read.

| Call | Reads | Returns | Panic (no modifier) |
|------|--------|---------|---------------------|
| `io.in(prompt?)` | One character | STRING (len 1) | **`io.in.args`** (bad prompt); cancel → **`io.user_cancel`**; EOF → **`io.eof`**; context → **`io.context`** |
| `io.inln(prompt?)` | One line (no `\n` in value) | STRING | **`io.inln.args`** / cancel/EOF/context; long line → **`pool.full`** |
| `io.in_num(prompt?)` | One line + parse | NUMBER | **`io.in_num.args`** / cancel/EOF/context, or **`io.parse`** |
| `io.in_bool(prompt?)` | One line + parse | BOOL | **`io.in_bool.args`** / cancel/EOF/context, or **`io.parse`** |
| `io.in_list(prompt?)` | One line + parse | LIST | **`io.in_list.args`** / cancel/EOF/context, or **`io.parse`** |
| `io.in_object(prompt?)` | One line + parse | OBJECT | **`io.in_object.args`** / cancel/EOF/context, or **`io.parse`** |
| `io.in_bools(prompt?)` | One line + parse | LIST of BOOL | **`io.in_bools.args`** / cancel/EOF/context, or **`io.parse`** |

Parsing for NUMBER/BOOL: [str.md](./str.md). JSON-shaped lines: [json.md](./json.md), [IO_COLLECTIONS.md](../IO_COLLECTIONS.md).

Cancel / EOF / parse: [IO.md §5](../IO.md#5-cancel--eof-ioin-only). Prefix the line with **`?`** to continue after panic-class outcomes; **`~`** routes to **`#or.*`** instead.

```text
io.inln('Name:') -> name
io.in_num('Price:') -> price
io.in_list('Items:') -> xs
```

## Stream redirects

| Call | Affects | Panic (no modifier) |
|------|---------|---------------------|
| `io.ctxin(source)` | All `io.in*` | **`io.ctxin`** — invalid source, stale ref, **`io.context`** |
| `io.ctxout(destination)` | `io.out*` / `io.outvar` | **`io.ctxout`** — invalid destination / stale ref |
| `io.ctxerr(destination)` | `io.err*` | **`io.ctxerr`** — invalid destination / stale ref |

Tokens **`tin`** / **`tout`** / **`terr`** reset to the terminal. Details: [IO_CONTEXTS.md](../IO_CONTEXTS.md).

## Introspection

| Call | Behavior | Panic (no modifier) |
|------|----------|---------------------|
| `io.docs(query)` | Print API hints to **stdout** (signatures only; no type-check) | **`io.docs.args`** / **`io.docs`** |

Query shapes and listing limits: [IO.md §2.1](../IO.md#21-introspection-iodocs).

## Related

- [file.md](./file.md) — handles as `ctxin` / `ctxout` targets  
- [mem.md](./mem.md) — `mem.ref_empty()` for in-memory capture  
- Tests: `test/cases/io/`, `test/cases/io_ctx/`
