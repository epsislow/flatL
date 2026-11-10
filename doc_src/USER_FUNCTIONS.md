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
- Optional `func:varSlots N` sets the unique executor scratch VCX capacity; otherwise it is inferred from the body.
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

**Coexistence:** one program may mix zones (`func:callVM 0` then later `func:callVM 1`). Default **`0`** if the header line is omitted. Old `.fb` files without **`0x10`** remain strategy A only.

**Toolchain:** WASM `flc` in fleditor follows the same rules after AM-1. To compare call lowering size (A vs B), use static disassembly: **`fbdump`** on `.fb` or **`flc --dump-bc`** on `.fl` — see [TOOLCHAIN.md § Bytecode inspection](./TOOLCHAIN.md#bytecode-inspection-fbdump--flc---dump-bc).

## Call site

```flat
add(2, 3) -> r
io.outln(r)
```

Lowering (strategy A) builds an instruction context (`__call_instr_vcx`), copies call metadata, and `call`s the executor block.

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
