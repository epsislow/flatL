# flatL toolchain — build & CLI

Documentation index: [README.md](./README.md) (English).

Run all commands from the **repository root** (directory containing `Makefile` and `include/flat.h`).

Built programs are written to **`bin/`** (gitignored). Requires **gcc**, **make**, and a POSIX shell (WSL/Linux) for the default **`make test`** workflow — see [Portability](#portability-windows-shell-and-io-tests) below.

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
| `make io_generate_tests` | Build `io_generate_tests` and rewrite `test/cases/io/*` from C catalog |
| `make test` | **Runs** the test suite (not part of plain `make`) |

| Binary | Role |
|--------|------|
| `bin/flc` | Compiler: `.fl` → FGIR → `.fb` |
| `bin/flvm` | Virtual machine: runs `.fb` |
| `bin/run_tests` | Discovers and runs tests under `test/cases/` |
| `bin/mkfb` | Emits minimal `.fb` fixtures (`--min-ok`) |
| `bin/io_generate_tests` | Regenerates I/O test stubs under `test/cases/io/` (portable C; no Bash) |

## `flvm`

```bash
bin/flvm --version
bin/flvm program.fb
bin/flvm --trace program.fb          # opcode log → stderr
bin/flvm --trace-vars program.fb     # opcode + variables → stderr
```

Program output uses **`io.outln(...)`** and **`io.outvar(name)`** on **stdout** (see [LANGUAGE_TYPES.md §8](./LANGUAGE_TYPES.md#8-io-output--debug)).

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

Pipeline cases may set `stdout_contains` / `stderr_contains`. The runner merges stderr into captured output when executing `run` (`2>&1`), so substring checks usually target the combined stream.

---

## Portability (Windows, shell, and I/O tests)

### What is cross-platform

**`flc`**, **`flvm`**, and **`mkfb`** are plain C11 programs. Once compiled for your OS, they do not require Bash or `make` at runtime. You can compile a `.fl` program and run bytecode with redirected stdin/stdout using your shell or IDE — the VM is meant to be embeddable everywhere.

### What still assumes POSIX today

| Component | Dependency | Why |
|-----------|------------|-----|
| **`bin/run_tests`** | POSIX shell (`sh`) | Pipeline cases run the literal `run = "…"` string via `popen()` — often `printf '…' \| bin/flvm …` |
| **`bin/run_tests`** | `/tmp`, `mkdtemp`, `wait` status | Temp dirs for compile-and-run pipeline cases |
| **`make test`** | **make** + **gcc** | Standard Linux/WSL dev loop |

On **Windows without WSL**, you can still build and use **`flc` / `flvm`** manually; running the **full** checked-in suite via `run_tests` matches the Linux/WSL environment documented above.

**Recommended on Windows:** use **WSL** (or Linux/macOS) for `make test` so pipeline `run` lines behave the same as in CI.

### I/O test fixtures — no Bash required to *use* them

The **`test/cases/io/`** tree (30 pipeline cases) is **committed to git**. Cloning the repo gives you `program.fl` + `case.toml` for each case. You only regenerate when you change the catalog.

| Task | Tool | Bash needed? |
|------|------|----------------|
| Run I/O tests (with rest of suite) | `make test` / `bin/run_tests io` | Yes (via `run=` snippets inside `case.toml`) |
| Regenerate I/O stubs after editing catalog | `make io_generate_tests` | **No** — uses `bin/io_generate_tests` only |
| Optional wrapper | `test/cases/io/gen_cases.sh` | Yes — calls `bin/io_generate_tests` if built |

**Source of truth for I/O case definitions:** `src/io_generate_tests/main.c` (same scenarios as the legacy shell generator). Run **`make io_generate_tests`** (builds and runs the tool) or **`bin/io_generate_tests`** after the binary exists.

### Manual I/O check without the test runner

```bash
bin/flc -o /tmp/out.fb program.fl
printf 'line\n' | bin/flvm /tmp/out.fb    # POSIX example; use your OS redirect on Windows
```

See [IO.md §9](./IO.md#9-tests) for the I/O behavior those cases cover.

### Possible future improvement

Native **`stdin = "…"`** (and direct `flvm` spawn with pipes) in `case.toml` would let **`run_tests`** run I/O cases without shell pipelines on Windows. Not implemented yet; pipeline `run` strings remain the portable *format* across Unix-like CI.

---

## Related docs

- [SPECIFICATION.md](./SPECIFICATION.md) — language, `.fb` layout, VM semantics
- [../test/README.md](../test/README.md) — test layout
- [../README.md](../README.md) — overview
