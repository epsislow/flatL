# Native module `file`

**File handles** and byte/line I/O. **Core** — always linked into `flvm` / `flatl` (like **`io`** / **`mem`**). Handles are opaque values (printed as `<file>`); they are not exposed as numbers.

Paths are STRING (inline or REF). Tests often use **`cd {tmp}`** in `case.toml` so relative paths resolve next to the compiled `.fb`.

## Opening

| Call | Mode |
|------|------|
| `file.readCtx(path)` | Read existing file |
| `file.createCtx(path)` | Create / truncate (`w+`) |
| `file.writeCtx(path)` | Read/write existing, seek to 0 |
| `file.appendCtx(path)` | Read/write, seek to EOF |
| `file.readWriteCtx(path)` | Read/write existing |

Returns a **FILE** handle bound to a variable or `->` target.

## Handle operations

| Call | Result |
|------|--------|
| `file.readln(fp)` | One line (strip `\n`); EOF → PANIC `io.eof` |
| `file.read(fp)` | Rest of file as STRING REF |
| `file.read(fp, n)` | Up to **n** bytes as STRING REF |
| `file.write(fp, data)` | Write formatted value at current position |
| `file.writeBytes(fp, data)` | Write raw STRING bytes (inline or REF); no formatting |
| `file.flush(fp)` | Flush stdio buffer |
| `file.pos(fp)` | Current byte offset (NUMBER) |
| `file.size(fp)` | File size in bytes (NUMBER) |
| `file.setpos(fp, n)` | Absolute seek |
| `file.iseof(fp)` | BOOL after reads |
| `file.close(fp)` | Close handle; if it was an active **`io.ctx*`** target, revert to **tin/tout/terr** |

All open handles are closed on VM exit.

## With `io.ctx*`

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

- [IO.md](../IO.md) §1.1 — context redirect rules  
- [mem.md](./mem.md) — `mem.ref_empty()` for in-memory capture  
- Implementation: `src/lib/mod_file.c`, `src/common/flat_file.c`
