# flatL — I/O (`io.*`)

Normative behavior for program **stdout/stdin**, **`io.in*`**, and host **stderr** debug.  
Implementation status: **v0.5** — natives in **`src/lib/`** registry. Bytecode **`OP_NATIVE`** (id + args). Legacy **`OP_IO`** still decoded for old `.fb`.

**Native API (all `io.*` calls):** [natives/io.md](./natives/io.md).  
**I/O contexts** (`io.ctxin` / `io.ctxout` / `io.ctxerr`): [IO_CONTEXTS.md](./IO_CONTEXTS.md).  
**String parsing** (`str.to_num`, `str.to_bool`, …): [natives/str.md](./natives/str.md).  
**JSON & collection I/O:** [natives/json.md](./natives/json.md), [IO_COLLECTIONS.md](./IO_COLLECTIONS.md).  
**File handles:** [natives/file.md](./natives/file.md).

See also: [LANGUAGE_TYPES.md](./LANGUAGE_TYPES.md) (types), [TOOLCHAIN.md](./TOOLCHAIN.md) (`flvm --trace`), plan [.cursor/plans/debug-io.plan.md](../.cursor/plans/debug-io.plan.md).

---

## 1. Streams

| Stream | Used for |
|--------|-----------|
| **stdout** | `io.out*`, prompts before `io.in*` |
| **stderr** | `flvm --trace`, load/VM errors, **unhandled** PANIC only |
| **stdin** | `io.in*` (blocking) |

Recovered PANIC (handler `#panic.*` / `#panic.any`) does **not** log to stderr by default.

**Program stderr (logical):** `io.err` / `io.errln` write to **`ctxerr`** (default **`terr`** = host stderr). Redirects: [IO_CONTEXTS.md](./IO_CONTEXTS.md). This is separate from **`flvm --trace`** on host stderr.

---

## 2. Output (`io.out*`)

| Call | Behavior |
|------|----------|
| `io.out(expr)` | Print value, no newline |
| `io.outln(expr)` | Print value + `\n` |
| `io.outvar(name)` | Print variable + `\n` |
| `io.err(expr)` | Print on **ctxerr** (no `\n`) |
| `io.errln(expr)` | Print on **ctxerr** + `\n` |

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

### 2.1 Introspection (`io.docs`)

`io.docs(query)` prints API hints to **stdout** (same stream as `io.outln`). Signatures are **documentation only** — the VM does not type-check calls.

| Query shape | Result |
|-------------|--------|
| Starts with `#` | Block labels in the **linked** program whose full name starts with the prefix (e.g. `io.docs('#H')` after `use "lib/x.fl" as #H_`) |
| Module or alias token (`mem`, `O`, …) | In the **same `.fl` file** as the `io.docs` call: list/signatures for that module or **local alias** (shared alias `kit` on several modules → union **in that file only**) |
| Qualified prefix (`mem.var`, `str.l`) | Functions whose qualified name starts with the prefix |
| Exact qualified name (`mem.vars_max`, `O.get`) | One signature line (e.g. `mem.vars_max()`) |

**Listing rules:**

- Sorted lexicographically.
- At most **25** lines; if more match, line `....` then a footer **`(N blocks)`** or **`(N functions)`** with the **total** count (footer is always printed, even when `N ≤ 25`).
- No matches: `no blocks` or `no functions`; undeclared optional module: `unknown module`.

**Core** modules are always listable by canonical name. **Optional** modules appear when declared in **that file's** header. An alias in another linked file is **`unknown module`** here. See [natives/README.md](./natives/README.md).

Examples:

```text
io.docs('#main')
io.docs('#or')
io.docs('mem')
io.docs('mem.vars_max')
use native obj with O
io.docs('O')
io.docs('obj')
```

Pass the query as a **string literal** (recommended): `io.docs('#H')`, `io.docs('mem')`.

---

## 3. Input (`io.in*`)

| Call | Reads | Returns |
|------|--------|---------|
| `io.in(prompt?)` | One character | STRING (len 1) |
| `io.inln(prompt?)` | One line (no `\n` in value) | STRING |
| `io.in_num(prompt?)` | One line + parse | NUMBER |
| `io.in_bool(prompt?)` | One line + parse | BOOL |

**Optional prompt:** one STRING argument — written to **stdout** before read; omitted = no prompt.

```text
io.inln('Name:') -> name
io.in_num('Price:') -> price
```

For parsing rules shared with **`str.to_num`** / **`str.to_bool`**, see [natives/str.md](./natives/str.md).

---

## 4. Binding results (`->` and `=`)

Preferred:

```text
io.inln() -> line
```

Equivalent:

```text
line = io.inln()
```

Parse STRING to NUMBER/BOOL with **`str.to_num`** / **`str.to_bool`** ([natives/str.md](./natives/str.md)):

```text
raw = io.inln()
str.to_num(raw) -> n
```

**`OP_NATIVE` payload:** `uint16` native id, then argument string (paren contents) `\0`, then optional bind name `\0`.  
Registry: `include/flat_native.h`, table in `src/lib/native_registry.c`, handlers in `mod_io.c` / `mod_str.c` / `mod_json.c`.

---

## 5. Cancel / EOF (`io.in*` only)

Not mapped to `false`, `0`, or `''`. Triggers **PANIC** → `#panic.any` if present.

| Signal | Applies to |
|--------|------------|
| Empty line (Enter) | `io.inln`, `io.in_num`, `io.in_bool` |
| EOF (e.g. Ctrl+D) | All `io.in*` |
| ESC (ASCII 27) | `io.in()` |
| Enter (`\n`) as only char | `io.in()` |

**Extra cancel tokens (only `io.in_bool`):** `cancel`, `c`, `quit`, `q` → PANIC `io.user_cancel`.

**Not cancel:** typing `cancel` into `io.inln` returns STRING `cancel`.

Invalid NUMBER/BOOL line on `io.in_num` / `io.in_bool` → PANIC `io.parse` (when not caught). BOOL token rules: [natives/str.md](./natives/str.md#bool-tokens-for-to_bool--ioin_bool).

---

## 6. Host debug

```bash
bin/flvm --trace program.fb
bin/flvm --trace-vars program.fb
```

With `--trace`, recovered PANIC may log: `[trace] panic reason=… handler=#panic.any`.

Unhandled PANIC: stderr `flvm: unhandled panic: …` and exit **1**.

---

## 7. Tests

Suite **`test/cases/io/`** — 30 pipeline cases (`stdout_contains`, stdin via `printf … | flvm` in `case.toml`).

| Action | Command |
|--------|---------|
| Run I/O tests | `bin/run_tests io` or `make test` |
| Regenerate stubs (after editing catalog) | `make io_generate_tests` |

Catalog source: **`src/io_generate_tests/main.c`**. Output directories are **committed**; most developers never regenerate.

**Portability (Windows, Bash, WSL):** [TOOLCHAIN.md — Portability](./TOOLCHAIN.md#portability-windows-shell-and-io-tests) and [test/README.md](../test/README.md). Summary: **`io_generate_tests` needs no Bash**; **`run_tests`** still uses a POSIX shell for pipeline `run` lines.

---

## 8. Collections & JSON (v0.4)

- **`io.in_list` / `io.in_object` / `io.in_bools`**, **`json.*`** — [IO_COLLECTIONS.md](./IO_COLLECTIONS.md), guide [LIST_OBJECT.md](./LIST_OBJECT.md), tests `test/cases/json/`

## 9. Not planned (v0.5)

- `io.inln_allow_empty`, `str.try_to_*`
