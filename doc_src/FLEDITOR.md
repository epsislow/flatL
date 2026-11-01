# fleditor — browser playground (WASM)

**fleditor** lives under **`docs/fleditor_site/`** (same tree as [GitHub Pages](../docs/index.html)): edit `.fl` in the browser, compile and run via **flc + flvm** in WebAssembly. Node is only an optional static server.

## Quick start

1. Build WASM (Linux/WSL/macOS with [Emscripten](https://emscripten.org/) on `PATH`):

   One-time Emscripten (example):

   ```bash
   git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
   cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest
   echo 'source "$HOME/emsdk/emsdk_env.sh"' >> ~/.bashrc
   ```

   Then from the repo:

   ```bash
   source ~/emsdk/emsdk_env.sh   # if not in .bashrc yet
   make fleditor-rebuild
   # or: make fleditor-wasm / ./scripts/build-fleditor-wasm.sh
   ```

2. Build offline docs (optional, for **Docs** link in the editor):

   ```bash
   make docs
   ```

3. Serve the **Pages layout** (`docs/` root):

   ```bash
   make fleditor-serve
   # Port in use: make fleditor-serve-forced
   # or: node docs/fleditor_site/node/serve.js 8765
   ```

   **WSL:** if `make fleditor-serve` says `node: Permission denied`, use `make fleditor-serve NODE=/usr/bin/node`.

4. Open `http://127.0.0.1:8765/` (redirects to **fleditor**) or `http://127.0.0.1:8765/fleditor_site/` and click **Run**.

**Two-file example:** create `lib/greet.fl` with a block (no `#main` in imports), then in `main.fl`:

```text
use "lib/greet.fl" as #Greet_

#main
  call #Greet_hello
  OK
```

Paths in `use "…"` must match the project tree. Rebuild WASM after compiler changes: `make fleditor-rebuild`.

Windows without Emscripten: use prebuilt `docs/fleditor_site/wasm/flatl.js` + `flatl.wasm` from the repo, then `make fleditor-serve`.

## GitHub Pages

Publish from branch **`main`**, folder **`/docs`**. The repo layout matches local serve:

- `docs/index.html` → redirect to `fleditor_site/`
- `docs/fleditor_site/` — editor (+ committed WASM)
- `docs/doc_site/` — run **`make docs`** before push

Enable Pages in repo settings after the bundle is on `main`.

## Layout (under `docs/fleditor_site/`)

| Path | Role |
|------|------|
| `index.html` | Shell UI |
| `css/` | Theme |
| `js/fleditor.js` | Run/Save, project UI, WASM glue, Xterm I/O |
| `js/fleditor-project.js` | Virtual project, localStorage, MEMFS sync |
| `ext/` | Vendored CodeMirror 5 + xterm.js (see `ext/VENDOR.md`) |
| `wasm/` | `flatl.js` + `flatl.wasm` (Emscripten → `Makefile.wasm`) |
| `node/serve.js` | Serves **`docs/`** (editor + doc_site), port **8765**, **127.0.0.1** |
| `examples/` | Curated `.fl` + `manifest.json` |

Refresh vendored UI: `./scripts/vendor-fleditor-ext.sh`

## I/O model

- **Stdout** (`io.out*`) → xterm.js terminal.
- **Stdin** (`io.in*`) → in-page **modal** (`<dialog>`); OK/Cancel via Asyncify polling.
- Project actions use **`<dialog>`** — no native `prompt` / `confirm` / `alert`.
- Empty line on `io.inln` / `io.in_num` / `io.in_bool` → cancel (`#panic.any`), same as desktop [IO.md](IO.md).

## Multi-file project

- **Project panel**, **Run** = open file, implicit save before compile, **Save**, dirty markers, Undo/Redo.
- **MEMFS** under `/fleditor/…` on Run.
- **`localStorage`** key `flatl-fleditor-project`.

### Delete and reset

- **`+fl`** / **`+dir`**, **`×`** delete, **Reset project** loads `main.fl` + `examples/` from disk manifest.
- Toolbar **Docs** → relative **`../doc_site/index.html`**. In docs, **Open fleditor** → **`../fleditor_site/index.html`**.

## Panels and layout

- Terminal below editor; chips **Project** / **Terminal**; **Reset layout**.
- Run footer: `-- finished: OK` / `complete` / `FAIL` / `panic` / `error`; compile errors: `-- compile failed`.

## Limits

- Optional natives static-linked in WASM when declared with `use native`.
- No Git/cloud sync in v1.

## Spck Editor (phone)

Copy **`docs/fleditor_site/`** into a Spck project; keep relative `wasm/`, `ext/`, etc.

## Rebuild WASM

```bash
make -f Makefile.wasm fleditor-wasm-clean
make -f Makefile.wasm fleditor-wasm
```

Output: **`docs/fleditor_site/wasm/`**. See `Makefile.wasm` (`-DFLAT_WASM_BUILD`, `-s ASYNCIFY=1`).
