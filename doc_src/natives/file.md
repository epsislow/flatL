# Native module `file`

**File handles** and byte/line I/O. **Core** — always linked into `flvm` / `flatl` (like **`io`** / **`mem`**). An open file is a **NUMBER** resource id (the slot index for that open file, same idea as a **VCX** context id from **`vcx.create`**). You may bind **`fp:u32`** or another numeric type; invalid or closed ids panic on use. Stream I/O: [io.md](./io.md).

**Availability:** native ✅ · WASM **partial** — FlEditor paths resolve in project **MEMFS** (`/fleditor`), not the host OS disk. Matrix: [README.md § WASM](./README.md#wasm-availability).

Paths are STRING (inline or REF). Tests often use **`cd {tmp}`** in `case.toml` so relative paths resolve next to the compiled `.fb`.

Recoverable failures use **PANIC** with reason **`file.<fn>.…`** (or **`io.eof`** / **`pool.full`**) unless the line has **`?`** / **`~`** ([SPECIFICATION.md](../SPECIFICATION.md) §1.2.2).

Common suffixes: **`.path`** (path not STRING), **`.open`** (host open failed), **`.id`** (bad/closed handle), **`.args`** (arity/type), **`.range`** (negative / non-integer length), **`.io`** (stdio seek/write/flush), **`.memory`** (alloc), **`.busy`** (CSV still holds the handle).

## Opening

| Call | Mode | Panic (no modifier) |
|------|------|---------------------|
| `file.readCtx(path)` | Read existing file | **`file.readCtx.path`** / **`file.readCtx.open`** |
| `file.createCtx(path)` | Create / truncate (`w+`) | **`file.createCtx.path`** / **`file.createCtx.open`** |
| `file.writeCtx(path)` | Read/write existing, seek to 0 | **`file.writeCtx.path`** / **`file.writeCtx.open`** |
| `file.appendCtx(path)` | Read/write, seek to EOF | **`file.appendCtx.path`** / **`file.appendCtx.open`** |
| `file.readWriteCtx(path)` | Read/write existing | **`file.readWriteCtx.path`** / **`file.readWriteCtx.open`** |

Returns a **NUMBER** file id bound to a variable or `->` target.

## Handle operations

| Call | Result | Panic (no modifier) |
|------|--------|---------------------|
| `file.readln(fp)` | One line (strip `\n`) | **`file.readln.id`** / **`io.eof`** at EOF |
| `file.writeln(fp, data)` | Write **data** + `\n` | **`file.writeln.id`** / **`.args`** / **`.io`** |
| `file.read(fp)` | Rest of file as STRING REF | **`file.read.id`** / **`.io`** / **`pool.full`** |
| `file.read(fp, n)` | Up to **n** bytes as STRING REF | **`file.read.args`** / **`.range`** / **`.id`** / **`pool.full`** |
| `file.write(fp, data)` | Write at current position | **`file.write.id`** / **`.args`** / **`.io`** |
| `file.writeBytes(fp, data)` | Raw STRING bytes | **`file.writeBytes.id`** / **`.args`** (non-STRING) / **`.io`** |
| `file.flush(fp)` | Flush stdio buffer | **`file.flush.id`** / **`.io`** |
| `file.pos(fp)` | Current byte offset (NUMBER) | **`file.pos.id`** / **`.io`** |
| `file.size(fp)` | File size in bytes (NUMBER) | **`file.size.id`** / **`.io`** |
| `file.setpos(fp, n)` | Seek: **`n >= 0`** absolute from start; **`n < 0`** from EOF (e.g. `-3` = 3 bytes before end) | **`file.setpos.id`** / **`.args`** / **`.io`** |
| `file.setposrel(fp, delta)` | Seek relative to current position (`SEEK_CUR`) | **`file.setposrel.id`** / **`.args`** / **`.io`** |
| `file.truncate(fp, len)` | Set file size to **`len`** bytes (non-negative whole byte count); read-only handle invalid | **`file.truncate.id`** / **`.args`** / **`.range`** / **`.io`** |
| `file.iseof(fp)` | BOOL after reads | **`file.iseof.id`** |
| `file.close(fp)` | Close handle; revert active **`io.ctx*`** if needed | **`file.close.id`** / **`file.close.busy`** / **`.io`** |

### Seeking

- **`file.setpos(fp, 0)`** — start of file (not EOF).
- **`file.setpos(fp, n)`** with **`n >= 0`** — byte offset from the beginning.
- **`file.setpos(fp, n)`** with **`n < 0`** — offset from end of file (same idea as `SEEK_END`; `-1` is the last byte).
- **`file.setposrel(fp, delta)`** — move the cursor by **delta** bytes forward (positive) or backward (negative) without changing the meaning of **`setpos`**.

### Truncating

- **`file.truncate(fp, len)`** — host **`ftruncate`** / size set: file becomes exactly **`len`** bytes. Shorter than the previous size drops the tail without rewriting from the start (unlike **`file.writeCtx`** + **`file.write`**, which only overwrites from the current cursor). **`len == 0`** empties the file; if the cursor was past the new size it is moved to **`len`**.
- **`file.createCtx(path)`** still truncates on **open** (`w+`). **`io.outcls()`** with **`io.ctxout(fp)`** also truncates to **0** on the active output sink ([IO_CONTEXTS.md](../IO_CONTEXTS.md)).

Binary reads/writes use the current position; combine **`setpos`** / **`setposrel`** with **`file.read(fp, n)`** and **`file.writeBytes`** for opaque blobs.

All open handles are closed on VM exit.

## Path separator

| Call | Result |
|------|--------|
| `file.sep()` | Effective path separator STRING: `"/"` or `"\\"` |

**Core** (no `use native`). Same runtime value used by optional [`path`](./path.md) and future `fs`. Defaults: `/` on POSIX and WASM; `\` on native Windows. Override: `flvm --path-sep /` or `--path-sep \`, or FlEditor **Settings → Path**.

```text
#main
  io.outln(file.sep())
  OK
```

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
- [path.md](./path.md) — optional string path helpers; share `file.sep`  
- [fs.md](./fs.md) — exists / mkdir / listDir / remove  
- Implementation: `src/lib/mod_file.c`, `src/common/flat_file.c`, `src/common/flat_path.c`
