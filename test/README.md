# Tests

Project documentation is in **English** — see [docs/README.md](../docs/README.md).

Automated checks live under **`cases/`** — one directory per case:

```text
cases/<suite>/<id>_<name>/case.toml
```

Run from repo root:

```bash
make test
bin/run_tests <suite>[id-range] -v
```

See [docs/TOOLCHAIN.md](../docs/TOOLCHAIN.md) for filters, `case.toml` fields, and **portability** (Windows vs WSL, shell vs `io_generate_tests`).

## I/O suite (`cases/io/`)

Thirty pipeline tests for `io.*` / `str.to_*` (stdin simulation, stdout checks). Spec: [docs/IO.md](../docs/IO.md).

| Goal | Command |
|------|---------|
| Run all tests (including I/O) | `make test` or `bin/run_tests io` |
| Run one suite | `bin/run_tests io -v` |

**Fixtures are in git** — you do not need to regenerate to run tests.

### Regenerating I/O cases (maintainers)

When you add or change scenarios, edit **`src/io_generate_tests/main.c`**, then:

```bash
make io_generate_tests
```

This builds **`bin/io_generate_tests`** (C11, no Node/Bash logic) and rewrites `cases/io/NNN_*/program.fl` and `case.toml`.

Optional: `test/cases/io/gen_cases.sh` — thin Bash wrapper that execs `bin/io_generate_tests`.

### Windows note

- **Regenerate stubs:** only a C compiler + `make io_generate_tests` (or compile `io_generate_tests` yourself).
- **Run the full suite:** `run_tests` executes shell snippets in `run = "printf … | flvm …"` — use **WSL** or see [TOOLCHAIN.md — Portability](../docs/TOOLCHAIN.md#portability-windows-shell-and-io-tests).
- **Run programs:** `flc` / `flvm` work like any native binaries; redirect stdin manually if you skip `run_tests`.
