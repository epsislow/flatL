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

Publish from branch **`main`**, folder **`/docs`** (Settings → Pages → **Deploy from a branch** → `/docs`).

When `/docs` is active, the **site root** is the contents of `docs/` — URLs do **not** include `/docs/` in the path:

| Path on site | Example (project `flatL`) |
|--------------|---------------------------|
| Editor | `https://<user>.github.io/flatL/fleditor_site/` |
| Docs viewer | `https://<user>.github.io/flatL/doc_site/` |
| Landing | `https://<user>.github.io/flatL/` → redirect to editor |

If you still see the **README (Jekyll)** at `/flatL/` or need `/flatL/docs/fleditor_site/` to open the editor, Pages is publishing the **repo root** instead of `/docs`. Fix the folder to **`/docs`**, wait a few minutes, or use repo-root **`index.html`** + **`.nojekyll`** (redirect to `docs/fleditor_site/`) until the setting applies.

Run **`make docs`** before push so `docs/doc_site/` is up to date. **`docs/.nojekyll`** disables Jekyll inside the published folder.

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

- **Project panel**; **Run** / **Save** / Undo/Redo on the **editor action bar** (under the file path). Run uses the open file; Run implicitly saves before compile.
- **MEMFS** under `/fleditor/…` on Run.
- **`localStorage`** key `flatl-fleditor-project`.

### Delete and reset

- **`+fl`** / **`+dir`**, **`×`** delete.
- **Reset project** (**⟲** on the Project panel header) loads `main.fl` + `examples/` from disk manifest.
- Toolbar **Docs** → relative **`../doc_site/index.html`**. In docs, **Open fleditor** → **`../fleditor_site/index.html`**.

## Panels and layout

- **Project** (left) and **Terminal (I/O)** (below editor) use **`[−]`** on the panel header to **minimize** (not destroy state).
- Minimized panels appear on the **bottom dock** as **`[+]` Project** / **`[+]` Terminal** — click to restore.
- **↺** on the bottom dock (after the restore buttons) **Reset layout** — default: Project open, terminal minimized; does not change your `.fl` files.
- **Run** still opens the terminal if it was minimized.
- **Undo** / **Redo** / **Save** / **Run** on the editor action bar; top toolbar is title, **Docs**, and status only.
- **Clear** on the terminal panel tool row (below the Terminal title).
- Run footer: `-- finished: OK` / `complete` / `FAIL` / `panic` / `error`; compile errors: `-- compile failed`.

Layout prefs: `localStorage` key `flatl-fleditor-layout`.

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
