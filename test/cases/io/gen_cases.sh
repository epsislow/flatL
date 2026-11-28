#!/usr/bin/env bash
# Optional wrapper: same as make io_generate_tests (portable bin/io_generate_tests).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
GEN="$ROOT/bin/io_generate_tests"
if [[ ! -x "$GEN" ]]; then
  echo "gen_cases: build first: make io_generate_tests (from repo root)" >&2
  exit 1
fi
exec "$GEN"
