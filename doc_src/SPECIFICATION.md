# flatL — Architecture & Binary Specification (draft)

Documentation index: [README.md](./README.md) (English).

Consolidated from project design notes. Normative details will be refined as `flc` and `flvm` are implemented in the repository.

**Related:** FGIR (Flat Graph Intermediate Representation) v1.0 · `.fb` bytecode & VM v1.1

**Typed bind & schemas (T1–T2 in VM):** [STRICT_TYPES.md](./STRICT_TYPES.md) · [TYPED_BIND.md](./TYPED_BIND.md) — opt-in `name:type`, **`<schema>:`** + **`name:<schema>`**, slot contract per VCX; `freeze()` / `const` still planned.

---

## 1. Flat graph model (FGIR)

Programs are **not** abstract syntax trees. They are a **flat graph** of **basic blocks**:

- Each block is a contiguous, linear sequence of instructions (e.g. `#main`, `#check_balance`).
- Control flow is **explicit**: guards, `#or` handler labels, panic labels, and `call` targets are resolved at **compile time**.
- **Logical failure** (`FAIL`, failed guards) is separate from **infrastructure panic** (I/O errors, OOM, timeouts, network faults).

### 1.1 Lexical and structural rules

| Construct | Meaning |
|-----------|---------|
| `#name` | Block declaration |
| `#name(arg1, arg2)` | Block with parameter list |
| Indented lines under a block | Sequential execution (implicit AND), top to bottom |
| `call #block(args) -> var` | Invoke another block; bind result to `var` |
| `jump #block` | Transfer control to block **without** a `call` frame (no trailing `;`) |
| `expr == value` | Guard; on false, branch to `#or` handler |
| `OK` / `FAIL` | Terminal success / logical failure |
| `return expr` | Return from block to caller |
| `foreach(coll, item, '#loop')` | Iterate collection via sub-block |
| `for(ntimes, i, '#loop')` | Fixed-count loop: `i` = `0 … floor(ntimes)-1` (see §1.4) |
| `-- …` | Full-line comment (from `--` to end of line) |
| `--<` … `-->` | Block comment (multiple lines; see below) |

**Comments (v0.2):**

- After trimming leading/trailing whitespace, a line starting with **`--`** (but not **`--<`**) is a **line comment** and is ignored. Examples: `-- todo: fix`, `-- note`, `-- #not-a-block`.
- A line starting with **`--<`** opens a **block comment**; scanning continues until a line starting with **`-->`**. Blank lines inside the block are ignored. Nested **`--<`** is a parse error; an unclosed block at EOF is a parse error.
- **No trailing comments** on instruction lines — `--` in the middle of a line is part of the instruction text.
- Block comments and line comments may appear before blocks, between blocks, or between instructions.

Explicit **`jump #block`** transfers control at compile-time-resolved block entry. Guard failure still uses **`#or.*`** handler blocks (not `jump`).

### 1.1.1 Language keywords vs native libraries

**Keywords** (compile to VM opcodes — not optional “built-in functions”):

- `#block`, `call`, `jump`, `return`, `foreach`, `for`, `OK`, `FAIL`, `->` (call result binding)
- **`free(var)`** — release pool-backed value held by **`var`**; **`var`** becomes **`null`** (see [LANGUAGE_TYPES.md](./LANGUAGE_TYPES.md) §5)
- **`freeze(var)`** — *planned* slot immutability after bind (see [TYPED_BIND.md](./TYPED_BIND.md) §8); not implemented yet
- **`const name:type = expr`** — *planned* immutable typed bind (same doc)
- Guards and assignment lines (`=`, `==`, value **`->`** bind) map to `OP_ASSIGN` / `OP_GUARD`

**Assignment / bind** (same RHS rules — one literal, variable, list/object literal, `mem.ref_from(...)`, etc.; **no** infix such as `A * 2`):

