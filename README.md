# flatL (Flat Language)

**flatL** is a label-driven language built on a **flat graph** instead of nested scopes. Programs are collections of linear **basic blocks** linked by explicit control flow: guards (`#or` handlers), `call`/`return`, and isolated panic handlers. The toolchain compiles human-readable source (`.fl`) into packed bytecode (`.fb`) executed by a small virtual machine (**flvm**).

> FGIR (Flat Graph Intermediate Representation) spec v1.0 · Binary & VM spec v1.1  
> **Documentation is in English** — index: [doc_src/README.md](doc_src/README.md) · offline site: **`make docs`** + **`make fleditor-serve`** → [docs/doc_site/](docs/doc_site/index.html)

## Why flatL?

Traditional block nesting (`if`, `try/catch`, deep indentation) hides where success, failure, and recovery actually go. flatL makes both ends of every branch visible:

- **Guards** — a failed condition jumps to an **`#or`** handler block (`#or.#block.index` or `#or.#block.lineLabel`), not implicit fall-through. Source lines may end with `#.lineLabel` after the instruction.
- **Panics** — infrastructure faults route to `#panic.#block.index` or `#panic.any`, separate from logical `FAIL`.
- **No programmer `goto`** — targets are derived at compile time from block name and instruction index.

## Examples (`.fl`)

### Runnable today (toolchain v0.4)

Parses, compiles, and runs with current `flc` / `flvm` (see `make test`):

```text
#main
  amount = 100
  has_funds = true
  has_funds == true
  call #check_balance
  payment_status = 'SUCCESS'
  payment_status == 'SUCCESS'
  OK

#or.#main.2
  FAIL

#check_balance
  return
```

Types: `true` / `false`, numbers, quoted strings; `call #block`; guards with `==` (and `>=`, etc. on numbers).  
**I/O:** `io.out` / `io.outln` / `io.inln` / `io.in_num` / `io.in_bool`, binding `->` — see [doc_src/IO.md](doc_src/IO.md). **Strings:** core **`str.*`** (`len`, `slice`, `concat`, `split`, …) — [doc_src/natives/str.md](doc_src/natives/str.md); parsing **`str.to_num`** / **`str.to_bool`** in [IO.md](doc_src/IO.md). Test helper **`__ref__`**; LIST/OBJECT literals ([LANGUAGE_TYPES.md](doc_src/LANGUAGE_TYPES.md)).

Minimal sample: [examples/hello.fl](examples/hello.fl).

### Target syntax (illustrative — not all implemented yet)

Shows the **intended** shape once native libraries and full expressions land (`form.*`, `str.*`, `kv.*`, `end.*`, call arguments, block parameters):

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

#check_balance(userId, amount)
  u_key = str.concat('user:wallet:', userId)
  balance = kv.get(u_key)
  result = calc(balance >= amount)
  return result
```

| Feature in example | v0.2 status |
|--------------------|-------------|
| `#main`, `#or…`, `OK` / `FAIL` | Yes |
| Guards `==`, `>=` on vars | Yes |
| `call #block` (no args) | Yes |
| `call #block(a, b) -> out` | Parsed; **args / return binding not executed** |
| `#block(param, …)` headers | Parsed; **params not bound at runtime** |
| `form.userId`, `str.*`, `kv.*`, `end.*`, `calc(...)` | **No** — planned via native libs |
| `return expr` | `return` only (no value) |

Lexical rules (summary):

| Construct | Meaning |
|-----------|---------|
| `#name` | Block declaration |
| Sequential lines | Implicit AND — run top to bottom |
| `call #block(args) -> var` | Sub-graph invocation with output binding |
| `condition == value` | Guard; failure → `#or.#current.index` |
| `OK` / `FAIL` | Terminal success / logical failure |
| `foreach(coll, item, '#loop')` | Iteration via sub-block, no nested loop syntax |

## Toolchain pipeline

```text
 program.fl  ──►  flc  ──►  program.fb  ──►  flvm  ──►  execution
 (source)              (bytecode)              (VM loop)
```

| Extension | Role |
|-----------|------|
| `.fl` | flatL source — indent-delimited graph DSL |
| `.fb` | **flat Bytecodes** — magic header, block directory, opcode stream |

```bash
./flc program.fl -o program.fb
./flvm program.fb
# optional driver (same engine, in-process):
./flatl crun program.fl
```

