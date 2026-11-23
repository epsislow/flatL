# fleditor examples

Curated `.fl` samples shipped with the browser playground. Paths in the editor project are prefixed with `examples/` (e.g. `examples/hello.fl`).

| File | What it shows |
|------|----------------|
| `hello.fl` | Minimal `#main` + `io.outln` |
| `http_get.fl` | HTTPS `http.*` GET + `transportOk` / `statusOk` (CORS-friendly demo API) |
| `http_post.fl` | HTTPS `http.*` POST + `http.json` + asserts (same demo API) |
| `io_inln.fl` | `io.inln` prompt (modal stdin) |
| `use/main.fl` + `use/lib/helper.fl` | Multi-file `use "…" as #Prefix` |
| `native_list.fl` | `use native list` (static-linked in WASM) |
| `order_flow.fl` + `blocks/executor.fl` | User `def`, executors, `math`/`str`, guards + `#or` (v0.4) |
| `guess_number/guess_number.fl` | stdin loop, relational guards (`<`/`>`), `math.calc`, score |
| `csv_reader/grid_row_build.fl` | `csv.next` + `L.push` → `t16[][]`, `call` + `str.to_num`, fără `getCells` |

**Reset project** reloads `main.fl` plus all files listed in `manifest.json`.

These are teaching samples, not the full `test/cases/` CI corpus.
