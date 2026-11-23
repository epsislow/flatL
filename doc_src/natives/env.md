# Native module `env`

Read **environment variables** and **program argv**. **Optional** — declare before use:

```text
use native env
```

Values are **STRING** only. There is **no** `env.set` in the language: configure the host (`--env`, process env, `case.toml` `[env]`, or FlEditor **Settings → Env**).

| Host | How `env.get` / `env.has` see values |
|------|--------------------------------------|
| `flvm` / `flatl` | Process environment **plus** `--env KEY=VAL` overrides (override wins) |
| Test runner | Same; `[env]` in `case.toml` injects process env for the child |
| FlEditor (WASM) | Settings → Env map only (no OS process env) |

Program args (`env.args` / `env.argc`) come from CLI tokens after `--` (or after the `.fb` / `.fl` path), or from FlEditor **Program args**.

Recoverable failures → **PANIC** with reason **`env.<fn>…`** unless the line has **`?`** / **`~`** ([SPECIFICATION.md](../SPECIFICATION.md) §1.2.2).

## Functions

| Call | Returns | Panic (no modifier) |
|------|---------|---------------------|
| `env.has(name)` | BOOL | **`env.has.args`** — bad / empty name |
| `env.get(name)` | STRING | **`env.get.missing`** if key absent; **`env.get.args`** on bad name |
| `env.get(name, default)` | STRING | Value or **default** (STRING); **no** panic on missing |
| `env.argc()` | NUMBER | — |
| `env.args()` | LIST of STRING | **`env.args.args`** if call has arguments; **`env.args.memory`** on alloc failure |

### `env.has`

```text
use native env

#main
  io.outln(env.has('PATH'))
  io.outln(env.has('FLAT_ENV_DOES_NOT_EXIST'))
  OK
```

### `env.get`

```text
use native env

#main
  # panic if missing:
  url = env.get('API_URL')
  # or default:
  mode = env.get('MODE', 'dev')
  io.outln(url)
  io.outln(mode)
  OK
```

CLI:

```bash
bin/flvm --env API_URL=https://example.test --env MODE=prod out.fb
bin/flatl crun --env API_URL=https://example.test program.fl
```

### `env.argc` / `env.args`

```text
use native env
use native list

#main
  io.outln(env.argc())
  xs = env.args()
  io.outln(list.get(xs, 0))
  OK
```

```bash
bin/flvm out.fb -- input.txt --flag
bin/flatl crun program.fl -- input.txt --flag
```

`env.args` is **not** the list of environment variable names — it is the program’s positional argv.

## Test runner `[env]`

```toml
[env]
API_URL = "https://example.test"
TOKEN = "secret"
```

The runner exports these into the child process environment so `env.get` sees them (separate from putting `--env` on the `run=` line).

## FlEditor

**Settings → Env** (global `localStorage` key `flatl-fleditor-env-prefs`): key/value list, **sensitive** (hide value + Copy), **Program args**, injected at **Run**. Details: [FLEDITOR.md](../FLEDITOR.md) § Settings.

## Related

- Build toggle: `make … env=0` → `-DFLAT_WITH_ENV=0`
- Implementation: `src/lib/mod_env.c`, `src/common/flat_env.c`