| Form | Example |
|------|---------|
| Equals | `n = 6`, `s = 'hi'`, `B = A` |
| Typed bind *(planned)* | `a:u8 = 3`, `row:<stock> = {}`, `s:/typeStr` — [TYPED_BIND.md](./TYPED_BIND.md) |
| Arrow (value first) | `6 -> n`, `'hi' -> s`, `A -> B` |
| Arrow + type *(planned)* | `json.parse_object(s) -> row:<stock>` |
| Native + arrow | `str.to_num(raw) -> n` ( **`FGIR_IO`** , not assign) |
| Native + equals | `n = str.to_num(raw)` |

**`?` / `~`** apply only to **native** lines (including `n = str.to_num(x)`); not to plain value binds like `6 -> n` or `n = 6`. Mixing **`=`** and **`->`** on one line is a **compile error**.

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

### 1.2 Or-handler addressing (`#or`)

When the guard at instruction index *n* in block `#target` evaluates to false, the runtime transfers control to:

```text
#or.#target.n
```

Alternatively, if that instruction carries a **line label** (see §1.2.1), control may transfer to:

```text
#or.#target.<lineLabel>
```

If no such block exists, the enclosing flow aborts with logical **FAIL**.

#### 1.2.1 Line labels (`#.ident`)

After a full instruction on a source line, an optional **end-of-line** marker may appear:

```text
amount = form.amount   #.loadAmount
has_funds == true   #.checkFunds
```

| Rule | Detail |
|------|--------|
| Position | **EOL only** — whitespace, then `#.` + identifier; not inside expressions |
| Identifier | Letter first; then letters, digits, `_`; no `.` |
| Uniqueness | At most one `#.ident` per line; each **ident** appears on **at most one line in the `.fl` file** (global per unit) |
| Handlers | Define `#or.#block.ident` or `#or.#block.n`; in a **`#or.*` handler block**, a failed guard may use short handler **`#or.<ident>`** (same `ident` as the guard’s `#.<ident>`) |
| References | Use `#or.#block.ident` (no `#.` in the path); last segment is either a **1-based index** or a **line label** |

When a guard fails, the compiler resolves the handler block in order:

1. **`#or.<currentBlock>.<n>`** or **`#or.<currentBlock>.<lineLabel>`** (always tried first).
2. If **`<currentBlock>`** is itself a **`#or.*`** handler and the guard has **`#.<lineLabel>`**, also try **`#or.<lineLabel>`** (short chain form).

There is **at most one** handler per **`#.<lineLabel>`** in a file — do not define both `#or.#main.X` and `#or.X` for the same line label.

### 1.2.2 Line modifiers (`?` and `~`)

Optional markers at the **start of an instruction line** (after indent), before any native call or assign-to-native:

| Modifier | On recoverable native failure |
|----------|--------------------------------|
| *(none)* | **PANIC** — reason string is the **qualified native name** (e.g. `str.to_num`, `json.parse_list`, `obj.get`); handlers **`#panic.#block.n`**, **`#panic.#block.#.ident`**, or **`#panic.any`** (same as I/O panic) |
| **`?`** | **Silent** — target binding unchanged; execution continues with the next line in the block |
| **`~`** | **Logical failure** — no bind; branch to the same **`#or.*`** target as a **false guard** on that line (index or **`#.<ident>`**); if no handler → **FAIL** (exit 2) |

Rules:

- At most **one** of **`?`** or **`~`**; **`?~`** / **`~?`** → compile error.
- Allowed only on lines that call a **native** (`io.outln(…)`, `str.to_num(…) -> n`, `n = json.parse_list(…)`, …). **`? OK`**, **`? x = 1`**, **`? call …`**, guards, **`jump`**, etc. → compile error.
- **`? n = str.to_num(x)`** and **`? str.to_num(x) -> n`** are equivalent (native only).
- Value binds **`6 -> n`** and **`n = 6`** are equivalent at runtime; modifiers do **not** apply to those forms.
- Scope is the **whole line**: any outcome that would be PANIC without a modifier is covered (parse fail, EOF on `? io.in_num`, stale I/O context, …).
- **Bind** is atomic: on failure the assignment target is unchanged. **I/O** already performed on that line is not rolled back.

Example — default panic vs silent:

```text
#main
  raw = 'x'
  str.to_num(raw) -> price
  OK
```

→ PANIC `str.to_num` unless `#panic.any` (or a indexed handler) exists.

