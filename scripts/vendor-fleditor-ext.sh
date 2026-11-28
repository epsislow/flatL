#!/usr/bin/env bash
# Vendor CodeMirror 5 + xterm.js into docs/fleditor_site/ext/ (no CDN at runtime).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EXT="$ROOT/docs/fleditor_site/ext"
LOGT="$HOME/logTscript/res"

mkdir -p "$EXT"

if [[ -f "$LOGT/js/codemirror.min.js" ]]; then
  cp "$LOGT/js/codemirror.min.js" "$EXT/codemirror.min.js"
  cp "$LOGT/css/codemirror.min.css" "$EXT/codemirror.min.css"
else
  echo "Downloading CodeMirror 5.65.16..."
  curl -fsSL "https://cdn.jsdelivr.net/npm/codemirror@5.65.16/lib/codemirror.js" \
    -o "$EXT/codemirror.min.js"
  curl -fsSL "https://cdn.jsdelivr.net/npm/codemirror@5.65.16/lib/codemirror.css" \
    -o "$EXT/codemirror.min.css"
fi

XTERM_VER="5.3.0"
echo "Downloading xterm.js ${XTERM_VER}..."
curl -fsSL "https://cdn.jsdelivr.net/npm/xterm@${XTERM_VER}/lib/xterm.js" \
  -o "$EXT/xterm.js"
curl -fsSL "https://cdn.jsdelivr.net/npm/xterm@${XTERM_VER}/css/xterm.css" \
  -o "$EXT/xterm.css"

echo "OK: vendored into $EXT"
