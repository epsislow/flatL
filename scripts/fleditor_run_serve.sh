#!/bin/sh
# Start fleditor static server: prefer Linux node, else Windows node.exe via wslpath (WSL1).
set -u

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SERVE_JS="$ROOT/docs/fleditor_site/node/serve.js"
PORT=${1:-8765}

if [ ! -f "$SERVE_JS" ]; then
  echo "fleditor: missing $SERVE_JS" >&2
  exit 1
fi

try_linux() {
  c=$1
  [ -n "$c" ] || return 1
  [ -x "$c" ] || [ -f "$c" ] || return 1
  if ! "$c" --version >/dev/null 2>&1; then
    return 1
  fi
  exec "$c" "$SERVE_JS" "$PORT"
}

LINUX=$(sh "$ROOT/scripts/fleditor_linux_node.sh" 2>/dev/null || true)
if try_linux "$LINUX"; then
  : # exec above
fi

for c in /usr/bin/node /usr/bin/nodejs; do
  if try_linux "$c"; then
    :
  fi
done

find_win_node() {
  for w in \
    "/mnt/c/Program Files/nodejs/node.exe" \
    "/mnt/c/Program Files (x86)/nodejs/node.exe"; do
    if [ -f "$w" ]; then
      printf '%s\n' "$w"
      return 0
    fi
  done
  return 1
}

WIN_NODE=$(find_win_node || true)
if [ -n "$WIN_NODE" ]; then
  if ! command -v wslpath >/dev/null 2>&1; then
    echo "fleditor: wslpath required for Windows Node fallback" >&2
    exit 1
  fi
  WIN_SERVE=$(wslpath -w "$SERVE_JS")
  exec "$WIN_NODE" "$WIN_SERVE" "$PORT"
fi

cat >&2 <<'EOF'
fleditor: no working Node found.

  WSL1: Ubuntu nodejs (/usr/bin/node) often fails with "Exec format error".
  Options:
    nvm install --lts   # native Linux Node in WSL
    or ensure Windows Node is at: C:\Program Files\nodejs\node.exe

  make fleditor-node-check
EOF
exit 1
