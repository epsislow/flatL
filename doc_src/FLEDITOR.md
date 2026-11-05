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
   make rebuild                  # clean + bin/* + fleditor WASM (recommended after compiler changes)
   # or: make fleditor-rebuild (same as rebuild) / make fleditor-wasm (WASM only)
   # without emcc: make rebuild-native, then make fleditor-wasm when emsdk is available
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

Paths in `use "…"` must match the project tree. After compiler/link changes: `make rebuild` (native + WASM), then hard-refresh the browser.

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

**FlatL syntax:** line modifiers **`?`** (silent native fail) and **`~`** (route to **`#or`**) at the start of an instruction line are highlighted as **`fl-modifier`**. See [SPECIFICATION.md](./SPECIFICATION.md) §1.2.2.
| `wasm/` | `flatl.js` + `flatl.wasm` (Emscripten → `Makefile.wasm`) |
| `node/serve.js` | Serves **`docs/`** (editor + doc_site), port **8765**, **127.0.0.1** |
| `examples/` | Curated `.fl` + `manifest.json` |

Refresh vendored UI: `./scripts/vendor-fleditor-ext.sh`

## I/O model

- **Stdout** (`io.out*`) → xterm.js terminal.
- **Stdin** (`io.in*`) → **Settings → Terminal → Program input**: **inline in terminal** (default; prompt on stdout, type in xterm, Enter submits, **Escape** cancels) or **dialog modal** (legacy `<dialog>`). WASM uses Asyncify + `emscripten_sleep` while waiting.
- Project actions use **`<dialog>`** — no native `prompt` / `confirm` / `alert`.
- Empty line on `io.inln` / `io.in_num` / `io.in_bool` → cancel (`#panic.any`), same as desktop [IO.md](IO.md).

## Multi-file project

- **Project panel**; **Run** / **Save** / Undo/Redo on the **editor action bar** (under the file path). Run uses the open file; Run implicitly saves before compile.
- **MEMFS** under `/fleditor/…` on Run.
- Before VM execution, the runtime **`chdir`s to `/fleditor`**, so `file.readCtx('data.txt')` in source matches project file **`data.txt`** (no `/fleditor/` prefix required in `.fl`). After **Run**, MEMFS sync adds files by **relative path** (e.g. `aa.txt` at project root, not under `examples/` unless you created `examples/aa.txt`).
- After **Run**, any file under `/fleditor/` that is **new** or **changed** (byte content vs snapshot right before execution) is **synced into the Project panel** and `localStorage`. Text is stored as UTF-8 strings; other bytes use an internal base64 wrapper (binary-safe round-trip via MEMFS). The wrapper is never shown in the UI.
- **`+file`** opens a dialog: enter the **path with extension**, check **Binary (Hex editor)** for a binary file (empty, wrapper storage), or leave unchecked for **text**. New `.fl` files get a minimal `#main` … `OK` template; other text paths start empty. Binary entries in the tree use an **orange** label (wrapper storage, not extension alone).
- **Run** compiles a **`.fl` entry** (`main.fl` if present, else the first `.fl` in the project), even when a **text** or **hex** file is open in the editor.

## Text editor (non-binary, not `.fl`)

UTF-8 project files such as **`data.txt`**, **`config.json`**, or **`notes.md`** open in **Text editor** (plain CodeMirror, no FlatL highlighting). Click the file row in the Project tree to open it; switching files **auto-saves** the previous file into the project (no discard dialog). **Save** writes `localStorage`. After **Run**, the open file reloads if MEMFS changed it.

## Hex editor (binary files)

When a binary file is open, the panel title shows **Hex editor** and the path. **`.fl`** sources use **Fl editor**.

### View layout

- **16 bytes per line**, **40 lines per page** → **640 bytes per page** in a **hex grid** (one input cell per byte). CodeMirror is hidden; **Fl editor** still uses CodeMirror for text.
- Each row: fixed **offset**, **16 hex cells** (00–ff), read-only **ASCII**.
- **Paginate always:** `|<` `<` `>` `>|`, page indicator, byte range for the current page.
- Status shows **Grid** or **Modified** (no parse step — invalid input reverts on blur).

### Editing

- Click a cell, type hex in **overwrite** mode (two digits then auto-advance to the next cell). Clearing a cell and leaving it → **00**. Invalid hex reverts on blur.
- On the **last page**, the **`+`** cell after the final byte **appends** a byte when you enter hex (same typing rules; focus clears the `+`). It uses the same cell size as other bytes (dashed border when idle).
- **Delete** removes the byte at the focused offset and shifts the rest; focus stays on that offset (or on **`+`** if you deleted the last byte). **Backspace** removes the previous byte and moves focus one cell left (**Backspace** on **`+`** deletes the last byte).
- **Arrow keys** move between cells (**Left** / **Right** follow byte order across row breaks; **Up** / **Down** stay in the same column). **Right** from the last byte focuses **`+`**.
- Cells that differ from the last **saved** snapshot are **highlighted** until Save.
- Edits apply immediately to the in-memory buffer; **Save** writes the full buffer to the project (base64 wrapper) and `localStorage`.
- **Run** saves first. After Run, if the open file was synced from MEMFS and you have no unsaved edits, the editor reloads from the project.
- **Undo / Redo** are disabled in hex mode (byte edits are not in CodeMirror history).

