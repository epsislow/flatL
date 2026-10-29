# flatL — Architecture & Binary Specification (draft)

Documentation index: [README.md](./README.md) (English).

Consolidated from project design notes. Normative details will be refined as `flc` and `flvm` are implemented in the repository.

**Related:** FGIR (Flat Graph Intermediate Representation) v1.0 · `.fb` bytecode & VM v1.1

---

## 1. Flat graph model (FGIR)

Programs are **not** abstract syntax trees. They are a **flat graph** of **basic blocks**:

- Each block is a contiguous, linear sequence of instructions (e.g. `#main`, `#check_balance`).
- Control flow is **explicit**: guards, backtrack labels, panic labels, and `call` targets are resolved at **compile time**.
- **Logical failure** (`FAIL`, failed guards) is separate from **infrastructure panic** (I/O errors, OOM, timeouts, network faults).

### 1.1 Lexical and structural rules

| Construct | Meaning |
|-----------|---------|
| `#name` | Block declaration |
| `#name(arg1, arg2)` | Block with parameter list |
| Indented lines under a block | Sequential execution (implicit AND), top to bottom |
| `call #block(args) -> var` | Invoke another block; bind result to `var` |
| `expr == value` | Guard; on false, branch to backtrack address |
| `OK` / `FAIL` | Terminal success / logical failure |
| `return expr` | Return from block to caller |
| `foreach(coll, item, '#loop')` | Iterate collection via sub-block |
| `-- …` | Full-line comment (from `--` to end of line) |
| `--<` … `-->` | Block comment (multiple lines; see below) |

**Comments (v0.2):**

- After trimming leading/trailing whitespace, a line starting with **`--`** (but not **`--<`**) is a **line comment** and is ignored. Examples: `-- todo: fix`, `-- note`, `-- #not-a-block`.
- A line starting with **`--<`** opens a **block comment**; scanning continues until a line starting with **`-->`**. Blank lines inside the block are ignored. Nested **`--<`** is a parse error; an unclosed block at EOF is a parse error.
- **No trailing comments** on instruction lines — `--` in the middle of a line is part of the instruction text.
- Block comments and line comments may appear before blocks, between blocks, or between instructions.

There is **no programmer-visible `goto`**. Jump targets are derived from block names and **1-based** instruction indices.

### 1.1.1 Language keywords vs native libraries

**Keywords** (compile to VM opcodes — not optional “built-in functions”):

- `#block`, `call`, `return`, `foreach`, `OK`, `FAIL`, `->` (call result binding)
- Guards and assignment lines (`=`, `==`) map to `OP_ASSIGN` / `OP_GUARD`

**Native libraries** (dotted calls such as `io.outln`, `str.to_num`, `json.parse`):

- Implemented as registered C modules under `src/lib/`
- Resolved at compile time to `(module, function)` IDs in bytecode (`OP_NATIVE`)
- No special-case function names in the VM dispatch loop

**Core natives** — always available: **`io`**, **`json`**, **`str`**. No header declaration.

**Optional natives** — e.g. **`math.trig`**. Declare in the header:

```text
use native math.trig with trig
```

Then call `trig.sin(...)` and/or `math.trig.sin(...)`. Undeclared optional calls are a **compile** error. Full rules: [natives/README.md](./natives/README.md).

`use "path.fl" as #Prefix` (source modules) and `use native …` are independent; both may appear in the header.

### 1.2 Backtrack addressing

When the guard at instruction index *n* in block `#target` evaluates to false, the runtime transfers control to:

```text
#backtrack.#target.n
```

If no such block exists, the enclosing flow aborts with logical **FAIL**.

Example:

```text
#main
  uid = form.userId
  amount = form.amount
  call #check_balance(uid, amount) -> has_funds
  has_funds == true
  call #process_payment(uid, amount) -> payment_status
  payment_status == 'SUCCESS'
  end.msg('Order completed')
  OK

#backtrack.#main.4
  end.err('Insufficient funds')
  FAIL

#backtrack.#main.6
  end.err('Payment declined')
  FAIL
```

### 1.3 Panic addressing

On an **infrastructure** fault at index *n* in block `#target`, the runtime routes to:

```text
#panic.#target.n
```

If no handler exists for that index, control falls through to **`#panic.any`** (e.g. transaction rollback via `tstop()`).

Example:

```text
#check_balance(userId, amount)
  key = str.concat('user:wallet:', userId)
  balance = kget(key)
  result = calc(balance >= amount)
  return result

#panic.#check_balance.2
  redis.log('Balance lookup failed')
  return false
```

### 1.4 Iteration

`foreach(collection, key_var, '#loop_body')`:

1. Bind an iterator over `collection`.
2. For each element, jump to `#loop_body` with `key_var` set.
3. When the loop block finishes, resume the foreach cursor; when the collection is exhausted, execution continues on the line below `foreach`.

