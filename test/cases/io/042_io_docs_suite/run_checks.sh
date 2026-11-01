#!/bin/sh
set -e
ROOT=$1
FB=$2
OUT=$($ROOT/bin/flvm "$FB" 2>/dev/null || true)
printf '%s' "$OUT" | grep -q '(16 functions)' || {
  echo "missing mem footer"
  exit 1
}
printf '%s' "$OUT" | grep -q 'io.docs' || {
  echo "missing io.docs in io list"
  exit 1
}
printf '%s' "$OUT" | grep -q '....' || {
  echo "missing truncate ellipsis"
  exit 1
}
printf '%s' "$OUT" | grep -q '(33 functions)' || {
  echo "missing cap functions footer"
  exit 1
}
exit 0
