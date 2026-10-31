# fleditor examples

Curated `.fl` samples shipped with the browser playground. Paths in the editor project are prefixed with `examples/` (e.g. `examples/hello.fl`).

| File | What it shows |
|------|----------------|
| `hello.fl` | Minimal `#main` + `io.outln` |
| `io_inln.fl` | `io.inln` prompt (modal stdin) |
| `use/main.fl` + `use/lib/helper.fl` | Multi-file `use "…" as #Prefix` |
| `native_list.fl` | `use native list` (static-linked in WASM) |

**Reset project** reloads `main.fl` plus all files listed in `manifest.json`.

These are teaching samples, not the full `test/cases/` CI corpus.
