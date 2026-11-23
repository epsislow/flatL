# Native module `path`

**Pure string** path helpers (no disk I/O). **Optional** — declare before use:

```text
use native path
```

The **effective separator** for join/normalize output is **not** taken from `path.*` — use core **`file.sep()`** (always available). Override with `flvm --path-sep /` or `\` / FlEditor **Settings → Path**. See [file.md](./file.md).

Parsing accepts **both** `/` and `\` as separators; emitted paths use the effective sep.

Recoverable failures → **PANIC** **`path.<fn>…`** unless **`?`** / **`~`** ([SPECIFICATION.md](../SPECIFICATION.md) §1.2.2).

## API

| Call | Returns | Panic (no modifier) |
|------|---------|---------------------|
| `path.join(a, b, …)` | STRING | **`path.join.args`** (&lt;2 args / non-STRING); **`path.join.empty`** on `""` segment |
| `path.base(p)` | STRING | **`path.base.empty`** / **`path.base.args`** |
| `path.dir(p)` | STRING | **`path.dir.empty`** / **`path.dir.args`** |
| `path.ext(p)` | STRING (no leading `.`) | **`path.ext.empty`** / **`path.ext.args`**; no ext → `""` |
| `path.normalize(p)` | STRING | **`path.normalize.empty`** / **`path.normalize.args`** |
| `path.isAbs(p)` | BOOL | **`path.isAbs.empty`** / **`path.isAbs.args`** |

Build toggle: `make … path=0`.

---

## `path.join`

Joins ≥2 STRING segments with the effective separator. If a later segment is absolute (or has a drive), earlier parts are discarded (same idea as Node `path.join`).

```text
use native path

#main
  io.outln(path.join('src', 'lib', 'mod.fl'))
  io.outln(path.join('/root', 'x'))
  io.outln(path.join('a', '/abs', 'b'))
  OK
```

With `--path-sep /`: `src/lib/mod.fl`, `/root/x`, `/abs/b`.

```bash
bin/flvm --path-sep / out.fb
```

---

## `path.base`

Last path segment (basename). Root-only (`/` or `\`) → `""`.

```text
use native path

#main
  io.outln(path.base('/a/b/c.fl'))
  io.outln(path.base('/'))
  OK
```

---

## `path.dir`

Parent directory. Single relative segment → `"."`. Root → `"/"` (or `\`).

```text
use native path

#main
  io.outln(path.dir('/a/b/c.fl'))
  io.outln(path.dir('only'))
  OK
```

---

## `path.ext`

Extension **without** the leading dot. No extension, trailing `.`, or names like `.hidden` → `""`.

```text
use native path

#main
  io.outln(path.ext('a/b/c.fl'))
  io.outln(path.ext('a.tar.gz'))
  io.outln(path.ext('readme'))
  OK
```

→ `fl`, `gz`, empty line.

---

## `path.normalize`

Collapses `.` and empty segments; applies `..` (does not climb above an absolute root). Mixed `/` and `\` are rewritten with the effective sep. Empty string → **`path.normalize.empty`**. No I/O / no symlinks.

```text
use native path

#main
  io.outln(path.normalize('a/./b/../c'))
  io.outln(path.normalize('/../x'))
  io.outln(path.normalize('a\\b/c'))
  OK
```

With `--path-sep /`: `a/c`, `/x`, `a/b/c`.

---

## `path.isAbs`

`true` if the path starts with a separator, or a Windows drive + separator (`C:\…` / `C:/…`).

```text
use native path

#main
  io.outln(path.isAbs('/a'))
  io.outln(path.isAbs('a/b'))
  OK
```

---

## Separator (related)

```text
#main
  io.outln(file.sep())
  OK
```

```bash
bin/flvm --path-sep '\\' out.fb
```

FlEditor: **Settings → Path** (Auto / `/` / `\`). Same override feeds future **`fs`**.

## Related

- [file.md](./file.md) — `file.sep()`, file handles  
- [fs.md](./fs.md) — directory / existence ops on disk or MEMFS  
- Implementation: `src/lib/mod_path.c`, `src/common/flat_path.c`
