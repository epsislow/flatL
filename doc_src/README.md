# flatL documentation (sources)

**Language:** all files under **`doc_src/`** (and the root [README.md](../README.md), [test/README.md](../test/README.md)) must be written in **English**. Source code and comments are **English** too. Cursor **plans** under [`.cursor/plans/`](../.cursor/plans/) may use Romanian.

**Offline browser docs:** from the repo root, run **`make docs`**, then open via **`make fleditor-serve`** → `http://127.0.0.1:8765/doc_site/` (output in **`docs/doc_site/`**). The table of contents is driven by **[index.json](./index.json)** — add a section there when you add a new normative doc page.

**Browser playground (fleditor):** see **[FLEDITOR.md](./FLEDITOR.md)** — site in **`docs/fleditor_site/`**; full stack rebuild: **`make rebuild`** (includes **`make fleditor-wasm`**).

**GitHub Pages:** publish folder **`docs/`** on branch `main` (see FLEDITOR.md).
