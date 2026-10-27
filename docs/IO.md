# flatL — I/O (`io.*`, `str.to_*`)

Normative behavior for program **stdout/stdin** and host **stderr** debug.  
Implementation status: **v0.3** (`OP_IO` in VM; future `src/lib/io` registry).

See also: [LANGUAGE_TYPES.md](./LANGUAGE_TYPES.md) (types), [TOOLCHAIN.md](./TOOLCHAIN.md) (`flvm --trace`), plan [.cursor/plans/debug-io.plan.md](../.cursor/plans/debug-io.plan.md).

---

## 1. Streams

| Stream | Used for |
|--------|-----------|
| **stdout** | `io.out*`, prompts before `io.in*` |
| **stderr** | `flvm --trace`, load/VM errors, **unhandled** PANIC only |
| **stdin** | `io.in*` (blocking) |

Recovered PANIC (handler `#panic.*` / `#panic.any`) does **not** log to stderr by default.

---

## 2. Output (`io.out*`)

| Call | Behavior |
|------|----------|
| `io.out(expr)` | Print value, no newline |
| `io.outln(expr)` | Print value + `\n` |
| `io.outvar(name)` | Print variable + `\n` |

**Formatting (`flat_value_fprint_io`):**

| Type | Display |
|------|---------|
| STRING | Raw bytes (no surrounding quotes) |
| NUMBER | `%g` style |
| BOOL | `true` / `false` |
| LIST / OBJECT | v0.3: debug shape; **planned:** LIST simple comma line, OBJECT `json.stringify` — [IO_COLLECTIONS.md §6](./IO_COLLECTIONS.md#6-output) |

Examples:

```text
io.outln('hello')
io.outln(42)
io.outvar(x)
```

---

## 3. Input (`io.in*`)

| Call | Reads | Returns |
|------|--------|---------|
| `io.in()` | One character | STRING (len 1) |
| `io.inln()` | One line (no `\n` in value) | STRING |
| `io.in_num()` | One line + parse | NUMBER |
| `io.in_bool()` | One line + parse | BOOL |

**Optional prompt:** one STRING argument — written to **stdout** before read; omitted = no prompt.

```text
io.inln('Name:') -> name
io.in_num('Price:') -> price
```

---

## 4. Binding results (`->` and `=`)

Preferred:

```text
io.inln() -> line
str.to_num(raw) -> n
```

Equivalent:

```text
line = io.inln()
n = str.to_num(raw)
```

Bytecode stores optional bind name after the call text in `OP_IO` payload.

---

## 5. Conversions (`str.to_*`)

| Call | Input | Output |
|------|--------|--------|
| `str.to_num(s)` | STRING | NUMBER |
| `str.to_bool(s)` | STRING | BOOL |

No stdin. Invalid parse → assign/`->` **fails silently** (variable unchanged); no PANIC.

---

## 6. BOOL tokens (no truthy)

Single token after trim, ASCII case-insensitive:

| true | false |
|------|-------|
| `true`, `t`, `yes`, `y`, `on`, `1` | `false`, `f`, `no`, `n`, `off`, `0` |

---

## 7. Cancel / EOF (`io.in*` only)

Not mapped to `false`, `0`, or `''`. Triggers **PANIC** → `#panic.any` if present.

| Signal | Applies to |
|--------|------------|
| Empty line (Enter) | `io.inln`, `io.in_num`, `io.in_bool` |
| EOF (e.g. Ctrl+D) | All `io.in*` |
| ESC (ASCII 27) | `io.in()` |
| Enter (`\n`) as only char | `io.in()` |

**Extra cancel tokens (only `io.in_bool`):** `cancel`, `c`, `quit`, `q` → PANIC `io.user_cancel`.

**Not cancel:** typing `cancel` into `io.inln` returns STRING `cancel`.

Invalid NUMBER/BOOL line on `io.in_num` / `io.in_bool` → PANIC `io.parse` (when not caught).

---

## 8. Host debug

```bash
bin/flvm --trace program.fb
bin/flvm --trace-vars program.fb
```

With `--trace`, recovered PANIC may log: `[trace] panic reason=… handler=#panic.any`.

Unhandled PANIC: stderr `flvm: unhandled panic: …` and exit **1**.

---

## 9. Tests

Suite **`test/cases/io/`** — 30 pipeline cases (`stdout_contains`, stdin via `printf … | flvm` in `case.toml`).

| Action | Command |
|--------|---------|
| Run I/O tests | `bin/run_tests io` or `make test` |
| Regenerate stubs (after editing catalog) | `make io_generate_tests` |

Catalog source: **`src/io_generate_tests/main.c`**. Output directories are **committed**; most developers never regenerate.

**Portability (Windows, Bash, WSL):** [TOOLCHAIN.md — Portability](./TOOLCHAIN.md#portability-windows-shell-and-io-tests) and [test/README.md](../test/README.md). Summary: **`io_generate_tests` needs no Bash**; **`run_tests`** still uses a POSIX shell for pipeline `run` lines.

---

## 10. Collections & JSON (v0.4)

- **`io.in_list` / `io.in_object` / `io.in_bools`**, **`json.*`** — [IO_COLLECTIONS.md](./IO_COLLECTIONS.md), guide [LIST_OBJECT.md](./LIST_OBJECT.md), tests `test/cases/json/`

## 11. Not in v0.3

- `io.inln_allow_empty`, `str.try_to_*`
- Native module registry (`D12` in plan)