### Go to byte

- **Pos** accepts decimal or `0x` hex (byte index **0 … N−1**). **Go** opens the right page.

### Search

- **Search** accepts **hex** (`48 65`), **base64**, or **literal text** (UTF-8 bytes).
- **Prev** / **Next** cycle through matches in the **whole file** (wrap at ends). Counter **`n / total`** between the buttons shows **`0`** when there are no matches.
- Focusing a grid cell updates **Pos** to that byte index.

### Size and payload tools

| Control | Action |
|---------|--------|
| **Insert…** | Offset + hex or base64 payload inserted into the file |
| **Delete…** | Offset + byte count removed |
| **Size…** | New file length (pad with `0x00` or truncate) |
| **Revert** | Restore last saved bytes |
| **Import…** | Replace entire file from disk |
| **Download** | Save raw bytes locally |

New binary files from **+file** start at **0 bytes**; the grid shows only the **`+`** append cell until you add data. After Run/sync, an empty file behaves the same way.

### FlatL programs

Use [`file.read`](./natives/file.md), [`file.writeBytes`](./natives/file.md), etc. in `.fl` to read/write the same project files. The hex editor is for inspection and manual tweaks; MEMFS sync after Run matches program output.

### Tests (Node)

```bash
make fleditor-hex-smoke
```

Runs `test/fleditor/hex_roundtrip.mjs`, `hex_project_smoke.mjs`, and `hex_grid.mjs` (dump helpers, project wrapper, grid cell parsing).
- **`io.out*`** → terminal (normal). **`io.err*`** on default **`terr`** → same terminal, **gray** text (`flatWasmOnStderr`). Unhandled **PANIC** lines also go to stderr (gray in terminal), with **`[panic]`** / **`-- finished: panic — …`** summarizing the reason when WASM is rebuilt.
- **`localStorage`** key `flatl-fleditor-project`.

### Delete and reset

- **`+file`** / **`+dir`**, **`×`** delete.
- **Reset project** (**⟲** on the Project panel header) loads `main.fl` + `examples/` from disk manifest.
- Toolbar **Docs** → relative **`../doc_site/index.html`**. In docs, **Open fleditor** → **`../fleditor_site/index.html`**.

## Panels and layout

- **Project** (left) and **Terminal (I/O)** (below editor) use **`[−]`** on the panel header to **minimize** (not destroy state).
- Minimized panels appear on the **bottom dock** as **`[+]` Project** / **`[+]` Terminal** — click to restore.
- **↺** on the bottom dock (after the restore buttons) **Reset layout** — default: Project open, terminal minimized; does not change your `.fl` files.
- **Resize panels** (saved in layout prefs): wide screen — drag the **right edge** of Project; drag the **Terminal** title bar up/down. Narrow / stacked screen — drag the **editor** title bar (Project vs editor) and the **Terminal** title bar (editor vs terminal).
- **Run** still opens the terminal if it was minimized.
- **Undo** / **Redo** / **Save** / **Run** on the editor action bar; top toolbar is title, **Docs**, and status only.
- **Clear** on the terminal panel tool row (below the Terminal title).
- Run footer: `-- finished: OK` / `complete` / `FAIL` / `panic` / `error`; compile errors: `-- compile failed`.

Layout prefs: `localStorage` key `flatl-fleditor-layout`.

### Settings

- Toolbar **⚙ Settings** (right of **Docs**) opens a modal: **Display**, **Editor**, **Terminal** (nav left, content right).
- **Display:** theme preset (**Dark**, **Light**, **Carbon**, **Turquoise Gray**, **Custom**, plus **My themes** you save). Colors apply live to chrome, **Project panel** (sidebar only), **Fl editor** + **FlatL syntax**, **Text editor**, **Hex editor**, and **Terminal**. Project panel background does not paint the editor area. Any manual color edit switches to **Custom**. **Save as new theme…** / **Delete theme** (user themes only) / **Reset to Dark**.
- **Editor:** font size, line numbers, tab size, word wrap (`flatl-fleditor-editor-prefs`).
- **Terminal:** font size, **Clear on Run**, **Program input** (`io.in*` — terminal inline vs dialog modal), **Max jumps** / **Max VM steps** / **No jump limit** (WASM run uses same limits as `flvm --max-jumps` / `--max-steps`; stored in `flatl-fleditor-terminal-prefs`).
- Theme state: `localStorage` key **`flatl-fleditor-theme`**. Does not change project files or panel layout reset (↺).
- **About:** fleditor UI version and **flatL WASM** toolchain version (after `make fleditor-wasm`). **Reset all settings** — theme + editor + terminal prefs only (not project or dock layout ↺).

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
