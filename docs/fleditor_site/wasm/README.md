# WASM artifacts

Built by Emscripten from repo root:

```bash
make -f Makefile.wasm fleditor-wasm
```

Produces `flatl.js` and `flatl.wasm` in this directory (`docs/fleditor_site/wasm/`). Commit prebuilt binaries if Windows users should run fleditor without WSL.


`flat_wasm_run()` executes through a one-shot cooperative `FlatRuntime` root task. This enables `use native task` and `use native channel` in FLEditor examples. The browser build does not start OS worker threads or browser Web Workers; it uses the cooperative scheduler.
