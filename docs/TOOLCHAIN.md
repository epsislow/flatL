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

## Building on Windows (Command Prompt)

**`make` is not available in plain `cmd.exe`.** Pick one of these:

### Option A — WSL (recommended)

Install [WSL](https://learn.microsoft.com/en-us/windows/wsl/), open **Ubuntu**, clone the repo inside Linux home (e.g. `~/flatL`), then:

```bash
sudo apt update && sudo apt install -y build-essential
cd ~/flatL && make && make test
```

Same workflow as Linux; pipeline tests behave like CI.

### Option B — MSYS2 / MinGW (`make` works in that shell)

1. Install [MSYS2](https://www.msys2.org/).
2. Open **MSYS2 UCRT64** (or MINGW64) and install tools:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc make
```

3. From the repo root (path like `/c/Users/you/flatL`):

```bash
make tools
```

Uses **`flat_port_posix.c`** in that environment (POSIX API available in MSYS).

### Option C — Native `cmd` + MSVC (`cl`)

1. Open **“x64 Native Tools Command Prompt for VS”** (Visual Studio Build Tools).
2. `cd` to the repo root.
3. Create output folder: `mkdir bin`
4. Build **compiler + VM** (one line each; run from repo root):

```bat
cl /nologo /W3 /std:c11 /Iinclude /Fe:bin\flc.exe src\flc\main.c src\common\parse.c src\common\compile.c src\common\flat_str.c src\lib\native_registry.c src\lib\native_util.c src\common\fb_format.c

cl /nologo /W3 /std:c11 /Iinclude /Fe:bin\flvm.exe src\flvm\main.c src\common\fb_format.c src\common\vm.c src\common\value_pool.c src\common\value_collection.c src\common\value_ops.c src\common\value_parse.c src\common\value_format.c src\common\value_io.c src\common\value_json.c src\common\flat_str.c src\lib\native_registry.c src\lib\native_util.c src\lib\native_dispatch.c src\lib\mod_io.c src\lib\mod_str.c src\lib\mod_json.c
```

Optional **`mkfb`**: `cl ... /Fe:bin\mkfb.exe src\mkfb\main.c src\common\fb_format.c`

**`run_tests`** on native Windows also needs `flat_port_win.c` and the runner sources (see `Makefile` `RUN_TESTS_SRCS`). Full suite still relies on shell pipelines in `case.toml`; WSL is easier for **`make test`**.

Usage:

```bat
bin\flc.exe -o out.fb examples\program.fl
bin\flvm.exe out.fb
```

**Tests fail with `flc` or `flvm` exit `-1073741571`:** Windows **stack overflow** (`0xC00000FD`). Typical causes: old `flc` with **`FgirProgram` on the stack**, or JSON **`parse_simple_list`** token buffer on the stack (~4 MiB). Sync and rebuild `make clean && make all`. Quick check: `bin/flc.exe -o out.fb test/cases/e2e/001_hello_ok/program.fl` → **0**; json cases should no longer crash the VM.

**Exit 127, no version line printed:** (1) Invoke the file you see in `ls`: **`./bin/flc.exe --version`** if only `flc.exe` exists. (2) If still silent/127, the binary may be an **old build** that kept **`FgirProgram` on the stack** (Windows stack overflow before any output). Sync `src/flc/main.c` (heap IR + early `--version`), then `make clean && make flc`. Confirm from cmd: `cmd //c D:\\wamp64\\www\\flatL\\bin\\flc.exe --version`. `ldd bin/flc.exe` should show only system DLLs (no missing `libgcc`).

**Slow `run_tests.exe`:** each case runs **two** shell subprocesses (`flc` then `flvm`); on Windows `cmd /c` is slower than Linux `sh`. Use filters while developing: `bin\run_tests.exe e2e[1] -v`.

**`-v` from MSYS2 UCRT64:** `cd /d/wamp64/www/flatL && ./bin/run_tests.exe -v` (same as from cmd; avoid relying on a non-existent `make tests` target — use **`make test`**).

### Option D — Native `cmd` + GCC (MinGW in `PATH`)

If `gcc` is on `PATH` (e.g. from MinGW, not MSYS shell):

```bat
set OS=Windows_NT
mkdir bin
gcc -Wall -Wextra -std=c11 -Iinclude -o bin/flc.exe src/flc/main.c src/common/parse.c src/common/compile.c src/common/flat_str.c src/lib/native_registry.c src/lib/native_util.c src/common/fb_format.c
```

Use forward slashes or escaped backslashes; link **`src\port\flat_port_win.c`** for tools that need `flat_port` (`run_tests`, generators). For **`flvm`**, use the same source list as in the `Makefile` `VM_SRC` rule (no `flat_port`).

---

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

**`flc`**, **`flvm`**, and **`mkfb`** are **C11** only (no direct OS headers). String splitting uses **`flat_str`**; OS calls go through **`flat_port`** (`src/port/flat_port_posix.c` or `flat_port_win.c` — see [PORTABILITY.md](./PORTABILITY.md)).

| Compiler / OS | `PORT_SRC` (Makefile) |
|---------------|------------------------|
| GCC, Clang (Linux, WSL, Termux) | `flat_port_posix.c` |
| MSVC (`OS=Windows_NT`) | `flat_port_win.c` |

**`bin/port_check`** — small self-test for `flat_str`; case **`port/001_str_util`**.

### What still assumes a shell today

| Component | Dependency | Why |
|-----------|------------|-----|
| **`bin/run_tests`** | Shell in `run = "…"` | Pipeline cases use `flat_port_shell_capture` (`popen` / `_popen` + `cmd /c` on Windows) |
| **`bin/run_tests`** | Writable temp | `flat_port_make_temp_dir` — **`TMPDIR`**, **`TMP`**, **`TEMP`**, **`$PREFIX/tmp`** (Termux), **`$HOME/tmp`**, `/tmp`, **`{repo}/.flatl_run_tmp`** |
| **`make test`** | **make** + C compiler | Dev loop; use **WSL** or native MSVC build of the same sources |

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