```text
  ? str.to_num(raw) -> price
  io.outln('continues')
  OK
```

Example — `~` and `#or`:

```text
#main
  ~ str.to_num(raw) -> price   #.badNum
  io.outln('only if parse OK')
  OK

#or.#main.badNum
  io.outln('not a number')
  OK
```

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

#or.#main.4
  end.err('Insufficient funds')
  FAIL

#or.#main.6
  end.err('Payment declined')
  FAIL
```

### 1.3 Panic addressing

On an **infrastructure** fault at index *n* in block `#target`, the runtime routes to:

```text
#panic.#target.n
```

(or `#panic.#target.<lineLabel>` when the faulting instruction has a line label and a matching handler block exists).

If no handler exists for that index, control falls through to **`#panic.any`** (e.g. transaction rollback via `tstop()`).

**Panic reason intrinsics** (language builtins, not optional natives):

- **`panicReason() -> name`** — binds the **last infrastructure panic reason** (same string as VM / unhandled stderr: `io.parse`, `for.ntimes_type`, qualified native name, `type.assign.*`, …). Empty string when none was recorded.
- **`panicReset()`** — clears the stored reason; the next **`panicReason()`** returns `""` until another panic is recorded.

The reason is updated on each PANIC routed to a handler (`#panic.*` / `#panic.any`). It is **not** cleared when the handler returns unless the program calls **`panicReset()`**.

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

#### 1.4.1 `foreach`

`foreach(collection, key_var, '#loop_body')`:

1. **`collection`** is evaluated **once** at loop entry as a **FlatL expression** that must yield a **LIST** (variable, path such as `o.tags`, or literal `[1, 2, 3]`). Not a CSV string — use a LIST value.
2. For each element, jump to `#loop_body` with `key_var` set.
3. When the loop block finishes, resume the foreach cursor; when the collection is exhausted, execution continues on the line below `foreach`.
4. If the expression is not a LIST → **PANIC** **`foreach.collection`**.

Commas inside **`[ … ]`** in the first argument are **not** argument separators (same bracket-aware splitting as native calls).

An **empty** `#or` handler block for a guard inside the loop body means **continue** to the next collection item.

#### 1.4.2 `jump` (control transfer)

| Rule | Detail |
|------|--------|
| Syntax | **`jump #block`** — no semicolon; **`#block`** must exist |
| Call stack | **Unchanged** — no push, no pop |
| Loop stack | **`for` / `foreach` frames cleared** on `jump` (abandon active loops) |
| After jump | Instructions below `jump` in the same block are **not** executed |
| vs `call` | Use **`call`** when you need a **`return`** back to the caller; use **`jump`** for shared paths (e.g. `#common`) without a frame |
| Jump budget | Default **1000** `OP_JUMP` executions per run (`FLAT_VM_DEFAULT_MAX_JUMPS`); exceed → PANIC `vm.jump_limit` (routed like other panics). Override: `flvm --max-jumps N`, or `--no-jump-limit`. **fleditor WASM:** Settings → Terminal, or `flat_wasm_set_run_limits` before run |

Work plan: [`.cursor/plans/jump-or-handlers.plan.md`](../.cursor/plans/jump-or-handlers.plan.md).

#### 1.4.3 `for` (numeric)

`for(ntimes, i, '#loop_body')` — same **block + `return`** model as `foreach`; third argument is a **block label** (quoted `'#name'`), rewritten on `use` link like `foreach`.

| Rule | Detail |
|------|--------|
| Semantics | Equivalent to `for (i = 0; i < ntimes; i++)` with `i` only inside the loop body binding |
| `ntimes` | Must evaluate to **NUMBER** at loop entry; may be a **variable** or a **numeric literal** |
| Non-number | Any other type → **PANIC** (infrastructure), not silent skip |
| `ntimes ≤ 0` | **0 iterations**; execution continues on the line below `for` |
| Fractional `ntimes` | **Not an error** — effective bound is **`floor(ntimes)`** (same as comparing `i < ntimes` with integer `i`) |
| `i` | Loop index variable; bound to `0`, `1`, …, `floor(ntimes) - 1` on each entry to `#loop_body` |
| Body exit | **`next`** advances to the next `i` / collection item; **`return`** exits the loop (resume after `for` / `foreach`) |
| Loop block arg | Third argument: quoted `'#block'`, unquoted `#block`, or a **STRING** variable (e.g. `lbl = '#step'; for(n, i, lbl)`) |
| Guards in body | Same `#or` / empty-handler **continue** convention as `foreach` (when implemented in VM) |

