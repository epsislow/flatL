# flatL — portability (C11, POSIX, MSVC)

Normative for contributions. Work plan (may be bilingual): [`.cursor/plans/portability.plan.md`](../.cursor/plans/portability.plan.md).

## 1. Layers

```
  .fl sources
       ↓
  flc / flvm / value_* / lib/*     ← C11 only (no OS headers)
       ↓
  flat_str.*                       ← tokenization (pure C)
       ↓
  flat_port.*                      ← files, temp, shell, cwd
       ↓
  OS (Linux, WSL, Termux, Windows)
```

## 2. Rules

| Rule | Detail |
|------|--------|
| **No OS in VM** | `vm.c`, `parse.c`, `value_*.c`, `mod_*.c` must not include `unistd.h`, `dirent.h`, `windows.h`. |
| **Single OS gate** | Temp dirs, `access`, `popen`, directory listing → **`flat_port_*` only**. |
| **No strtok** | Use **`flat_str_trim`**, **`flat_str_next`**. Split at one delimiter; remainder stays in the buffer (same as “first `,`, then rest” for `__list_push__`). |
| **Local `#ifdef`** | Only in `src/port/` (and small tools if needed). |

## 3. `flat_str` behavior

- **`flat_str_trim(s)`** — trims ASCII whitespace **in-place**; returns `s`.
- **`flat_str_next(cursor, delim)`** — `cursor` is `char **` into a **mutable** buffer; returns the current token or `NULL`; after the call, `*cursor` points past the delimiter (or at `\0`).

Example (equivalent to old `strtok` on `a,b,c`):

```c
char buf[] = "a,b,c";
char *p = buf;
char *t1 = flat_str_next(&p, ',');  /* "a", p → "b,c" */
char *t2 = flat_str_next(&p, ',');  /* "b", p → "c" */
```

For **`__list_push__(var, expr)`**: one `flat_str_next` for the variable name; the expression is **`flat_str_trim(p)`** (remainder of the line).

## 4. `flat_port` API (summary)

| Function | Role |
|----------|------|
| `flat_port_path_exists` | File or directory exists |
| `flat_port_dir_writable` | Existing directory is writable |
| `flat_port_getcwd` | Current working directory |
| `flat_port_path_is_root` | `/` or Windows drive root |
| `flat_port_mkdir` | Create directory (`mode` on POSIX) |
| `flat_port_temp_parent` | Writable parent for temp dirs (no unique subdir) |
| `flat_port_make_temp_dir` | Unique directory for pipeline tests |
| `flat_port_shell_capture` | Run shell command; capture combined stdout+stderr; exit code |
| `flat_port_scan_dir` | Callback per entry (skips `.` / `..`) |

**Temp dir order (`run_tests` pipelines):** `TMPDIR`, `TMP`, `TEMP`, `$PREFIX/tmp` (Termux), `$HOME/tmp`, `/tmp`, then `{repo}/.flatl_run_tmp`.

## 5. Target compilers

| Environment | Notes |
|-------------|--------|
| **GCC / Clang (WSL, Linux, Termux)** | `flat_port_posix.c`, `-std=c11 -Wall -Wextra` |
| **MSVC** | `flat_port_win.c`; link `flc`/`flvm` without POSIX; `run_tests` uses `_popen` |

## 6. Tests

- Full suite: `make test`.
- **`test/cases/port/001_str_util`**: exercises `flat_str` via `bin/port_check`.

## 7. Shell-dependent cases

**`kind = pipeline`** in `case.toml` uses `run = "printf … | flvm …"`. Requires a **POSIX `sh`** on Unix and **`cmd /c`** on Windows (via `flat_port_shell_capture`).
