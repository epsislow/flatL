# fleditor static server

Serves the parent folder `fleditor_site/` on localhost.

```bash
# from repo root
node fleditor_site/node/serve.js 8765
```

Open `http://127.0.0.1:8765/` in a browser. WASM must be built first (`make -f Makefile.wasm fleditor-wasm`).