Example:

```text
#main
  count = 3
  for(count, k, '#step')
  OK

#step
  io.outln(k)
  return
```

With `count = 0`, the body never runs.

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
| `#or` / `#panic` | In a module, write `#or.#add.N`; after link, `#or.#math_add.N`. In entry, you may define `#or.#math_add.N` for an imported block if the module did not (duplicate label → error). |
| Rewrite scope | Structural only (`call`, `foreach`, `for`, block labels) — not string payloads. |

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

## 3. `.fb` binary layout (v1.2)

### 3.1 File structure

**Current emit (`flc` / `flatl compile`) — format version 1:**

```text
+------------------------------------------------------------------+
| MAGIC (4): 'F','L','A','T'                                       |
| SENTINEL (1): 0xFF                                               |
| FORMAT VERSION (1): 0x01                                         |
| BLOCK COUNT (2, uint16 LE)                                       |
| OPTIONAL NATIVE OFFSET (4, uint32 LE) — 0 if none                |
+------------------------------------------------------------------+
| BLOCK DIRECTORY (count × 8 bytes)                                  |
|   Block ID (4) + instruction stream offset (4)                   |
+------------------------------------------------------------------+
| BYTECODE INSTRUCTION STREAM                                      |
+------------------------------------------------------------------+
| OPTIONAL NATIVE MANIFEST (only if offset ≠ 0)                    |
|   uint16 count                                                   |
|   repeat: u8 module_len, module bytes, u8 alias_len, alias bytes |
|   uint32 tail magic 0x54414E46 ('FNAT' LE)                       |
+------------------------------------------------------------------+
```

**Legacy load:** if byte at offset 4 is **not** `0xFF`, the file is treated as v0.1: block count at offset 4 (uint16), directory at offset 6, no native manifest.

Optional natives declared in source (`use native …`) are aggregated at link time; the manifest lists module IDs and optional `with` aliases. `flvm` uses it to allow lazy loading of optional native plugins (see [natives/README.md](./natives/README.md)).

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
| `OP_ASSIGN` | `0x01` | `u8` line_mod + source line **NUL-terminated**; if mod is **`~`**, trailing `u32` **`#or`** handler IP |
| `OP_NATIVE` | `0x0A` | `u16` native id + inner args **NUL-terminated** + bind name **NUL-terminated** + `u8` line_mod + `u32` **`#or`** IP (IP used when mod is **`~`**) |
| `OP_FREE` | `0x0B` | Variable name **NUL-terminated** — unbind pool value; **`var = null`** |
| `OP_GUARD` | `0x02` | `uint32` absolute IP of `#or.<block>.<index>` handler + guard expr **NUL-terminated** |
| `OP_CALL` | `0x03` | `uint32` target block IP + output var name **NUL-terminated** (may be empty) |
| `OP_CALL_BLOCK` | `0x0E` | Block id expression **NUL-terminated** + bind name **NUL-terminated** (may be empty; bind reserved) |
| `OP_JUMP` | `0x0D` | `uint32` target block IP |
| `OP_RETURN` | `0x04` | Empty — pops call or advances `foreach` |
| `OP_FOREACH` | `0x05` | `uint32` loop block IP + original `foreach(...)` line **NUL-terminated** |
| `OP_FOR` | `0x0C` | `uint32` loop block IP + original `for(...)` line **NUL-terminated** |
| `OP_OK` | `0x06` | Empty — process exit **0** |
| `OP_FAIL` | `0x07` | Empty — process exit **2** |
| `OP_PANIC` | `0x08` | `uint32` handler IP (`#panic.<block>.<index>` or `#panic.any`) |
| `OP_NEXT` | `0x0F` | Empty — `for` / `foreach` continue |
| `OP_CALL_FUNCTION` | `0x10` | **`func:callVM 1`:** `u16` FFUN index + `u8` argc + `u8` global + callee return name **NUL** + formal param names each **NUL-terminated** (see [USER_FUNCTIONS.md](./USER_FUNCTIONS.md)) |

