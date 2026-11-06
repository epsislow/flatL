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

### `io.ctx_stale`

The VM records **which variable name** and **which pool `ref_id`** were passed to `io.ctxout` / `io.ctxerr`. Before each write (or read, for `ctxin`), it checks that the **same name** in the **current active variable table** still holds a **STRING REF** with that **same** `ref_id`.

**PANIC** **`io.ctx_stale`** when that binding is no longer valid, for example:

- The REF variable was **`free`d** or **reassigned** to a *different* ref (or non-REF) while `ctxout` / `ctxerr` still points at it — see `test/cases/io_ctx/010_reassign_ctxout_stale/`.
- **`vcx.set`** switched to another storage where that **name** is missing or the **REF id** does not match — while a capture to global `buf` is still active, writing from scratch without **`V.set(1)`** (or copying `buf` into scratch) is unsafe — see `test/cases/vcx/013_ctxout_stale_wrong_vcx/`.

**Not stale:** after **`V.set`** away and **`V.set(1)`** back to global, the same **`buf`** name again holds the original REF; capture can continue — see `test/cases/vcx/011_io_ctxout_after_set/` and `test/cases/vcx/012_ctxerr_after_vcx_set_ok/`.

On panic, the I/O context resets to terminals before the `#panic.any` handler runs (if present).

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
| VCX + `io.ctx*` | `test/cases/vcx/` (e.g. `011_*`, `012_*`, `013_*`) |
| File + `io.ctx*` | `test/cases/file/` |
| `mem.ref_*` | `test/cases/memory/006_mem_ref_empty/` |

## 5. Not planned (v0.5)

- HTTP / network streams as `io.ctx*` targets
