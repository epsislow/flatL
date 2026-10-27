# flatL toolchain — build & CLI

Documentation index: [README.md](./README.md) (English).

Run all commands from the **repository root** (directory containing `Makefile` and `include/flat.h`).

Built programs are written to **`bin/`** (gitignored). Requires **gcc**, **make**, and a POSIX shell (WSL/Linux).

## Build

```bash
make help         # list all make targets (recommended first run)
make              # compile only: flc, flvm, mkfb, run_tests + fixtures (no tests run)
make tools        # flc, flvm, mkfb only
make test         # compile, then ./bin/run_tests
make clean        # remove bin/ and generated min_ok.fb copies
```

| Target | Effect |
|--------|--------|
| `make` / `make all` | Build all `bin/*` + regenerate `min_ok.fb` fixtures |
| `make tools` | `flc`, `flvm`, `mkfb` only |
| `make flvm` / `flc` / `mkfb` / `run_tests` | Single binary |
| `make fixtures` | Run `mkfb` for test/example `.fb` |
| `make test` | **Runs** the test suite (not part of plain `make`) |

| Binary | Role |
|--------|------|
| `bin/flc` | Compiler: `.fl` → FGIR → `.fb` |
| `bin/flvm` | Virtual machine: runs `.fb` |
| `bin/run_tests` | Discovers and runs tests under `test/cases/` |
| `bin/mkfb` | Emits minimal `.fb` fixtures (`--min-ok`) |

## `flvm`

```bash
bin/flvm --version
bin/flvm program.fb
```

| Exit | Meaning |
|------|---------|
| 0 | Success (`OP_OK`) |
| 1 | Load/bytecode error, unknown opcode |
| 2 | Logical failure (`OP_FAIL` or unmapped guard) |

See [SPECIFICATION.md §5](./SPECIFICATION.md#5-virtual-machine-flvm) for opcode behavior.  
Value types (STRING REF, NUMBER, BOOL): [LANGUAGE_TYPES.md](./LANGUAGE_TYPES.md).

## `flc`

```bash
bin/flc --version
bin/flc --dump-ir program.fl    # print FGIR after parse
bin/flc -o out.fb program.fl    # compile to bytecode
```

Parse/compile errors print to stderr and exit **1**.

## `mkfb`

```bash
bin/mkfb --min-ok -o path/to/out.fb
```

`make` also generates `examples/min_ok.fb` and `test/cases/flvm/001_min_ok/min_ok.fb`.

## `run_tests`

C runner — no Node.js. Scans `test/cases/<suite>/<id>_*/case.toml`.

```bash
bin/run_tests
bin/run_tests -h
bin/run_tests e2e[1-7] flc -v -e
```

### Filters

- `suite` — all cases in suite
- `suite[1]` / `suite[1-3,5]` — by `id` in `case.toml`

### Placeholders

| Token | Expands to |
|-------|------------|
| `{root}` | Repository root |
| `{case}` | Directory containing `case.toml` |
| `{tmp}` | Temp dir (pipeline only) |
| `{src}` | Path to `program.fl` (pipeline) |

### Case kinds

| kind | Fields |
|------|--------|
| `files` | `check = "path"` (repeatable) |
| `exec` | `cmd`, `expect_exit`, optional `stdout_contains` |
| `pipeline` | `source`, `compile`, `run`, `expect_exit` |

Example pipeline (`test/cases/e2e/001_hello_ok/case.toml`):

```toml
kind = "pipeline"
source = "program.fl"
compile = "{root}/bin/flc -o {tmp}/out.fb {src}"
run = "{root}/bin/flvm {tmp}/out.fb"
expect_exit = 0
```

## Related docs

- [SPECIFICATION.md](./SPECIFICATION.md) — language, `.fb` layout, VM semantics
- [../test/README.md](../test/README.md) — test layout
- [../README.md](../README.md) — overview
