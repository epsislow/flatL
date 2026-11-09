# fleditor static server

Serves the **`docs/`** tree (GitHub Pages layout) on localhost:

- `/` → `docs/index.html` (redirect to editor)
- `/fleditor_site/` → editor
- `/doc_site/` → offline HTML docs (`make docs`)

```bash
make fleditor-serve-forced
make fleditor-stop    # stop listener
```

Open `http://127.0.0.1:8765/` or `http://127.0.0.1:8765/fleditor_site/`. WASM must be built first (`make fleditor-wasm`).

### WSL1 / `apt nodejs`

On WSL1, Ubuntu’s `/usr/bin/node` often fails with **Exec format error**. The repo launcher (`scripts/fleditor_run_serve.sh`) falls back to **Windows** `C:\Program Files\nodejs\node.exe` via `wslpath` — **`make fleditor-serve-forced` is enough**; you do not need `nvm` for the editor server.

Optional — Linux Node via **nvm** (not installed by default; `nvm` is not an `apt` package):

```bash
curl -fsSL https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.1/install.sh | bash
# close and reopen the terminal, or:
source "$HOME/.nvm/nvm.sh"
nvm install --lts
make fleditor-node-check
```
