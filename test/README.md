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

See [docs/TOOLCHAIN.md](../docs/TOOLCHAIN.md) for filters and `case.toml` fields.
