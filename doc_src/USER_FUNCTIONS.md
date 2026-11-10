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
- Optional `func:varSlots N` sets the unique executor scratch VCX capacity; otherwise it is inferred from the body (including headroom for **nested** calls — desugar temps plus re-entrant global executor locals when a `.fmod` uses strategy A).
- Optional `func:callVM N` selects **how** user calls are lowered (see below). Omitted = same as **`0`**.
- The name after `->` on `def` is the formal return slot (here `sum`).

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

## Call site

```flat
add(2, 3) -> r
io.outln(r)
```

Lowering (strategy A) builds an instruction context (`__call_instr_vcx`), copies call metadata, and `call`s the executor block. The executor reads that context via **`V.callInstr()`** (VM call stack for strategy B / nested calls; strategy A uses the same native with fallback to `__call_instr_vcx`).

### `func:varSlots` and nested headroom (unique executor only)

With **`#executor.unique.scope`**, each call runs the body on a dedicated child VCX whose capacity is **`funcVcxSlots`** (stored in FFUN / passed on `instrVcx` for strategy B).

If you omit `func:varSlots` or set **`func:varSlots 0`**, **`flc` infers** the size:

1. **Scan** — distinct names from parameters plus assignment bindings in the `def` body.
2. **+2** — executor bookkeeping on the child VCX: `__run_block`, `__calle_id` (see `blocks/executor.fl`).
3. **Per nested call site** in the body — add two compile-time constants from `include/flat_func.h` (not FlatL syntax, only toolchain internals):

| Constant | Value | Role |
|----------|------:|------|
| **`FLAT_FUNC_NESTED_CALL_VCX_EXTRA`** | **12** | Margin for one nested `f(...) ->`, `m.f(...) ->`, or `m.calc('…') ->` while the **caller’s** funcVcx is still the active **callee** parent. Strategy A **desugar** expands a call into many temporary names on that VCX (on the order of ~11 slots: reserved `__call_*`, param_in_* lists, copy metadata, etc.). **12** is a rounded safety budget so lowering does not trip **`vcx.varsCopyByNames.capacity`**. |
| **`FLAT_FUNC_NESTED_GLOBAL_EXEC_EXTRA`** | **6** | Extra margin when the nested target often uses **`#executor.global.scope`** (typical **`.fmod`**): the global executor still copies through the same callee chain (`instrId`, `paramSrc`, …) without creating a sibling funcVcx the way the unique executor does. |

**Safety margin, not runtime law:** the **+12** / **+6** per call **site** are **`flc` compile-time guesses**, not a rule that nested bodies run on the parent VCX or that depth multiplies one context’s ID. At run time, **`#executor.unique.scope`** still runs each callee body on its **own** child **`funcVcx`** (`V.create(funcVcxSlots)` from that function’s metadata); **`calleVcx`** on the instruction is only the **parent ID at call time** (global `1` from `#main`, or the caller’s `funcVcx` when calling from inside another `def`). The toolchain does **not** yet do precise liveness on the parent VCX during setup/teardown, so it adds a **fixed cushion per `… ->` line in the body** to reduce **`vcx.varsCopyByNames.capacity`** panics (desugar temps, global-executor paths, sequential calls on the same parent). A tighter model could size the parent from **scan + executor locals only** and rely on each child’s separate **`func_vcx_slots`**; the current constants may be **conservative**. See tests `func/016`, `fmod/015` for cases where margin or executor choice still matters.

**Counted as nested:** `FGIR_FUNC_CALL` / `FGIR_FUNC_CALL_VM` (user `def` calls after parse) and assign lines that match `name(...) -> ret` (e.g. `m.calc('a * 2') -> r`).

**Global executor** (`#executor.global.scope`): inferred **`func_vcx_slots = 0`** — the body runs on the caller’s VCX; no child capacity is reserved.

**Manual `func:varSlots N`:** must be **≥** the scan (names only); **`flc`** errors with `func.varSlots.underflow` otherwise. Nested headroom is still added on top unless you rely on a large explicit `N` that already covers nested use (see `func/017`, `fmod/017`).

#### Lowering rules worth remembering (AM-1)

| Situation | Behaviour |
|-----------|-----------|
| Entry has **`func:callVM 1`**, callee from **`.fmod` without** `func:callVM 1` | Call site still uses **strategy B** (`OP_CALL_FUNCTION`) if the **caller** `def` was declared under `func:callVM 1` — avoid forcing strategy A into unique→unique desugar by mistake (`fmod/018`). |
| Second (or later) user call in the **same unique `def` body** | **`flc` does not** emit **`V.set(1)`** after the call inside a unique function body; the executor already restored **`calleVcx`**. **`V.set(1)`** there would switch to the global context and break the next nested call (`func/016`). |
| **Two** nested calls in **one** `def` on a **unique** funcVcx | Later calls can reuse the same parent VCX while temps from earlier calls are still accounted for; stress cases may need higher **`func:varSlots`** or a **global** executor on that `def` (`fmod/015` documents two `m.mul2` with **global** executor; unique + double call is covered by `func/016`). |

## Executors

| Label | Behaviour |
|-------|-----------|
| `#executor.global.scope` | Body runs on the caller context (`calleVcx`); after the call, `r = <returnVar>` assigns into the callee result name. |
| `#executor.unique.scope` | Body runs on a dedicated VCX; on return, formal and caller names are copied via `L.pack` + `V.varsCopyByNames`. |

Source: `blocks/executor.fl` (use with prefix `as #fl_` → labels `#fl_executor.*`).

## Expressions in the body

Plain `lhs = rhs` allows a **single** rhs atom (literal, variable, native call). Use `m.calc('…')` for arithmetic with variables in the expression string, e.g. `m.calc('a + b') -> sum`.

Lines of the form `native(...) -> var` are compiled as **ASSIGN**, not I/O.

## FFUN (`.fb` v5)

`flc` emits an optional **FFUN** section in v5 bytecode; `flvm` does not consume it at run time (toolchain metadata).

## Modules (`.fmod`, v0.4)

For compile-time libraries with **`export`** and `use fmod "path" as alias`, see **[FMOD_MODULES.md](./FMOD_MODULES.md)**.
