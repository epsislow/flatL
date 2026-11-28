#!/bin/sh
set -eu
root=$1
tmp=$2
cc=${CC:-gcc}
nm=${NM:-nm}

cat > "$tmp/emscripten.h" <<'EOF'
static inline void emscripten_sleep(unsigned int ms) { (void)ms; }
EOF

"$cc" -std=c11 -Wall -Wextra \
    -DFLAT_WASM_BUILD -DFLAT_WASM_BROWSER_WEBSOCKET \
    -DFLAT_WITH_WEBSOCKET=1 \
    -I"$tmp" -I"$root/include" \
    -c "$root/src/common/flat_runtime.c" \
    -o "$tmp/flat_runtime_wasm_gate.o"

undefined=$("$nm" -u "$tmp/flat_runtime_wasm_gate.o")
if printf '%s\\n' "$undefined" | grep -E 'flat_io_reactor_|flat_ws_server_registry_' >/dev/null; then
    echo "WASM runtime unexpectedly references native WebSocket server symbols" >&2
    printf '%s\\n' "$undefined" >&2
    exit 1
fi
if ! printf '%s\\n' "$undefined" | grep -F 'flat_ws_event_client_start' >/dev/null; then
    echo "WASM browser WebSocket client was unexpectedly disabled" >&2
    exit 1
fi

echo wasm-websocket-server-gate-ok
