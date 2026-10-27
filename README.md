# flatL (Flat Language)

**flatL** is a label-driven language built on a **flat graph** instead of nested scopes. Programs are collections of linear **basic blocks** linked by explicit control flow: guards, structural backtracking, `call`/`return`, and isolated panic handlers. The toolchain compiles human-readable source (`.fl`) into packed bytecode (`.fb`) executed by a small virtual machine (**flvm**).

> FGIR (Flat Graph Intermediate Representation) spec v1.0 · Binary & VM spec v1.1

## Why flatL?

Traditional block nesting (`if`, `try/catch`, deep indentation) hides where success, failure, and recovery actually go. flatL makes both ends of every branch visible:

- **Guards** — a failed condition jumps to a named backtrack block (`#backtrack.#block.index`), not implicit fall-through.
- **Panics** — infrastructure faults route to `#panic.#block.index` or `#panic.any`, separate from logical `FAIL`.
- **No programmer `goto`** — targets are derived at compile time from block name and instruction index.

## Example (`.fl`)

```text
#main
  uid = form.userId
  pret = form.amount
  call #verifica_balanta(uid, pret) -> are_bani
  are_bani == true
  call #proceseaza_plata(uid, pret) -> status_plata
  status_plata == 'SUCCESS'
  end.msg('Comandă finalizată!')
  OK

#backtrack.#main.4
  end.err('Fonduri insuficiente!')
  FAIL

#verifica_balanta(userId, amount)
  u_key = str.concat('user:wallet:', userId)
  balanta = kget(u_key)
  rezultat = calc(balanta >= amount)
  return rezultat
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
make              # bin/flc, bin/flvm, bin/run_tests, bin/mkfb
make test         # declarative cases under test/cases/
bin/run_tests -h
bin/run_tests smoke[1-3] -v
```

**Developer guide (all CLI tools):** [docs/TOOLCHAIN.md](docs/TOOLCHAIN.md)  
**Language & binary format:** [docs/SPECIFICATION.md](docs/SPECIFICATION.md)  
**Types & limits (STRING ref, NUMBER, BOOL):** [docs/LANGUAGE_TYPES.md](docs/LANGUAGE_TYPES.md)

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
