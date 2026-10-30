# Tests

Project documentation is in **English** — see [docs/README.md](../docs/README.md).

Automated checks live under **`cases/`** — one directory per case:

```text
cases/<suite>/<id>_<name>/case.toml
```

Run from repo root:

```bash
make test                           # flc + flvm (default)
make test-flatl                     # same 101 cases; pipeline [flatl] in case.toml
bin/run_tests <suite> -v          # e.g. flatl, json
bin/run_tests <suite>[id-range] -v
bin/run_tests --flatl             # same as make test-flatl (after build)
bin/run_tests -w 40               # wrap progress dots (default 40)
```

See [docs/TOOLCHAIN.md](../docs/TOOLCHAIN.md) for filters, `case.toml` fields, and **portability** (Windows vs WSL, shell vs `io_generate_tests`).

### JSON / collection I/O (`cases/json/`)

Tests for `json.*`, `io.in_list` / `in_object` / `in_bools`, `null`, simple-list rules.  
Guide: [docs/LIST_OBJECT.md](../docs/LIST_OBJECT.md).

```bash
bin/run_tests json -v
make json_generate_tests   # regenerate from src/json_generate_tests/main.c
```

Catalog: **`src/json_generate_tests/main.c`** (40 cases). Regenerating rewrites `case.toml` without **`[flatl]`** — restore those blocks for **`make test-flatl`** ([TOOLCHAIN.md](../docs/TOOLCHAIN.md#run_tests)).

### `flatl` driver (`cases/flatl/`)

Smoke tests for **`bin/flatl`** (`compile`, `run`, `crun`). **`make test`** uses root **`flc`/`flvm`**; **`make test-flatl`** uses **`[flatl]`** on all pipeline cases (plus these six direct checks).

```bash
bin/run_tests flatl -v
```

See [docs/TOOLCHAIN.md](../docs/TOOLCHAIN.md#flatl-driver-optional-ux). Pipeline cases include an optional **`[flatl]`** block (`crun` or `compile`+`run`) used by **`make test-flatl`**.

### Optional natives (`cases/native/`)

`use native math.trig with trig`, manifest in `.fb`, lazy plugin load, alias/canonical calls, link-time union, compile errors. Docs: [docs/natives/README.md](../docs/natives/README.md). Requires **`bin/libflat_math_trig.so`** on Unix (`make all`).

```bash
bin/run_tests native -v
```

### Portability (`cases/port/`)

Runs **`bin/port_check`** (self-test for **`flat_str`**). Rules: [docs/PORTABILITY.md](../docs/PORTABILITY.md).

```bash
bin/run_tests port -v
bin/port_check
```

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

Regeneration overwrites **`case.toml`** without **`[flatl]`**. Re-add **`[flatl]`** (`crun` or `compile`+`run`) for pipelines that need **`make test-flatl`** — see [TOOLCHAIN.md — `[flatl]` section](../docs/TOOLCHAIN.md#run_tests).

This builds **`bin/io_generate_tests`** (C11, no Node/Bash logic) and rewrites `cases/io/NNN_*/program.fl` and `case.toml`.

Optional: `test/cases/io/gen_cases.sh` — thin Bash wrapper that execs `bin/io_generate_tests`.

### Windows note

- **Regenerate stubs:** only a C compiler + `make io_generate_tests` (or compile `io_generate_tests` yourself).
- **Run the full suite:** `run_tests` executes shell snippets in `run = "printf … | flvm …"` — use **WSL** or see [TOOLCHAIN.md — Portability](../docs/TOOLCHAIN.md#portability-windows-shell-and-io-tests).
- **Run programs:** `flc` / `flvm` work like any native binaries; redirect stdin manually if you skip `run_tests`.
