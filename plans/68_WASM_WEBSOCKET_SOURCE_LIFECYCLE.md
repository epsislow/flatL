# WASM WebSocket source lifecycle and E2E prerequisite

## Problem

The one-shot FLEditor WASM runner treats every event subscription as outstanding asynchronous work. A socket subscription is persistent by design, so after the socket has emitted `close`, an otherwise-finished FlatL run can remain alive forever unless every subscription is explicitly stopped by user code. The close notification itself must still be delivered to matching handlers, and already-dispatched handlers must be allowed to finish.

## Change

- Mark all `socket` subscriptions for a source as `source_closed` when its `close` event is published.
- Keep closed-source subscriptions alive while they still have queued events, dispatch in progress, or active handler tasks.
- In the WASM runner, prune closed-source subscriptions only after their queues and handler accounting are drained.
- Do not change native runtime subscription semantics; pruning is used by the one-shot WASM runner only.

This separates source lifetime from subscription object lifetime: a closed socket cannot produce more events, but its final close notification and any already-started handler remain valid work.

## Validation

- `make -j2 all` — PASS.
- `bin/run_tests 'flvm[71-75]' -v -e` — PASS (5/5).

The current environment has no Emscripten SDK, so the WASM-specific branch and browser execution have not been compiled or run here. The full FlatL-level Node E2E remains the next task; this change addresses the closed-source keepalive condition that could otherwise prevent cleanup.

## Archive 385 follow-up — peer-close socket registry cleanup

The FlatL-level E2E exposed an additional lifecycle requirement in the browser backend: a peer-initiated `close` must remove not only the event subscriptions, but also the runtime-owned numeric socket entry. Otherwise the WASM runner still sees a nonzero socket count after the close callback and cannot tear down the one-shot runtime.

The browser `onclose` callback now removes its JavaScript socket handle before publishing the final close event. The C event bridge then calls `flat_runtime_ws_close()` to unlink and release the runtime-owned client record. This ordering avoids re-entering the JavaScript close callback for a socket that is already closed. Locally initiated close remains safe: registry unlink happens before the backend stop callback, so the nested close publication cannot remove the same entry twice.

The separate `test/fleditor/wasm_websocket_e2e.mjs` now executes a FlatL source program with `task.spawnFor()` handlers for `open`, `receive`, and `close`. Its local Node RFC6455 server sends text/binary messages, validates FlatL text/binary sends, echoes them, and performs a peer-initiated close. The Makefile target is `fleditor-wasm-websocket-e2e`; the existing JavaScript-only smoke remains separate.

Validation in this environment: `node --check test/fleditor/wasm_websocket_e2e.mjs` PASS; the FlatL program used by the E2E compiles with `bin/flc` PASS; `make -j2 all` PASS; `bin/run_tests 'flvm[71-75]' -v -e` PASS (5/5); `make docs` PASS (53 sections). Node.js 22.16.0 is available. `emcc` is not installed, so the actual WASM link and live E2E run have not been executed here. Do not mark the E2E runtime assertions as passing until the user runs the new target with their Emscripten SDK.
