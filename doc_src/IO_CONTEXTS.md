# flatL — I/O contexts (`io.ctx*`)

Redirect **`io.in*`**, **`io.out*`**, **`io.outvar`**, and **`io.err*`** to strings, REF buffers, files, or back to the terminal. Defaults at VM start: **`tin`** / **`tout`** / **`terr`**.

See also: [I/O (streams & rules)](./IO.md), [Native io](./natives/io.md), [Native file](./natives/file.md), [Native mem](./natives/mem.md) (`ref_empty` for capture buffers).

Implementation: **`src/lib/mod_io.c`**, **`mod_file.c`**; tests **`test/cases/io_ctx/`**, **`test/cases/file/`**.

---

## 1. Calls

Three **independent** redirects:

| Call | Selects source/sink for |
|------|-------------------------|
| `io.ctxin(source)` | `io.in*` |
| `io.ctxout(destination)` | `io.out*` / `io.outvar` |
| `io.ctxerr(destination)` | `io.err*` / `io.errln` |

| Token | Role |
|-------|------|
| **`tin`** | Host stdin (WASM: editor input) |
| **`tout`** | Host stdout |
| **`terr`** | Host stderr (program errors) |

## 2. Targets

| Target | `ctxin` | `ctxout` / `ctxerr` |
|--------|---------|---------------------|
| String literal | one line per `io.inln` | invalid on `ctxin` only |
| Inline STRING var | snapshot at `ctxin()` | snapshot buffer (≤127 B append) |
| STRING REF var | read from ref | **append** (`io.out*` text) |
| **`file`** handle | `file.readln` / byte read via `io.inln` | write formatted bytes to file |
| **`tin` / `tout` / `terr`** | reset to terminal | reset to terminal |

Invalid pairing (e.g. `io.ctxin(tout)`) → **PANIC** `io.context`.  
If a **REF** variable bound to `ctxout` / `ctxerr` is **`free`d** or reassigned while active → **PANIC** `io.ctx_stale` (context resets before handler runs).

## 3. Examples

Capture to REF (prefer **`mem.ref_empty()`** over test-only **`__ref__`**):

```text
buf = mem.ref_empty()
io.ctxout(buf)
io.out('Hello ')
io.out('flatL')
io.ctxout(tout)
io.outln(buf)
```

File redirect — see [natives/file.md](./natives/file.md):

```text
fp = file.readCtx('input.txt')
io.ctxin(fp)
io.inln() -> line
io.ctxin(tin)
file.close(fp)
```

## 4. Tests

| Suite | Path |
|-------|------|
| I/O contexts | `test/cases/io_ctx/` |
| File + `io.ctx*` | `test/cases/file/` |
| `mem.ref_*` | `test/cases/memory/006_mem_ref_empty/` |

## 5. Not planned (v0.5)

- HTTP / network streams as `io.ctx*` targets
