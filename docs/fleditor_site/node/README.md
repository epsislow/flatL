# fleditor static server

Serves the **`docs/`** tree (GitHub Pages layout) on localhost:

- `/` → `docs/index.html` (redirect to editor)
- `/fleditor_site/` → editor
- `/doc_site/` → offline HTML docs (`make docs`)

```bash
make fleditor-serve
# or from repo root:
node docs/fleditor_site/node/serve.js 8765
```

Open `http://127.0.0.1:8765/` or `http://127.0.0.1:8765/fleditor_site/`. WASM must be built first (`make fleditor-wasm`).
