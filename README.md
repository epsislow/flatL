# flatL (Flat Language)

**flatL** is a label-driven language built on a **flat graph** instead of nested scopes. Programs are collections of linear **basic blocks** linked by explicit control flow: guards, structural backtracking, `call`/`return`, and isolated panic handlers. The toolchain compiles human-readable source (`.fl`) into packed bytecode (`.fb`) executed by a small virtual machine (**flvm**).

> FGIR (Flat Graph Intermediate Representation) spec v1.0 · Binary & VM spec v1.1  
> **Documentation is in English** — index: [docs/README.md](docs/README.md)

## Why flatL?

Traditional block nesting (`if`, `try/catch`, deep indentation) hides where success, failure, and recovery actually go. flatL makes both ends of every branch visible:

- **Guards** — a failed condition jumps to a named backtrack block (`#backtrack.#block.index`), not implicit fall-through.
- **Panics** — infrastructure faults route to `#panic.#block.index` or `#panic.any`, separate from logical `FAIL`.
- **No programmer `goto`** — targets are derived at compile time from block name and instruction index.

## Example (`.fl`)

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
  balance = kget(u_key)
  result = calc(balance >= amount)
  return result
```

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
```

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
make test         # compile + run test/cases/
bin/run_tests -h
```

**Docs:** [docs/README.md](docs/README.md) · [TOOLCHAIN.md](docs/TOOLCHAIN.md) · [SPECIFICATION.md](docs/SPECIFICATION.md) · [LANGUAGE_TYPES.md](docs/LANGUAGE_TYPES.md)

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
