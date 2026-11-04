# Native module `file`

**File handles** and byte/line I/O. **Core** — always linked into `flvm` / `flatl` (like **`io`** / **`mem`**). Handles are opaque values (printed as `<file>`); they are not exposed as numbers. Stream I/O: [io.md](./io.md).

Paths are STRING (inline or REF). Tests often use **`cd {tmp}`** in `case.toml` so relative paths resolve next to the compiled `.fb`.

Recoverable failures use **PANIC** with reason **`file.<fn>`** (or **`io.eof`** on EOF reads) unless the line has **`?`** / **`~`** ([SPECIFICATION.md](../SPECIFICATION.md) §1.2.2).

## Opening

| Call | Mode | Panic (no modifier) |
|------|------|---------------------|
| `file.readCtx(path)` | Read existing file | **`file.readCtx`** — missing path, open error |
| `file.createCtx(path)` | Create / truncate (`w+`) | **`file.createCtx`** |
| `file.writeCtx(path)` | Read/write existing, seek to 0 | **`file.writeCtx`** |
| `file.appendCtx(path)` | Read/write, seek to EOF | **`file.appendCtx`** |
| `file.readWriteCtx(path)` | Read/write existing | **`file.readWriteCtx`** |

Returns a **FILE** handle bound to a variable or `->` target.

## Handle operations

| Call | Result | Panic (no modifier) |
|------|--------|---------------------|
| `file.readln(fp)` | One line (strip `\n`) | **`file.readln`** / **`io.eof`** at EOF |
| `file.writeln(fp, data)` | Write **data** + `\n` | **`file.writeln`** — bad handle / write error |
| `file.read(fp)` | Rest of file as STRING REF | **`file.read`** |
| `file.read(fp, n)` | Up to **n** bytes as STRING REF | **`file.read`** |
| `file.write(fp, data)` | Write at current position | **`file.write`** |
| `file.writeBytes(fp, data)` | Raw STRING bytes | **`file.writeBytes`** |
| `file.flush(fp)` | Flush stdio buffer | **`file.flush`** |
| `file.pos(fp)` | Current byte offset (NUMBER) | **`file.pos`** — invalid handle |
| `file.size(fp)` | File size in bytes (NUMBER) | **`file.size`** |
| `file.setpos(fp, n)` | Absolute seek | **`file.setpos`** |
| `file.iseof(fp)` | BOOL after reads | **`file.iseof`** |
| `file.close(fp)` | Close handle; revert active **`io.ctx*`** if needed | **`file.close`** |

All open handles are closed on VM exit.

## With `io.ctx*`

See [IO_CONTEXTS.md](../IO_CONTEXTS.md).

```text
fp = file.readCtx('input.txt')
io.ctxin(fp)
io.inln() -> line
io.ctxin(tin)
file.close(fp)
```

```text
fp = file.createCtx('out.txt')
io.ctxout(fp)
io.outln('captured in file')
io.ctxout(tout)
file.close(fp)
```

## Related

- [IO_CONTEXTS.md](../IO_CONTEXTS.md) — `io.ctx*` redirect rules  
- [mem.md](./mem.md) — `mem.ref_empty()` for in-memory capture  
- Implementation: `src/lib/mod_file.c`, `src/common/flat_file.c`