An **empty** backtrack block for a guard inside the loop body means **continue** to the next collection item.

### 1.2 Source units (`use`)

Compile-time linking merges multiple `.fl` files into one FGIR program and one `.fb`. Native C libraries (`io.*`, `json.*`, …) are separate from `use`.

**Entry file** (argument to `flc` / `flatl compile`): exactly one `#main`.

**Imported modules:** no `#main`; only block definitions and handlers.

**Header syntax** (before the first `#block`; comments allowed):

```text
use "lib/math.fl" as #math_
```

| Rule | Detail |
|------|--------|
| `as #Prefix` | Required. `#Prefix`: first character after `#` must be a letter; then letters, digits, `_`, `-`, `:`. No `.` in the prefix. |
| Path | Resolved **only** relative to the directory of the file containing the `use` line. No `-I` in v1. |
| Duplicate `use` | Same resolved file twice in one `.fl` → error. |
| One module instance | Same file linked once program-wide; one prefix. Same file with a different `as` in another file → error. Same `as` elsewhere → silent dedupe. |
| Block names | Local `#add` with `as #math_` becomes global `#math_add` (literal concat after `#`). |
| In-module calls | `call #add` rewritten to `call #math_add`. Calls to other linked modules use the **already prefixed** name (`call #math_add`). |
| `#backtrack` / `#panic` | In a module, write `#backtrack.#add.N`; after link, `#backtrack.#math_add.N`. In entry, you may define `#backtrack.#math_add.N` for an imported block if the module did not (duplicate label → error). |
| Rewrite scope | Structural only (`call`, `foreach`, block labels) — not string payloads. |

**Errors:** short message without absolute path, e.g. `lib/utils.fl:3: use: cannot resolve "lib/math.fl" relative to this file`. With `flc --verbose` / `flatl compile --verbose`, an extra line may print the absolute path tried.

**Limits:** linked program up to `FGIR_MAX_BLOCKS` (1024 in v0.2). Further IR memory optimizations are planned separately.

---

## 2. Toolchain

```text
 program.fl  ──►  flc  ──►  program.fb  ──►  flvm  ──►  process exit status
 (source)              (bytecode)              (dispatch loop)
```

| Extension | Role |
|-----------|------|
| `.fl` | Human-readable flatL source |
| `.fb` | **flat Bytecodes** — binary image for the VM |

```bash
./flc program.fl -o program.fb
./flc --verbose program.fl -o program.fb   # link errors may show absolute path tried
./flvm program.fb
```

---

## 3. `.fb` binary layout (v1.1)

### 3.1 File structure

```text
+------------------------------------------------------------------+
| MAGIC (4 bytes): 0x46 0x4C 0x41 0x54  ('F','L','A','T')          |
+------------------------------------------------------------------+
| BLOCK COUNT (2 bytes, uint16)                                    |
+------------------------------------------------------------------+
| BLOCK DIRECTORY (repeat count times)                             |
|   - Block ID / hash (4 bytes)                                    |
|   - Byte offset in instruction stream (4 bytes)                  |
+------------------------------------------------------------------+
| BYTECODE INSTRUCTION STREAM                                      |
+------------------------------------------------------------------+
```

### 3.2 Instruction packet

```text
+--------+--------------+---------------------------+
| Opcode | Payload size | Payload / jump metadata   |
| 1 byte | 1 byte       | variable                  |
+--------+--------------+---------------------------+
```

Payload interpretation is **opcode-specific** (see below). The compiler replaces textual labels with numeric offsets or indices in the binary image.

### 3.3 Opcode table

| Name | Hex | Payload (v0.1) |
|------|-----|----------------|
| `OP_ASSIGN` | `0x01` | Source line as UTF-8 **NUL-terminated** string (VM: simple `name = value` store) |
| `OP_GUARD` | `0x02` | `uint32` absolute IP of `#backtrack.<block>.<index>` + guard expr **NUL-terminated** |
| `OP_CALL` | `0x03` | `uint32` target block IP + output var name **NUL-terminated** (may be empty) |
| `OP_RETURN` | `0x04` | Empty — pops call or advances `foreach` |
| `OP_FOREACH` | `0x05` | `uint32` loop block IP + original `foreach(...)` line **NUL-terminated** |
| `OP_OK` | `0x06` | Empty — process exit **0** |
| `OP_FAIL` | `0x07` | Empty — process exit **2** |
| `OP_PANIC` | `0x08` | `uint32` handler IP (`#panic.<block>.<index>` or `#panic.any`) |

Block directory **block id**: `0` for `#main`; other blocks use a stable hash of the label string.

If a guard fails and the backtrack offset is **0**, the VM exits with code **2** (unmapped logical failure).

### 3.4 Compilation sketch

Source:

