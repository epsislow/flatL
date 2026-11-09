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
- The name after `->` on `def` is the formal return slot (here `sum`).

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

## Modules (phase H — partial)

`mod(` / `export` are parsed in headers; `use fmod` linking is not implemented yet.
