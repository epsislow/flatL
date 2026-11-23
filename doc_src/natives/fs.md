# Native module `fs`

Directory / path **existence** helpers on the host filesystem. **Optional** — declare before use:

```text
use native fs
```

| Host | Backing store |
|------|----------------|
| `flvm` / `flatl` (native) | Real OS filesystem (relative to process cwd) |
| FlEditor / WASM | **MEMFS** project tree (`chdir` → `/fleditor`) — same APIs, no host disk outside the sandbox |

Byte/line I/O stays on core [`file`](./file.md). String path helpers: optional [`path`](./path.md). Separator: [`file.sep`](./file.md).

Recoverable failures → **PANIC** **`fs.<fn>…`** unless **`?`** / **`~`**.

**Non-goals:** recursive delete, watchers, permissions UI, glob, symlink resolve.

Build toggle: `make … fs=0`.

## API

| Call | Returns | Panic (no modifier) |
|------|---------|---------------------|
| `fs.exists(path)` | BOOL | **`fs.exists.empty`** / **`fs.exists.args`** — missing path → `false` |
| `fs.isFile(path)` | BOOL | **`fs.isFile.empty`** / **`fs.isFile.args`** — missing / not file → `false` |
| `fs.isDir(path)` | BOOL | **`fs.isDir.empty`** / **`fs.isDir.args`** — missing / not dir → `false` |
| `fs.mkdir(path)` | (null) | **`fs.mkdir`**, **`fs.mkdir.exists`** if a non-dir already exists; idempotent if already a dir |
| `fs.listDir(path)` | LIST of STRING | **`fs.listDir`** if not a directory; names sorted, no `.` / `..` |
| `fs.remove(path)` | (null) | **`fs.remove.missing`**, **`fs.remove.not_empty`** (non-empty dir), **`fs.remove`** |

`mkdir` creates **one** level (parent must exist). `remove` deletes a **file** or an **empty** directory (no recursive tree wipe).

---

## `fs.exists` / `fs.isFile` / `fs.isDir`

```text
use native fs

#main
  io.outln(fs.exists('hello.txt'))
  io.outln(fs.isFile('hello.txt'))
  io.outln(fs.isDir('subdir'))
  io.outln(fs.exists('missing'))
  OK
```

---

## `fs.mkdir`

```text
use native fs

#main
  fs.mkdir('out')
  fs.mkdir('out')
  io.outln(fs.isDir('out'))
  OK
```

If `out` is already a **file**, panic **`fs.mkdir.exists`**.

---

## `fs.listDir`

```text
use native fs
use native list

#main
  fs.mkdir('d')
  fp = file.createCtx('d/a.txt')
  file.write(fp, 'x')
  file.close(fp)
  xs = fs.listDir('d')
  io.outln(list.get(xs, 0))
  OK
```

---

## `fs.remove`

```text
use native fs

#main
  fp = file.createCtx('tmp.txt')
  file.write(fp, 'z')
  file.close(fp)
  fs.remove('tmp.txt')
  fs.mkdir('empty')
  fs.remove('empty')
  OK
```

Non-empty directory → **`fs.remove.not_empty`**. Missing path → **`fs.remove.missing`**.

---

## With `path`

```text
use native fs
use native path

#main
  fs.mkdir('root')
  p = path.join('root', 'nested')
  fs.mkdir(p)
  io.outln(fs.isDir(p))
  OK
```

`use native path` is only required if you call `path.*`; absolute / already-joined STRING paths work with `fs` alone.

## Related

- [file.md](./file.md) — open/read/write handles; `file.sep()`  
- [path.md](./path.md) — `join` / `base` / `dir` / …  
- Implementation: `src/lib/mod_fs.c`, `src/common/flat_fs.c`