```text
#main
  is_empty == true
  end.msg('Success')
  OK
```

Conceptual bytecode for `#main`:

1. `OP_GUARD` — payload includes backtrack jump address  
2. `OP_ASSIGN` — payload encodes `end.msg('Success')`  
3. `OP_OK`

---

## 4. Compiler pipeline (FGIR)

`flc` parses `.fl` into **FGIR** (Flat Graph IR) in memory, then emits `.fb`.

| Stage | Command | Output |
|-------|---------|--------|
| Parse | (internal) | `FgirProgram`: blocks, 1-based instruction indices |
| Inspect | `flc --dump-ir file.fl` | Human-readable block/instruction listing |
| Emit | `flc -o out.fb file.fl` | Binary `.fb` |

**Parser rules (v0.1):**

- Block headers: `#name` or `#name(a, b)`; exactly one `#main`.
- Lines under a block are instructions; full-line `--` comments and `--<` … `-->` blocks skipped.
- Classification: `OK`, `FAIL`, `PANIC`, `return`, `foreach(...)`, `call #…`, lines with `==` (guard), else `assign`.

**Compile-time linking:**

- Guard at index *n* in `#B` → `#backtrack.#B.n` (must exist or backtrack offset 0).
- `PANIC` at index *n* → `#panic.#B.n`, else fallback `#panic.any`.

---

## 5. Virtual machine (`flvm`)

Loads `.fb`, starts at `#main`, dispatches opcodes. **No full expression parser** — guards and assigns use a small interpreter over the payload strings.

| Mechanism | Behavior |
|-----------|----------|
| **Variables** | v0.2: tagged **BOOL**, **NUMBER**, **STRING** (INLINE ≤127 B or REF pool + slice views). Details: [LANGUAGE_TYPES.md](./LANGUAGE_TYPES.md). |
| **Guard** | Supports `true`/`false` literals, `var == true/false/'text'`, and `var == other`. On false: jump to backtrack IP or exit 2 if unmapped. |
| **Call** | Push return IP, jump to target block. |
| **Return** | If inside `foreach` body: next item or resume after `foreach`; else pop call frame; if no frame, exit 0. |
| **Foreach** | Reads collection variable as comma-separated list; binds item var; enters loop block; `return` advances iterator. |
| **PANIC** | Keyword in source emits `OP_PANIC`; jumps to handler block (handler may end with `OK`). |

**Process exit codes:**

| Code | Cause |
|------|--------|
| 0 | `OP_OK` or end of top-level execution |
| 1 | Bad file, truncated bytecode, unknown opcode |
| 2 | `OP_FAIL` or failed guard with no backtrack block |

Reference implementation: **C11**, WSL/Linux.

---

## 6. Future: dynamic modules

Independent compilation units may become native shared libraries (`.so` / `.dll`). Block entry points could be resolved via `dlopen` / `dlsym`, allowing rebinding of handlers without restarting the host process. Initial milestone is **static** `.fb` only.

---

## 7. Repository layout & automated tests

Built artifacts live under **`bin/`** (gitignored): `flc`, `flvm`, `run_tests`.

```text
flatL/
  Makefile
  include/          # flat.h, shared headers
  src/
    flc/
    flvm/
    run_tests/      # C test runner (no Node/JS)
    common/
    lib/              # native std modules (planned)
  examples/         # sample .fl programs
  test/cases/       # declarative cases only
    <suite>/<id>_<name>/
      case.toml
      … fixtures …
  docs/
  bin/              # build output
```

### 7.1 `case.toml` kinds

| kind | Purpose |
|------|---------|
| `files` | Assert paths exist under repo root (`check = "relative/path"`) |
| `exec` | Run shell command (`cmd`, `expect_exit`, optional `stdout_contains`) |
| `pipeline` | `source`, `compile`, `run`, `expect_exit` — see [TOOLCHAIN.md](./TOOLCHAIN.md) |

Discovery: `bin/run_tests` scans `test/cases/*/*/case.toml`.

```bash
make          # builds bin/flc, bin/flvm, bin/run_tests
make test     # runs all cases
bin/run_tests smoke[1-3] -v -e
```

Suite names match directories under `test/cases/` (`smoke`, `flvm`, `flc`, `e2e`, …).

Command reference for `flc`, `flvm`, `mkfb`, and `run_tests`: [TOOLCHAIN.md](./TOOLCHAIN.md).

---

## 8. Document status

**v0.1** — `flc`/`flvm` implement the opcodes above; conformance coverage in `test/cases/` (`smoke`, `flvm`, `flc`, `e2e`). Native dotted calls (`kv.get`, …) and full expression evaluation are **not** implemented yet.

**v0.2:** runtime value model — [LANGUAGE_TYPES.md](./LANGUAGE_TYPES.md) (limits, refs, slice views).
