# User functions (v0.3)

User functions are declared with `def`, run under a VCX **executor**, and are called from `#main` or other blocks.

## Declaration

```flat
use "blocks/executor.fl" as #fl_

func:executor #fl_executor.global.scope   ; or #fl_executor.unique.scope

def add(a, b) -> sum
  m.calc('a + b') -> sum
  return
```

- `func:executor` selects isolation mode (see `blocks/executor.fl`).
- Optional `func:varSlots N` is an **initial capacity seed** for the unique executor’s `V.createAuto(...)` (see below). **`0`** or omitted → default seed (**4**). Capacity grows at runtime up to **`FLAT_MAX_VARS`**.
- Optional `func:callVM N` selects **how** user calls are lowered (see below). Omitted = same as **`0`**.
- The name after `->` on `def` is the formal return slot (here `sum`).
- **Typed formals / return (T6):** `def add(a:u8, b) -> out:u16` — `a` and `out` carry slot contracts; `b` stays untyped. Invalid arguments panic at bind (**`type.assign.range`**, **`type.call.arg`**, …). Types are stored in **FFUN** (`.fb` **v7**).

### `func:callVM` — call lowering (A vs B)

Header directive (like `func:executor`): applies to every **`def` below** until the next `func:callVM` line.

| Value | Name | Status | Meaning |
|-------|------|--------|---------|
| **`0`** | **Strategy A** (desugar) | **Default, implemented** | `flc` expands each call into bytecode + natives (`vcx.*`) and `call #…executor…`. Same contract `instrVcx` as in [RUNTIME_ARCHITECTURE.md](./RUNTIME_ARCHITECTURE.md). |
| **`1`** | **Strategy B** (VM) | **Implemented (AM-1)** | `flc` emits **`OP_CALL_FUNCTION` (`0x10`)**; **`flvm`** builds `instrVcx` in C, then **`call`s the executor** block (same as **`0`**). Semantics must match **`0`**. Use **`fbdump --summary`** to compare opcode counts vs **`func:callVM 0`**. |

```flat
func:callVM 0
func:executor #fl_executor.global.scope
def add(a, b) -> sum
  ...
```

**Coexistence:** one program may mix zones (`func:callVM 0` then later `func:callVM 1`, including between `def`s). Default **`0`** if the header line is omitted. Old `.fb` files without **`0x10`** remain strategy A only. Nested mix tests: `test/cases/func/012`–`014` (in-process), `test/cases/fmod/009`–`013` (with `.fmod`).

