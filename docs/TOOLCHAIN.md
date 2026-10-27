# flatL toolchain — build & CLI

Run all commands from the **repository root** (directory containing `Makefile` and `include/flat.h`).

Built programs are written to **`bin/`** (gitignored). Requires **gcc**, **make**, and a POSIX shell (WSL/Linux).

## Build

```bash
make              # bin/flc, bin/flvm, bin/run_tests, bin/mkfb + .fb fixtures
make clean        # remove bin/ and generated min_ok.fb copies
```

| Binary | Role |
|--------|------|
| `bin/flc` | Compiler: `.fl` → `.fb` (in progress) |
| `bin/flvm` | Virtual machine: runs `.fb` |
| `bin/run_tests` | Discovers and runs declarative tests under `test/cases/` |
| `bin/mkfb` | Dev helper to emit bytecode fixtures |

## `flvm`

```bash
bin/flvm --version
bin/flvm program.fb
```

Exit codes (current):

| Code | Meaning |
|------|---------|
| 0 | `OP_OK` / success |
| 1 | Load error, bad magic, truncated file, unsupported opcode |
| 2 | `OP_FAIL` (logical failure) |

## `flc`

```bash
bin/flc --version
bin/flc -o out.fb program.fl
```

Compilation is still being implemented; see the project plan in `.cursor/plans/flc-flvm.plan.md`.

## `mkfb`

Generates a minimal valid `.fb` (single block, one `OP_OK`) for tests and examples.

```bash
bin/mkfb --min-ok -o path/to/out.fb
```

`make` also writes:

- `examples/min_ok.fb`
- `test/cases/flvm/001_min_ok/min_ok.fb`

## `run_tests`

No Node.js — the runner is C. It scans `test/cases/<suite>/<case>/case.toml`.

```bash
make test                    # make all, then bin/run_tests
bin/run_tests                # all cases
bin/run_tests -h             # help
bin/run_tests smoke[1-3]     # filter by suite and numeric id
bin/run_tests flvm[1] smoke  # multiple filters
bin/run_tests -v             # print failure details
bin/run_tests -e             # progress grouped by suite
```

### Filter syntax

- `suite` — all cases in that suite (directory name under `test/cases/`)
- `suite[1]` — case with `id = 1` in `case.toml`
- `suite[1-3,5]` — id ranges and lists

Placeholders in `case.toml` commands:

- `{root}` — repository root path

### Case kinds (`case.toml`)

| kind | Use |
|------|-----|
| `files` | `check = "relative/path"` must exist under repo root |
| `exec` | `cmd`, `expect_exit`, optional `stdout_contains` / `stderr_contains` |
| `pipeline` | `flc` then `flvm` (planned) |

Full layout and language spec: [SPECIFICATION.md](./SPECIFICATION.md).

## Related docs

- [SPECIFICATION.md](./SPECIFICATION.md) — language, `.fb` format, repo layout
- [../README.md](../README.md) — project overview
