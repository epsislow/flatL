# Native module `executor`

**Fast `def` executors** for **`func:executor`** blocks — replaces the FlatL/`vcx` sequence in `blocks/executor.fl` with four natives. **Optional** core plugin (like `vcx`):

```text
use native executor with E
use "blocks/native-exec.fl" as #ne_
func:executor #ne_executor.unique.scope
```

`blocks/executor.fl` (`#fl_*`) is unchanged; use **`blocks/native-exec.fl`** (`#ne_*`) when you want the native path.

## Quick example

```text
use native executor with E
use "blocks/native-exec.fl" as #ne_
use native math with m

func:executor #ne_executor.unique.scope

def add(a, b) -> sum
  m.calc('a + b') -> sum
  return

#main
  add(2, 3) -> r
  io.outln(r)
  OK
```

## Syntax

| Form | Example |
|------|---------|
| Assign label | `blockId = E.uniqueBegin()` |
| Statement | `E.uniqueEnd()` |

**`uniqueBegin` / `globalBegin`** return a **NUMBER** — the same **block id** as `targetBlock` on the instr VCX (for **`call block blockId`**, like `__run_block` / `runId` in `blocks/executor.fl`). The executor block then uses **`call block blockId`** for the function body, then **`*End()`** with no arguments.

With **`use native executor with E`**, the qualified names are **`executor.uniqueBegin()`**, …; examples use alias **`E.*`**.

## Function reference

| Call | Arguments | Returns | Panic (no modifier) |
|------|-----------|---------|---------------------|
| **`E.uniqueBegin()`** | — | NUMBER — body **block id** for **unique** scope | `executor.begin.*` (see below) |
| **`E.uniqueEnd()`** | — | — | **`executor.end.noframe`**, teardown / `vcx.*` |
| **`E.globalBegin()`** | — | NUMBER — body **block id** for **global** scope | same family as unique begin |
| **`E.globalEnd()`** | — | — | **`executor.end.noframe`**, teardown / `vcx.*` |

### Begin (shared semantics)

On **`OP_CALL_FUNCTION`**, the runtime already created an **instr VCX** (`V.callInstr()`). **Begin**:

- **Unique:** `createAuto(funcVcxSlots)`, `bindCallParams`, sets **`__calle_id`** on the child (parity with `executor.fl`), activates child, returns **`targetBlock`** as NUMBER.
- **Global:** `bindCallParams` on **calleVcx**, returns **`targetBlock`** (caller context stays active, same as `blocks/executor.fl`).

Typical begin failures: `executor.begin.nobind`, `executor.begin.callInstr`, `executor.begin.calleVcx`, `executor.begin.target`, `executor.begin.frame`.

### End (happy path)

- **Unique:** `set(calleVcx)`, `copyCallReturn`, destroy child + instr VCX.
- **Global:** `set(calleVcx)`, destroy instr VCX.

Calling **`*End()`** without a matching **`*Begin()`** on the same call → PANIC **`executor.end.noframe`** (not a silent no-op).

### PANIC unwind

If the body PANICs before **`*End()`** (or before the FlatL executor block finishes **`V.destroy`**), the VM **drains** open executor state **LIFO** **before** jumping to `#panic.*` / `#panic.any`.

- On this panic path, **`copyCallReturn` is not run** (only on happy `*End()`).
- **`#ne_executor.*`:** native **`E.*Begin/End`** frames (`flat_executor_panic_drain`).
- **`#fl_executor.*`:** pending **instr VCX** on the VM call stack (`OP_CALL_FUNCTION`) — same teardown as panic `*End`, without changing `blocks/executor.fl`.

## Benchmarks (`#ne_` vs `#fl_`)

Pair programs live under **`test/cases/bench/004`–`011`**; case **`012`** and **`sh test/bench/compare_ne_fl_executor.sh .`** print timings on stderr (`flvm --time`, default **`FLAT_NE_FL_BENCH_ROUNDS=3`**, minimum **run** ms per variant).

| Scenario (`compare_ne_fl_executor.sh`) | What it measures |
|----------------------------------------|------------------|
| **loop_unique** | **`004`/`005`**: 20 000× `inc` in a `for`, unique executor |
| **nested_triple** | **`006`/`007`**: 8 000× triple-nested `def` chain (unique) |
| **mixed_global_unique** | **`008`/`009`**: unique caller → global callee, 12 000× |
| **nested_two_inner** | **`010`/`011`**: unique `outer` with two `inner` calls, 6 000× |

All pairs use **`func:callVM 1`** so the comparison is dominated by **executor block cost** (FlatL **`V.*`** in `#fl_executor.*` vs natives **`E.*`** in `#ne_executor.*`), not call-site desugar.

**Reading stderr:** `ne_vs_fl_pct` is **`ne_ms × 100 / fl_ms`** — **ne runtime as a percentage of fl**, not “% faster”. Speedup ≈ **`fl_ms / ne_ms`**.

**Reference snapshot (informational, not a CI gate — rerun on your machine):**

Measured 2026-11-17 on a Linux dev host; `compare_ne_fl_executor.sh .`, 3 rounds, min `run_ms`.

| Scenario | `fl_ms` | `ne_ms` | ne as % of fl | ne ≈ speedup vs fl |
|----------|--------:|--------:|--------------:|-------------------:|
| loop_unique | 2218 | 581 | 26% | ~3.8× |
| nested_triple | 2647 | 601 | 23% | ~4.4× |
| mixed_global_unique | 2095 | 644 | 31% | ~3.3× |
| nested_two_inner | 1988 | 474 | 24% | ~4.2× |

Expect roughly **3–5×** lower run time for **`#ne_`** on hot **`def`** loops; absolute milliseconds vary by CPU and load. Optional gate: **`FLAT_NE_FL_REQUIRE_FASTER=1`** with **`FLAT_NE_FL_BENCH_SLACK`** (see [test/README.md](../../test/README.md)).

## Relation to `vcx`

The module calls **`flat_vcx_*` internally**; user code in executors should use **`E.*` only**, not reimplement `V.callInstr` / `varsCopyByNames` for scope setup.

Entry programs with **`def`** and default **`func:callVM 0`** still need **`use native vcx with V`** in **that entry file** — desugar injects **`V.*`** into caller blocks there (see [USER_FUNCTIONS.md](../USER_FUNCTIONS.md) § `use native vcx`, [vcx.md](./vcx.md) § User functions). With **`func:callVM 1`**, entry often omits `vcx`. The **`native-exec.fl`** unit only imports **`executor`**; it does not replace the entry declaration when strategy A applies.

## See also

- [USER_FUNCTIONS.md](../USER_FUNCTIONS.md) — `def`, `func:executor`, unique vs global
- [vcx.md](./vcx.md) — underlying context primitives
- `blocks/executor.fl` — reference FlatL implementation
- `blocks/native-exec.fl` — unique + global scopes (phase 1)