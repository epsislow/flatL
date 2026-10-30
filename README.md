# flatL (Flat Language)

**flatL** is a label-driven language built on a **flat graph** instead of nested scopes. Programs are collections of linear **basic blocks** linked by explicit control flow: guards, structural backtracking, `call`/`return`, and isolated panic handlers. The toolchain compiles human-readable source (`.fl`) into packed bytecode (`.fb`) executed by a small virtual machine (**flvm**).

> FGIR (Flat Graph Intermediate Representation) spec v1.0 · Binary & VM spec v1.1  
> **Documentation is in English** — index: [docs/README.md](docs/README.md) · offline site: **`make docs`** → open [doc_site/index.html](doc_site/index.html)

## Why flatL?

Traditional block nesting (`if`, `try/catch`, deep indentation) hides where success, failure, and recovery actually go. flatL makes both ends of every branch visible:

- **Guards** — a failed condition jumps to a named backtrack block (`#backtrack.#block.index`), not implicit fall-through.
- **Panics** — infrastructure faults route to `#panic.#block.index` or `#panic.any`, separate from logical `FAIL`.
- **No programmer `goto`** — targets are derived at compile time from block name and instruction index.

## Examples (`.fl`)

### Runnable today (toolchain v0.2)

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

#backtrack.#main.2
  FAIL

#check_balance
  return
```

Types: `true` / `false`, numbers, quoted strings; `call #block`; guards with `==` (and `>=`, etc. on numbers).  
**I/O:** `io.out` / `io.outln` / `io.inln` / `io.in_num` / `io.in_bool`, binding `->` — see [docs/IO.md](docs/IO.md). **Strings:** core **`str.*`** (`len`, `slice`, `concat`, `split`, …) — [docs/natives/str.md](docs/natives/str.md); parsing **`str.to_num`** / **`str.to_bool`** in [IO.md](docs/IO.md). Test helper **`__ref__`**; LIST/OBJECT literals ([LANGUAGE_TYPES.md](docs/LANGUAGE_TYPES.md)).

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

#backtrack.#main.4
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
| `#main`, `#backtrack…`, `OK` / `FAIL` | Yes |
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
| `condition == value` | Guard; failure → `#backtrack.#current.index` |
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

See [docs/TOOLCHAIN.md — flatl](docs/TOOLCHAIN.md#flatl-driver-optional-ux).

## `.fb` binary layout (v1.1)

1. **Magic** — `0x46 0x4C 0x41 0x54` (`FLAT`)
2. **Block directory** — count + entries (block id/hash, byte offset)
3. **Bytecode stream** — opcode + payload (e.g. guard backtrack index)

## Opcodes

| Opcode | Hex | Role |
|--------|-----|------|
| `OP_ASSIGN` | `0x01` | State / assignment |
| `OP_GUARD` | `0x02` | Conditional; jump on false |
| `OP_CALL` | `0x03` | Call block; push return address |
| `OP_RETURN` | `0x04` | Pop call stack |
| `OP_FOREACH` | `0x05` | Collection iteration |
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

**Cross-platform:** `flc` and `flvm` are portable C; **`make test`** uses `run_tests`, which runs pipeline commands through a **POSIX shell** (e.g. `printf … | flvm`). On Windows, use **WSL** for the full suite, or compile/run bytecode manually. I/O test files under `test/cases/io/` are committed — regenerate only when changing the catalog: `make io_generate_tests` (`bin/io_generate_tests`, no Bash). Details: [docs/TOOLCHAIN.md — Portability](docs/TOOLCHAIN.md#portability-windows-shell-and-io-tests) · [test/README.md](test/README.md).

**Docs:** [docs/README.md](docs/README.md) · [TOOLCHAIN.md](docs/TOOLCHAIN.md) · [SPECIFICATION.md](docs/SPECIFICATION.md) · [LANGUAGE_TYPES.md](docs/LANGUAGE_TYPES.md) · [IO.md](docs/IO.md)

## Roadmap

- [x] Toolchain skeleton + C test runner (`run_tests`)
- [x] `flvm` — opcodes v0.1 (OK/FAIL/GUARD/ASSIGN/CALL/RETURN/FOREACH/PANIC)
- [x] `flc` — parse `.fl` → FGIR, `--dump-ir`, emit `.fb`
- [x] E2E tests (`make test`)
- [x] Runtime values v0.2 — STRING REF pool, NUMBER, BOOL ([LANGUAGE_TYPES.md](docs/LANGUAGE_TYPES.md))
- [ ] Native std libraries (`str`, `kv`, …) — no VM built-in functions
- [ ] Full expression language + infrastructure panic from natives
- [ ] Optional dynamic modules (`.so`)

## License

MIT — see [LICENSE](LICENSE).

## Author

[epsislow](https://github.com/epsislow)