Block directory **block id**: `0` for `#main`; other blocks use a stable hash of the label string.

If a guard fails and the `#or` handler offset is **0**, the VM exits with code **2** (unmapped logical failure).

### 3.4 Compilation sketch

Source:

```text
#main
  is_empty == true
  end.msg('Success')
  OK
```

Conceptual bytecode for `#main`:

1. `OP_GUARD` — payload includes `#or` handler jump address  
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
- Classification: `OK`, `FAIL`, `PANIC`, `return`, `foreach(...)`, `for(...)`, `call #…`, **`call block` *expr***, `jump #…`, lines with `==` (guard), else `assign`.

**Compile-time linking:**

- Guard at index *n* in `#B` → `#or.#B.n` (must exist or handler offset 0).
- `PANIC` at index *n* → `#panic.#B.n`, else fallback `#panic.any`.

---

## 5. Virtual machine (`flvm`)

Loads `.fb`, starts at `#main`, dispatches opcodes. **No full expression parser** — guards and assigns use a small interpreter over the payload strings.

| Mechanism | Behavior |
|-----------|----------|
| **Variables** | v0.2: tagged **BOOL**, **NUMBER**, **STRING** (INLINE ≤127 B or REF pool + slice views). Details: [LANGUAGE_TYPES.md](./LANGUAGE_TYPES.md). |
| **Guard** | Supports `true`/`false` literals, `var == true/false/'text'`, and `var == other`. On false: jump to `#or` handler IP or exit 2 if unmapped. |
| **Call** | Push return IP, jump to target block; clears “entered via jump” flag. |
| **Call block** | Source line **`call block` *expr*** — *expr* is evaluated at run time to a **whole NUMBER** **block id** (see block directory); same call/return stack as **`call #…`**. PANIC: **`call.block.arg`**, **`call.block.id`**, **`call.block.notfound`**, **`call.block.limit`**. Details: [CALL_BLOCK.md](./CALL_BLOCK.md). |
| **Jump** | Set IP to target block; **clear** active `for`/`foreach` frames; mark entry as via jump (no call frame). |
| **Return** | Pop **call** frame first if any; else if inside `foreach` / `for` body: **exit the loop** (resume after the loop); else exit 0 (complete). |
| **Next** | Pop **call** frame if any (like `return`); else if inside a loop body: **next iteration**; else complete (exit 0). |
| **Foreach** | Reads collection variable as comma-separated list; binds item var; enters loop block; `return` advances iterator. |
| **PANIC** | Keyword in source emits `OP_PANIC`; jumps to handler block (handler may end with `OK`). |

**Process exit codes:**

| Code | Cause |
|------|--------|
| 0 | `OP_OK` or end of top-level execution |
| 1 | Bad file, truncated bytecode, unknown opcode |
| 2 | `OP_FAIL` or failed guard with no `#or` handler block |

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
  doc_src/          # normative Markdown (sources)
  docs/             # GitHub Pages: fleditor_site + doc_site
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

**v0.1** — baseline opcodes and `test/cases/` smoke suites.

**v0.2** — runtime value model — [LANGUAGE_TYPES.md](./LANGUAGE_TYPES.md) (limits, refs, slice views).

**v0.4 (toolchain)** — user **`def`** and executors ([USER_FUNCTIONS.md](./USER_FUNCTIONS.md)); **`use fmod`** ([FMOD_MODULES.md](./FMOD_MODULES.md)); broad **native** catalog (`io`, `str`, `math`, `json`, `file`, `vcx`, `list`, `obj`, …). Examples in §1 that use **`form.*`**, **`kv.*`**, **`end.*`**, or **`call #block(args) -> var`** on plain blocks are **illustrative** (target application shape), not shipped APIs. Parameterized calls that work today use **`def`**; plain **`#block(param)`** headers are parsed but not bound at run time; **`return expr`** on blocks is not supported (bare **`return`** only). See the capability table in the root [README.md](../README.md).
