# WASM Node.js test runner fix — archive 336

## Problem

The user successfully completed `make test` (1,151/1,151) and `make rebuild`, including `fleditor-wasm`. The subsequent `make fleditor-wasm-test` and `make fleditor-wasm-api-smoke` failed with `Permission denied` on `.mjs` files.

The cause was Makefile variable reuse: the general `NODE` variable is intentionally cleared when no supported Linux Node executable is found by `scripts/fleditor_linux_node.sh`, because the FLEditor server targets must avoid WSL shims to Windows `node.exe`. WASM smoke recipes reused `$(NODE)`, so an empty value made the shell try to execute the non-executable `.mjs` file directly. The tests themselves were not failing.

## Change

- Add `WASM_NODE ?= node` for headless WASM tests.
- Switch every `fleditor-wasm-*` Node smoke recipe to `$(WASM_NODE)`.
- Keep the existing `NODE` policy unchanged for FLEditor server/UI targets.
- Do not change file executable bits; `.mjs` files are Node source files and should be invoked through Node.
- Users with a non-standard Node path can override with `make WASM_NODE=/path/to/node fleditor-wasm-test`.

## Validation

On the available Linux container with Node.js v22.16.0 and the archived `flatl.js`/`flatl.wasm` artifacts:

- `make -o fleditor-wasm fleditor-wasm-test`: PASS, all nine tests. The `-o` option skips rebuilding the already-present WASM target; it is used here because `emcc` is not installed in this container.
- `make -o fleditor-wasm fleditor-wasm-api-smoke`: PASS.
- `make -o fleditor-wasm fleditor-wasm-csv-smoke`: PASS.
- All nine `.mjs` scripts also passed when invoked directly with `node`.

The user independently confirmed `make test`: 1,151 passed, 0 failed, and `make rebuild` including `fleditor-wasm` succeeded. The user should now rerun `make fleditor-wasm-test` and `make fleditor-wasm-api-smoke` after this Makefile correction.

No production C/VM/WASM source was changed. The fix is limited to test command selection.
