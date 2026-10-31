#!/usr/bin/env bash
# Build fleditor WASM (requires Emscripten). From repo root:
#   ./scripts/build-fleditor-wasm.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [[ -f "$HOME/emsdk/emsdk_env.sh" ]]; then
  # shellcheck source=/dev/null
  source "$HOME/emsdk/emsdk_env.sh"
fi
if ! command -v emcc >/dev/null 2>&1; then
  echo "emcc not found. Install: https://emscripten.org/docs/getting_started/downloads.html" >&2
  echo "  git clone https://github.com/emscripten-core/emsdk.git ~/emsdk" >&2
  echo "  cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest" >&2
  exit 1
fi
make -C "$ROOT" -f Makefile.wasm fleditor-wasm
