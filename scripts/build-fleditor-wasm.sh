#!/usr/bin/env bash
# Build fleditor WASM (requires Emscripten). From repo root:
#   ./scripts/build-fleditor-wasm.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

try_source_emsdk() {
  local f="$1"
  if [[ -f "$f" ]]; then
  # shellcheck source=/dev/null
    EMSDK_QUIET=1 source "$f"
    return 0
  fi
  return 1
}

if [[ -n "${EMSDK:-}" ]]; then
  try_source_emsdk "${EMSDK%/}/emsdk_env.sh" || true
fi
if ! command -v emcc >/dev/null 2>&1; then
  try_source_emsdk /opt/emsdk/emsdk_env.sh || true
fi
if ! command -v emcc >/dev/null 2>&1; then
  try_source_emsdk "$HOME/emsdk/emsdk_env.sh" || true
fi
if command -v emcc >/dev/null 2>&1 && [[ -x /usr/bin/node ]]; then
  export EMSDK_NODE=/usr/bin/node
fi
if ! command -v emcc >/dev/null 2>&1; then
  echo "emcc not found. Install Emscripten (e.g. under /opt/emsdk):" >&2
  echo "  sudo mkdir -p /opt && sudo git clone https://github.com/emscripten-core/emsdk.git /opt/emsdk" >&2
  echo "  cd /opt/emsdk && ./emsdk install latest && ./emsdk activate latest" >&2
  echo "  source /opt/emsdk/emsdk_env.sh   # or export EMSDK=/opt/emsdk" >&2
  exit 1
fi
make -C "$ROOT" -f Makefile.wasm fleditor-wasm