See [doc_src/TOOLCHAIN.md — flatl](doc_src/TOOLCHAIN.md#flatl-driver-optional-ux).

## `.fb` binary layout (v1.1)

1. **Magic** — `0x46 0x4C 0x41 0x54` (`FLAT`)
2. **Block directory** — count + entries (block id/hash, byte offset)
3. **Bytecode stream** — opcode + payload (e.g. guard `#or` handler IP)

## Opcodes

| Opcode | Hex | Role |
|--------|-----|------|
| `OP_ASSIGN` | `0x01` | State / assignment |
| `OP_GUARD` | `0x02` | Conditional; jump on false |
| `OP_CALL` | `0x03` | Call block; push return address |
| `OP_CALL_BLOCK` | `0x0E` | Call block by runtime id (`call block expr` in source) |
| `OP_JUMP` | `0x0D` | Jump to block; no call frame; clears loop frames |
| `OP_RETURN` | `0x04` | Pop call stack |
| `OP_FOREACH` | `0x05` | Collection iteration |
| `OP_FOR` | `0x0C` | Numeric `for(ntimes, i, '#block')` |
| `OP_OK` | `0x06` | Success exit |
| `OP_FAIL` | `0x07` | Logical failure exit |
| `OP_PANIC` | `0x08` | Route to panic handler |

Reference implementation direction: **C** — static IR structs, linear memory layout, switch-dispatch VM (no string parsing at runtime).

## Build & test (WSL)

```bash
make help         # list make targets
make              # compile bin/ (no tests run)
make test         # compile + run test/cases/ (flc + flvm)
make test-flatl   # same cases via [flatl] in case.toml
bin/run_tests -h
```

**Testing policy:** Every automated check in this repo is **must-pass**. There are no optional, skipped, or “nice-to-have” tiers in the runner (`bin/run_tests` has no xfail/skip flags). **`make test`** and **`make test-flatl`** must finish with **0 failed** — each `test/cases/*/…/case.toml` that is discovered counts. Additional checks (e.g. **`make fleditor-smoke`**, **`make fleditor-wasm-smoke`**, and related fleditor targets) are separate Makefile targets but follow the same rule: when you run them, they must pass; do not merge or ship with known failures. New suites and cases should be written as required gates, not as best-effort extras.

**Cross-platform:** `flc` and `flvm` are portable C; **`make test`** uses `run_tests`, which runs pipeline commands through a **POSIX shell** (e.g. `printf … | flvm`). On Windows, use **WSL** for the full suite, or compile/run bytecode manually. I/O test files under `test/cases/io/` are committed — regenerate only when changing the catalog: `make io_generate_tests` (`bin/io_generate_tests`, no Bash). Details: [doc_src/TOOLCHAIN.md — Portability](doc_src/TOOLCHAIN.md#portability-windows-shell-and-io-tests) · [test/README.md](test/README.md).

**Docs:** [doc_src/README.md](doc_src/README.md) · [TOOLCHAIN.md](doc_src/TOOLCHAIN.md) · [SPECIFICATION.md](doc_src/SPECIFICATION.md) · [LANGUAGE_TYPES.md](doc_src/LANGUAGE_TYPES.md) · [IO.md](doc_src/IO.md)

**Browser playground:** [docs/fleditor_site/](docs/fleditor_site/) — WASM compile/run in the browser; see [doc_src/FLEDITOR.md](doc_src/FLEDITOR.md).

## Roadmap

- [x] Toolchain skeleton + C test runner (`run_tests`)
- [x] `flvm` — opcodes v0.1 (OK/FAIL/GUARD/ASSIGN/CALL/RETURN/FOREACH/PANIC)
- [x] `flc` — parse `.fl` → FGIR, `--dump-ir`, emit `.fb`
- [x] E2E tests (`make test`)
- [x] Runtime values v0.2 — STRING REF pool, NUMBER, BOOL ([LANGUAGE_TYPES.md](doc_src/LANGUAGE_TYPES.md))
- [ ] Native std libraries (`str`, `kv`, …) — no VM built-in functions
- [ ] Full expression language + infrastructure panic from natives
- [ ] Optional dynamic modules (`.so`)

## License

MIT — see [LICENSE](LICENSE).

## Author

[epsislow](https://github.com/epsislow)