**Toolchain:** WASM `flc` in fleditor follows the same rules after AM-1. To compare call lowering size (A vs B), use static disassembly: **`fbdump`** on `.fb` or **`flc --dump-bc`** on `.fl` — see [TOOLCHAIN.md § Bytecode inspection](./TOOLCHAIN.md#bytecode-inspection-fbdump--flc---dump-bc).

**AM-1 microbench (loop):** `test/cases/bench/` — 20 000× `inc(s)` in a `for` loop (`001` = strategy A, `002` = strategy B). Case **`003`** runs `test/bench/compare_callvm_loop.sh` (3× `flvm --time`, min `run` ms, opcode must be ≤ desugar + **5%** slack; logs ratio and optional `fbdump --summary`). Manual: `bin/run_tests bench -v` or `sh test/bench/compare_callvm_loop.sh .` Env: `FLAT_CALLVM_BENCH_ROUNDS`, `FLAT_CALLVM_BENCH_SLACK`. The plan’s **~20% faster** target is informational, not a CI gate.

## Call site

```flat
add(2, 3) -> r
io.outln(r)
```

### Overload by tag (v2.0, AM-TAGURI)

Multiple `def` lines may share the same name when **arity** and/or **tag set** differ. Tags are **author-defined tokens** (not limited to numeric type names).

| Declarație | Tag set |
|------------|---------|
| `def f(a, b) -> r` | fără tag (sau echivalent `def f(a, b ; ) -> r` — **o singură** variantă fără tag) |
| `def f(a, b ; "fast") -> r` | tag literal const obligatoriu pe linia `def` |
| `def f(a, b ; "")` | **eroare compile** — `type.tag.arg` |

Apel:

```flat
pick(4 ; "fast") -> a
pick(4 ; "slow") -> b
pick(4) -> c              # varianta fără tag, dacă există
pick(4 ; tags) -> d       # tag variabil: necesită `func:callVM 1` (rezolvare în VM)
```

- Potrivire: `(nume, arity, tag_set_canonic)` — tokenii se **sortează alfabetic** la compile (ca la natives).
- Duplicate `(nume, arity, tag_set)` → **`type.def.overload.duplicate`**.
- Apel fără `;` când există doar variante cu tag → **`type.tag.not_accepted`**.
- **FFUN** în `.fb` **format v12**: fiecare intrare include `def_tag_has_section` + `tag_canonical`.
- Apeluri cu tag **dinamic** (`func:callVM 1` + `OP_CALL_FUNCTION` index `0xFFFF`): VM evaluează expresia tag și alege intrarea FFUN.

Tests: `test/cases/func/024`–`025`, `test/cases/types/159`–`162`.

### Multi-body sugar (v2.1)

Poți grupa overload-uri pe **tag** sub un singur „head” `def`, cu **clauze** după primul corp:

```flat
def pick(x ; "fast") -> r
  r = x
  return
  (; "slow") -> r
  m.calc('x * 3') -> r
  return
```

- `flc` desugarează la **aceleași** intrări FFUN ca la v2.0 (mai multe `def` implicite).
- Clauza: **`(; "tag") -> return…`** — fără parametri în paranteze; **tag** și **nume return** trebuie să coincidă cu head-ul (altfel `def.clause.return.*`).
- `;( )` pe clauză = variantă **fără tag** (aceeași regulă ca v2.0).
- Tag duplicat în același sugar → **`type.def.overload.duplicate`**.

Tests: `test/cases/func/026`–`027`.

Lowering (strategy A) builds an instruction context (`__call_instr_vcx`), copies call metadata, and `call`s the executor block. The executor reads that context via **`V.callInstr()`** (VM call stack for strategy B / nested calls; strategy A uses the same native with fallback to `__call_instr_vcx`).

### Multiple return values (AM-2)

A `def` may declare **up to 8** formal return slots (comma-separated after `->`). The call site must bind the **same number** of caller variables.

```flat
def parseRow(line) -> row, err, ok
  row = line
  err = ''
  ok = true
  return

parseRow('x') -> r, e, flag
r, e, flag = parseRow('y')
```

| Form | Example |
|------|---------|
| Arrow | `pair() -> a, b` |
| Assign | `a, b = pair()` |

Rules:

- Return names on `def` are **formal slots** in the function VCX (unique) or caller VCX (global); the call lists **existing** caller variable names.
- **Typed returns:** `def f() -> lo:u8, hi:u16` — same FFUN / **`vcx.copyCallReturn`** checks as a single typed return (**`type.call.return`** on failure).
- **Arity mismatch** at compile → **`call.function.return.count`**. Formal return names must not match a parameter name (**`def.return.name.param`**).
- **`.fb` FFUN v10+** stores `return_count` and per-return type strings; **v9** loaders still work (one return). New programs compile to **format version 10**.
- User **natives** with multi-return are **not** supported yet (only user `def`).

Tests: `test/cases/func/019`–`023`, `test/cases/types/112`.

### `func:varSlots` and `V.createAuto` (unique executor only)

With **`#executor.unique.scope`**, each call runs the body on a dedicated child VCX created by **`V.createAuto(funcVcxSlots)`** in `blocks/executor.fl`. The value **`funcVcxSlots`** in FFUN / on `instrVcx` is only an **initial capacity seed** (not a fixed limit).

| `func:varSlots` | `funcVcxSlots` at runtime | Meaning |
|-----------------|----------------------------|---------|
| omitted or **`0`** | **0** → **`createAuto()`** uses default seed **4** | Auto-grow when new names are bound |
| **`N` ≥ 1** | **N** → **`createAuto(N)`** | Start with capacity **N**, then grow as needed |

Growth is the same as [`vcx.createAuto`](./natives/vcx.md): **`varsMax`** increases on new bindings and on copy-into-dest when the destination is an auto context, up to **`FLAT_MAX_VARS`**.

**Global executor** (`#executor.global.scope`): **`func_vcx_slots = 0`** in metadata — the body runs on the caller’s VCX; no child context is created.

**Compile-time:** `flc` no longer infers capacity from the `def` body (no `func.varSlots.underflow`). Wrong explicit seeds only cost extra reallocations; undersized fixed **`V.create(N)`** contexts still panic at capacity as before.

#### Lowering rules worth remembering (AM-1)

| Situation | Behaviour |
|-----------|-----------|
| Entry has **`func:callVM 1`**, callee from **`.fmod` without** `func:callVM 1` | Call site still uses **strategy B** (`OP_CALL_FUNCTION`) if the **caller** `def` was declared under `func:callVM 1` — avoid forcing strategy A into unique→unique desugar by mistake (`fmod/018`). |
| Second (or later) user call in the **same unique `def` body** | **`flc` does not** emit **`V.set(1)`** after the call inside a unique function body; the executor already restored **`calleVcx`**. **`V.set(1)`** there would switch to the global context and break the next nested call (`func/016`). |
| Nested calls + **`for` / I/O binds / helpers** on unique executor | Rely on **`createAuto`** — see `func/018`, nested suites `func/012`–`017`, `fmod/012`–`018`. |

## Executors

| Label | Behaviour |
|-------|-----------|
| `#executor.global.scope` | Body runs on the caller context (`calleVcx`); after the call, `r = <returnVar>` assigns into the callee result name. |
| `#executor.unique.scope` | Body runs on a dedicated VCX; params via **`V.bindCallParams`**, return via **`V.copyCallReturn`** (typed when declared on `def`). |

Source: `blocks/executor.fl` (use with prefix `as #fl_` → labels `#fl_executor.*`).

## Expressions in the body

Plain `lhs = rhs` allows a **single** rhs atom (literal, variable, native call). Use `m.calc('…')` for arithmetic with variables in the expression string, e.g. `m.calc('a + b') -> sum`.

Lines of the form `native(...) -> var` are compiled as **ASSIGN**, not I/O.

## FFUN (`.fb` v5+, **v7** typed `def`)

`flc` emits **FFUN** (function table). **v7** adds per-parameter and return **type spec strings** for typed `def`; `flvm` uses them in **`vcx.bindCallParams`** / **`vcx.copyCallReturn`**.

## Modules (`.fmod`, v0.4)

For compile-time libraries with **`export`** and `use fmod "path" as alias`, see **[FMOD_MODULES.md](./FMOD_MODULES.md)**.
