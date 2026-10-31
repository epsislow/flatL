# fleditor — browser playground (WASM)

**fleditor** is a self-contained site under `fleditor_site/`: edit `.fl` source in the browser, compile and run via **flc + flvm** in WebAssembly. Node is only used as an optional static file server.

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

2. Serve the site:

   ```bash
   make fleditor-serve
   # Port already in use: make fleditor-serve-forced  (fuser -k 8765/tcp, then serve)
   # or: node fleditor_site/node/serve.js 8765
   ```

   **WSL:** if `make fleditor-serve` says `node: Permission denied`, your PATH may pick Windows `node.exe` under `/bin/sh`. Use Linux Node: `make fleditor-serve NODE=/usr/bin/node`, or put `/usr/bin` before `/mnt/...` in PATH.

3. Open `http://127.0.0.1:8765/` and click **Run** (runs the open file).

**Two-file example:** create `lib/greet.fl` with a block (no `#main` in imports), then in `main.fl`:

```text
use "lib/greet.fl" as #Greet_

#main
  call #Greet_hello
  OK
```

Paths in `use "…"` must match the project tree. Rebuild WASM after compiler changes: `make fleditor-rebuild`.

Windows without Emscripten: use prebuilt `fleditor_site/wasm/flatl.js` + `flatl.wasm` from the repo (after someone builds them), then run the same `node` command.

## Layout

| Path | Role |
|------|------|
| `index.html` | Shell UI |
| `css/` | Theme (dark + warm beige text) |
| `js/fleditor.js` | Run/Save, project UI, WASM glue, Xterm I/O |
| `js/fleditor-project.js` | Virtual project model, localStorage, MEMFS sync |
| `ext/` | Vendored CodeMirror 5 + xterm.js (see `ext/VENDOR.md`) |
| `wasm/` | `flatl.js` + `flatl.wasm` (Emscripten output) |
| `node/serve.js` | Static server, port = argv (default **8765**), bind **127.0.0.1** |
| `examples/` | Curated sample `.fl` files + `manifest.json` (see `examples/README.md`) |

Refresh vendored UI libs: `./scripts/vendor-fleditor-ext.sh`

## I/O model

- **Stdout** (`io.out*`) → xterm.js terminal.
- **Stdin** (`io.in*`) → in-page **modal** (`<dialog>`); the title uses the program prompt (e.g. `Name: (ln)` for `io.inln('Name:')`, truncated to 32 chars + `…` if needed). OK/Cancel resume WASM via `emscripten_sleep` polling.
- Project actions (new path, delete, reset, unsaved warning, validation errors) use the same **`<dialog>`** pattern — no native `prompt` / `confirm` / `alert`.
- Empty line on `io.inln` / `io.in_num` / `io.in_bool` → cancel (`#panic.any`), same as desktop [IO.md](IO.md).
- Single-character `io.in`: ESC or Enter → cancel.

## Multi-file project

- **Project panel** (left): file tree, **New file** / **New folder**.
- **Run** compiles and runs the **file currently open** in the editor (no separate “entry” flag).
- **Run** implicitly **saves** the open file to the project and `localStorage` before compile.
- **Save** persists the open file; a **●** marker shows unsaved edits (toolbar + tree).
- Switching files with unsaved edits opens a dialog: *“You have unsaved changes…”* — **Continue** / **Cancel** (English UI).
- **Undo** / **Redo** in the toolbar (CodeMirror history); **Ctrl+S** / **Cmd+S** saves.
- Paths are **POSIX-style** (`lib/util.fl`). A `use "lib/util.fl"` line must match a file at that path in the project.
- On **Run**, all project files are written to WASM **MEMFS** under `/fleditor/…`, then `flc` links from the open file’s path.

Project state is stored in **`localStorage`** key `flatl-fleditor-project` (JSON: `files`, `dirs`, `openPath`). The old single-buffer key `flatl-fleditor-source` is migrated to `main.fl` on first load.

### Delete and reset

- **`+fl`** / **`+dir`** — new file or folder (tooltips on hover).
- **`×`** on a file or folder row — delete (English confirm dialogs).
- Deleting the **open** file with unsaved edits: unsaved-changes dialog first, then delete confirm; then opens **`main.fl`** or another file, or shows the empty-project message.
- **Empty project:** message *“No files in project. Use +fl or Reset project.”* — **Run** is disabled.
- Dirty open file: editor title shows **`(not saved) path.fl`**.
- **Reset project** (toolbar) restores **`main.fl`** plus all curated files under **`examples/`** (from `fleditor_site/examples/` on disk); does **not** change panel layout (**Reset layout** is separate).
- First visit (empty `localStorage`) loads the same **`main.fl` + `examples/`** set.
- Toolbar **Docs** → `/doc_site/index.html` on the same host when using **`make fleditor-serve`** (server also mounts repo `doc_site/`). From **Open fleditor** in docs, link is `/index.html`.

## Panels and layout

- **Editor** uses most of the window; the **terminal** sits **below** the editor when open (not side-by-side).
- **Default:** Project panel **on**, terminal **off**. **Run** opens the terminal automatically.
- **Toolbar:** **Project** and **Terminal** chips toggle panels (use these if a panel was closed with **`[−]`** on its header).
- **`[−]`** on the Project or Terminal header hides that panel; **`Reset layout`** restores defaults (Project on, terminal off) and only clears layout prefs (`flatl-fleditor-layout`), not your `.fl` files.
- After each **Run**, the terminal prints a muted footer: `-- finished: OK`, `-- finished: complete`, `-- finished: FAIL`, `-- finished: panic`, or `-- finished: error` (from VM termination reason + exit code). On compile/link failure (after the error text): `-- compile failed`.

| Path | Role |
|------|------|
| `js/fleditor-layout.js` | Layout prefs in `localStorage` |

## Limits

- Optional natives (`list`, `obj`, `math*`) are **static-linked** in WASM when declared with `use native`.
- No `SharedArrayBuffer` / cross-origin isolation headers in v1.
- No Git, cloud sync, or drag-in folder import (browser-only virtual project).

## Spck Editor (phone)

Copy the whole `fleditor_site/` folder into a Spck project and use Spck’s HTTP preview. Relative paths must stay intact (`wasm/`, `ext/`, etc.).

## Rebuild WASM

After changing compiler, VM, or natives:

```bash
make -f Makefile.wasm fleditor-wasm-clean
make -f Makefile.wasm fleditor-wasm
```

Flags and sources are listed in `Makefile.wasm` (`-DFLAT_WASM_BUILD`, `-s ASYNCIFY=1`).
