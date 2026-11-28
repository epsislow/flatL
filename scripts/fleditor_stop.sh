#!/bin/sh
# Stop fleditor serve.js on PORT (WSL1: fuser often misses Windows node.exe listeners).
PORT=${1:-8765}

fuser -k "${PORT}/tcp" 2>/dev/null || true

# Linux node in WSL
pkill -f "docs/fleditor_site/node/serve.js ${PORT}" 2>/dev/null || true
pkill -f "fleditor_site/node/serve.js ${PORT}" 2>/dev/null || true

# Windows node launched from WSL (see: /init ... node.exe ... serve.js PORT)
pkill -f "node.exe.*serve.js ${PORT}" 2>/dev/null || true
pkill -f "serve.js ${PORT}" 2>/dev/null || true

# Allow the port to be released
sleep 1

exit 0
