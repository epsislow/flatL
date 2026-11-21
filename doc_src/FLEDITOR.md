# fleditor — browser playground (WASM)

**fleditor** lives under **`docs/fleditor_site/`** (same tree as [GitHub Pages](../docs/index.html)): edit `.fl` in the browser, compile and run via **flc + flvm** in WebAssembly. Node is only an optional static server.

## Quick start

1. Build WASM (Linux/WSL/macOS with [Emscripten](https://emscripten.org/) on `PATH`):

   One-time Emscripten (recommended: **system path**, not `$HOME`):

   ```bash
   sudo apt install -y git python3 cmake   # if cmake/git missing
   sudo mkdir -p /opt
   sudo git clone https://github.com/emscripten-core/emsdk.git /opt/emsdk
   cd /opt/emsdk && sudo ./emsdk install latest && sudo ./emsdk activate latest
   # optional: sudo chown -R "$(whoami)" /opt/emsdk   # so later installs need no sudo
   ```

   Add to `~/.bashrc` (no startup spam — `EMSDK_QUIET=1` is supported by emsdk):

   ```bash
   if [ -f /opt/emsdk/emsdk_env.sh ]; then
     EMSDK_QUIET=1 . /opt/emsdk/emsdk_env.sh
     # WSL1: bundled node may not run — point emcc at apt node
     [ -x /usr/bin/node ] && export EMSDK_NODE=/usr/bin/node
   fi
   ```

   If `emcc` still errors on `Exec format error` for `/opt/emsdk/node/...`, set **`/opt/emsdk/.emscripten`**:

   ```text
   NODE_JS = '/usr/bin/node'
   ```

   (Keep `LLVM_ROOT`, `EMSCRIPTEN_ROOT`, etc. unchanged.)

   Or only for one shell: `EMSDK_QUIET=1 source /opt/emsdk/emsdk_env.sh`

   Then from the repo:

   ```bash
   make rebuild                  # clean + bin/* + fleditor WASM (recommended after compiler changes)
   # or: make fleditor-rebuild (same as rebuild) / make fleditor-wasm (WASM only)
   # without emcc: make rebuild-native, then make fleditor-wasm when emsdk is available
   ```

2. Build offline docs (required for the **Docs** toolbar link — TOC from `doc_src/index.json`, including **Modules (.fmod)** and **User functions**):

   ```bash
   make docs
   ```

   Without `make docs`, `docs/doc_site/js/doc-data.js` is stale and new sections will not appear in the sidebar.

3. Serve the **Pages layout** (`docs/` root):

   ```bash
   make fleditor-serve-forced
   make fleditor-stop    # free port 8765
   make fleditor-node-check   # WSL1: apt node may fail; launcher uses Windows node.exe
   ```

   See [docs/fleditor_site/node/README.md](../docs/fleditor_site/node/README.md) for WSL1 / Node details.

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
| `js/fleditor-block-analyzer.js` | Static index of blocks and block-transfer edges for Code Map |
| `js/fleditor-code-map.js` | SVG Code Map renderer and click navigation callbacks |
| `ext/` | Vendored CodeMirror 5 + xterm.js (see `ext/VENDOR.md`) |

**FlatL syntax:** line modifiers **`?`** (silent native fail) and **`~`** (route to **`#or`**) at the start of an instruction line are highlighted as **`fl-modifier`**. See [SPECIFICATION.md](./SPECIFICATION.md) §1.2.2.
| `wasm/` | `flatl.js` + `flatl.wasm` (Emscripten → `Makefile.wasm`) |
| `node/serve.js` | Serves **`docs/`** (editor + doc_site), port **8765**, **127.0.0.1** |
| `examples/` | Curated `.fl` + `manifest.json` |

Refresh vendored UI: `./scripts/vendor-fleditor-ext.sh`


## Code Map

The **Code Map** button on the `.fl` editor action bar switches from source editing to an interactive block-transfer graph. The graph starts from the currently open `.fl` file and includes only that file plus its transitive `use` dependencies. Unrelated project files are excluded; every reachable file is included once, and isolated blocks inside those files remain visible.

The graph has two layout modes, selectable from its toolbar:

- **Connected** (default): blocks are arranged by transfer distance from blocks in the entry file. Nodes from different files can be interleaved; each node shows its source file and line. Disconnected blocks remain visible after the connected portion.
- **By file**: the same nodes and edges are grouped into columns by source file. Cross-file transfers remain visible.

Both modes use the same graph and transfer model. The analyzer draws `jump`, `call`, `#or` guard/`~` handler, `for`, `foreach`, explicit `PANIC` indexed/line-labelled handlers, and automatic runtime panic routes to `#panic.any` where statically identifiable. Edge clicks navigate to the source instruction; node clicks open the source file and move the cursor to the block declaration.

- The selected node follows the cursor in the source editor: a cursor inside a block selects that block; a cursor outside all blocks clears the selection. Moving focus away from the editor preserves the last cursor position and therefore the last selection.
- The selected block and its direct neighbours/transfers are highlighted, while the rest of the graph stays visible at reduced emphasis. Every node remains draggable regardless of selection.
- `next`, `return`, and user-defined function call graphs are intentionally not represented.
- `for` / `foreach` targets are resolved from literal block labels or a simple string-label assignment earlier in the same block. For dynamic expressions, a preceding special comment can declare possible destinations: `-- @map target=#round,#retry` or a block comment containing `@map target=#round,#retry`. The hint applies to the next instruction. Hint-derived edges use a dashed line.
- A dynamic `for` / `foreach` target without a hint is shown as a `Dynamic target` node connected to the loop's source block; the expression and source line are retained. This is informational, not a compiler diagnostic.

The analyzer is a browser-side static index, not a compiler diagnostic. Hints are editor-only comments and do not change compiler or VM behavior. An isolated block is not considered an error. Run `make fleditor-block-map-smoke` for the analyzer smoke test.

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

**Format…** (editor toolbar, not on `.fl` or CSV) — **line endings** (Unix LF, Windows CRLF, Mac CR) and **encoding** (UTF-8, UTF-8 with BOM, UTF-16 LE/BE). The editor shows normalized LF internally; save writes the chosen line ending. Non–UTF-8 text uses the same project wrapper pattern as CSV (`FLEDITOR::TEXT::<encoding>::\n` + body).

## CSV / TSV table editor (`.csv`, `.tsv`)

Project files named **`*.csv`** or **`*.tsv`** open in the **CSV editor (grid)** when the table is at most **2000 rows** and **100 columns** (always grid on reopen, not text). Larger files open in the **text editor** with a banner: **`Too large (X rows, Y columns). Open as text.`** Use **Open as table** on the banner (or **▦** in the editor toolbar) to force the grid anyway.

### View layout

- Scrollable **table grid** with one input per cell (CodeMirror hidden in table mode).
- Row/column labels; **`+`** cells on the right and bottom append a column or row (same idea as hex **append**).
- Toolbar row: **dimensions** (`R × C`), **Header** toggle (yellowish highlight on row 1, display only), **Size…** (modal with **Rows** and **Cols**; shrink asks for `yes` if data would be removed), **Format…** (separator, quote, encoding).
- **Text** / **▦** in the editor action bar toggles **table ↔ text** for the same file.

### Parsing and saving

- **Auto-detect** `,`, `;`, or tab on the first lines; **`.tsv`** prefers tab.
- **RFC-style** quoted fields (`"a,b"`, `""` escape).
- **Line endings** detected on load (`\n` or `\r\n`) and preserved on **Save**.
- **Empty file** (0 bytes): grid starts **1×1**; saving an unchanged empty grid writes **0 bytes**.
- **MEMFS / Run:** bytes imported from the filesystem are decoded as **UTF-8**, **UTF-8 with BOM**, or **UTF-16 LE/BE** (BOM required). On **Save** to the project, **UTF-8** is stored as a plain string; other encodings use a project prefix `FLEDITOR::CSV::<encoding>::\n` before the logical table text. MEMFS sync writes the matching bytes (including BOM where applicable).
- **Format…** — **separator** (`,`, `;`, tab) with **Auto** toggle (detect from content, same style as **Header**), **quote** (`"` or `'`) with **Always** (quote every field on save), **line endings** (Unix LF, Windows CRLF, Mac CR), and **encoding** from a fixed list (**UTF-8**, **UTF-8 with BOM**, **UTF-16 LE**, **UTF-16 BE**).
- All UI strings are **English**.

### Editing

- **Pos** + **Go** — jump to cell **`row,column`** (1-based, e.g. `2,3`). **Search** — literal text in cells; **Prev** / **Next** with **`n / total`** counter (like the hex editor).
- **Arrow keys**, **Tab** / **Shift+Tab**, **Enter** (cell below; adds a row at the bottom edge).
- **Delete** clears the cell; **Backspace** on an empty cell moves left.
- **Selection** — click a cell, then **Shift+click** another cell or **Shift+arrow** to select a rectangle (green tint). On **touch**, use the **Select** toggle in the CSV bar (stays on like **Shift**): tap the anchor cell, turn **Select** on, tap another cell or row/column header to extend. Click a **row number** or **column number** to select that entire row or column; **Shift+click** or **Select** on another header extends the range (headers highlight when the selection spans the full width or height). The bar shows **`N(c) S(sum)`** for the current block: **`N`** = selected cells (cap **`MAX(c)`** above 9999); **`S`** = sum of numeric cell values only (integers or up to 2 decimals, cap **`MAX(sum)`** / **`-MAX(sum)`** outside ±9999). **Copy** / **Paste** in the CSV bar (works on mobile when the async clipboard API is blocked). **Ctrl/Cmd+C** / **Ctrl/Cmd+V** on desktop; paste in a cell also expands a TSV/CSV block from the system clipboard. Copy uses the file **separator**, **line endings**, and **quote** rules (same as save).
- Modified cells are **highlighted** until **Save**; **Undo / Redo** are disabled in table mode (same as hex).

### Unsaved changes

- Switching **table ↔ text** or another project file uses the same **Unsaved changes** dialog as **`.fl`** files (Continue / Cancel). **Continue** discards edits since the last **Save** (reverts to the last saved in-editor snapshot) and then performs the action. Nothing is written to disk until **Save**.

### FlatL programs and CSV on MEMFS

After **Run**, the runtime **`chdir`s to `/fleditor`**, so paths match the **project tree** (e.g. **`file.readCtx('examples/csv_reader/stock.csv')`**, not bare `stock.csv` when the file lives under `examples/csv_reader/`).

| Topic | Guidance |
|-------|----------|
| **Reading** | `file.read` returns the file body as a FlatL string (UTF-8 bytes in MEMFS). For tables, declare **`use native csv`**, then **`csv.reader(fp)`** / **`csv.next`** (see [natives/csv.md](./natives/csv.md)). Example project: **`file.readCtx('examples/csv_reader/stock.csv')`** (cwd `/fleditor`, path = tree rel. to project root). |
| **Writing** | `file.write` should emit the **same bytes** the editor would: same **separator**, **line endings**, and **encoding** (use **Format…** in the editor as the reference). |
| **Encoding** | UTF-8 plain in project → UTF-8 bytes on MEMFS. UTF-8 BOM / UTF-16 → editor stores a wrapper; sync writes BOM bytes. |
| **Round-trip** | Edit in grid → **Save** → **Run** → program reads `/fleditor/your.csv`. If the program writes a new file, sync pulls it back with `bytesToProjectValue` (opens in CSV grid when decodable). |
| **Excel samples** | `docs/fleditor_site/examples/test1_csv_*.csv` — same 8×2 table: **LF**, **CRLF (MS-DOS)**, **CR (Mac)**, **UTF-8 BOM + LF**, **UTF-16 LE/BE**. Listed in `examples/manifest.json`; used in `make fleditor-csv-smoke`. **`.gitattributes`** marks them `-text` so Git keeps bytes. |

The editor does not run FlatL **`file.*`** for you; it keeps project/MEMFS bytes aligned so programs and Excel see a consistent file.

### Tests (Node)

```bash
make fleditor-csv-smoke
make fleditor-wasm-csv-smoke   # needs make fleditor-wasm
```

`fleditor-csv-smoke` runs parse, text format, round-trip (Excel exports under `examples/`), and **`csv_vm_parity`** (FlatL `csv.*` vs JS on `test1_csv_*` UTF-8 fixtures). `fleditor-wasm-csv-smoke` reads CSV via WASM `file.read` and runs **`examples/csv_reader/csv_reader.fl`** with **`use native csv`** on MEMFS.

## JSONL records editor (`.jsonl`, `.ndjson`, `.json`, `.jsl`)

Project files with these extensions open in the **Jsonl editor** (Records view) when the file has at most **100000** lines. Larger files open as **text** with a banner and **Open as records**.

### View layout

- **Records:** left list of lines (paginated, **40** lines/page) with **`+`** (add empty line), **`×2`** (duplicate the selected line), **`↑` / `↓`** (move the selected line), and **`×`** (delete a line). Duplicate preserves the exact stored source line and selects the copy. Move swaps the selected line with its adjacent line; boundary buttons are disabled. If the selected Free line has unapplied edits, Duplicate/Move first asks whether to Apply, Revert, or Cancel. Right panel shows the **selected line only**. The action row (`+`, `×2`, `↑`, `↓`) stays fixed while the line list scrolls independently, so actions remain accessible even at the bottom of a long page.
- **Free | Lock** toggle on the right:
  - **Lock** — pretty view of a parsed JSON value (object, array, or scalar). Nested objects/arrays start collapsed as `{…}` / `[…]` with **`[+]`**; click to expand **`[-]`**. Lock is **read-only** (no inline edit).
  - **Free** — text editor for that one line. Empty/whitespace shows **`no json detected`**. Invalid JSON shows an error (and a caret highlight when the engine reports a position).
- **Free → Lock** runs `JSON.parse` on the Free text; on error you **stay in Free**; on success you enter Lock (line stored compact via `JSON.stringify`).
- **Lock → Free** always allowed (pretty text as a starting point for edits).
- Toolbar **Text** / **Records** toggles the whole-file CodeMirror view vs Records (same pattern as CSV **Text** / **▦**).
- On disk, each record stays **one compact line**; pretty formatting is only in the Lock/Free panels.

### Select / Filter / Adv.Filter

The bar button cycles **Select → Filter → Adv.Filter → Select**. Empty Filter / Adv.Filter input shows **all** lines (`Lines …`). With a non-empty query, Filter / Adv.Filter narrow the list and the page label becomes **`Found a–b of N`**. Line numbers in the list stay **1-based file indices**.

| Mode | Input | List | Prev / Next |
|------|-------|------|-------------|
| **Select** | substring | all lines | jump between matches (Lock expands/highlights matches) |
| **Filter** | substring | matching lines only | within Found |
| **Adv.Filter** | path query on `$` | lines where the expression is true on parsed JSON | within Found |

Invalid Adv.Filter syntax → Found empty + short error next to the input (until the query parses). Empty / invalid JSON lines never match Adv.Filter (they are skipped, not a query error). Comparisons are **case-sensitive**. List indexes in paths are **0-based** (`[0]` = first).

#### Adv.Filter syntax (MVP)

- **Path:** `$`, `$.key`, `$."weird key"`, `$[2]`, `$[*]`, chains such as `$.items[2].location`
- **Existence:** bare path → true if navigable (`$.items`, `$.items[*].location`)
- **Compare:** `=`, `!=`, `<`, `>` on strings / numbers; `is true` / `is false` / `is null` / `is not null`  
  (missing path → false for both `is null` and `is not null`; only JSON `null` makes `is null` true)
- **`len`:** `PATH len OP N` = **string** length; `PATH[] len OP N` / `$[] len OP N` = **list** length
- **Lists:** `$[*] = "A2"` ≡ `$.locations has "A2"`; also `not has`, `has […]` (OR), `has all […]`, `IN […]` / `not IN […]`
- **Strings:** `contains` / `starts with` / `ends with` (+ `not …`)
- **Type:** `PATH type object|number|bool|list|array|string|null` (`array` ≡ `list`); `not type` / `not ( … )`
- **Logic:** `and` / `or` / `not` / `( … )` — precedence `not` > `and` > `or`

Examples:

```text
$.name = "Bob" and $.admin is true
$.id > 4
$ is true
$[] len = 3
$.items[] len > 0
$.locations has "A2"
$.mesaj contains "test"
$.val type list
```

### Tests (Node)

```bash
make fleditor-jsonl-smoke
make fleditor-jsonl-adv-filter-smoke
```

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
- **`def`**, **`func:executor`**, and linked **`blocks/executor.fl`** require a current **`flatl.wasm`** (includes `flat_func.c` in `Makefile.wasm`). If Run reports `expected use directive or block` on a `func:` line, rebuild WASM below and hard-refresh the browser.
- No Git/cloud sync in v1.

## Spck Editor (phone)

Copy **`docs/fleditor_site/`** into a Spck project; keep relative `wasm/`, `ext/`, etc.

## Rebuild WASM

```bash
make -f Makefile.wasm fleditor-wasm-clean
make fleditor-wasm
# or: make rebuild  (when emcc is on PATH)
```

Smoke after rebuild: `make fleditor-wasm-order-flow-smoke`.

Output: **`docs/fleditor_site/wasm/`**. See `Makefile.wasm` (`-DFLAT_WASM_BUILD`, `-s ASYNCIFY=1`).


### Code Map layouts (v1.3)

The Code Map provides three interchangeable layouts: **Vertical** (execution layers from top to bottom), **Horizontal** (execution layers from left to right), and **By file** (blocks grouped by source file). All layouts use the same analyzed nodes and transfers; layout changes never filter transfer edges. Edge colors continue to identify transfer types. Selecting a block highlights its incoming and outgoing transfers, and nodes can be repositioned by dragging. Zoom controls are available in the map toolbar.
