#!/bin/sh
# One isolated FlatL performance scenario per run_tests case.
# Usage: perf_case.sh REPO_ROOT SCENARIO
set -eu
ROOT="${1:?repo root}"
SCENARIO="${2:?scenario}"
FLC="$ROOT/bin/flc"
FLVM="$ROOT/bin/flvm"
ROUNDS="${FLAT_PERF_ROUNDS:-5}"
case "$ROUNDS" in ''|*[!0-9]*) echo "invalid FLAT_PERF_ROUNDS: $ROUNDS" >&2; exit 2;; esac
[ "$ROUNDS" -ge 1 ] || { echo 'FLAT_PERF_ROUNDS must be >= 1' >&2; exit 2; }
TMP="${TMPDIR:-/tmp}/flatl_perf_${SCENARIO}_$$"
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
SRC="$TMP/$SCENARIO.fl"
FB="$TMP/$SCENARIO.fb"

make_vars_case() {
    count=$1
    {
        echo 'use native math with m'
        echo '#main'
        i=0
        while [ "$i" -lt "$count" ]; do
            printf '  v%04d = %d\n' "$i" "$i"
            i=$((i + 1))
        done
        echo "  for(20000, i, '#step')"
        echo '  io.outln(result)'
        echo '  OK'
        echo '#step'
        printf "  m.calc('v%04d + 1') -> result\n" "$((count - 1))"
        echo '  next'
    } > "$SRC"
}

case "$SCENARIO" in
  vm_loop)
    cat > "$SRC" <<'FL'
#main
  for(100000, i, '#step')
  OK
#step
  next
FL
    ;;
  string_churn)
    cat > "$SRC" <<'FL'
#main
  s = ''
  for(20000, i, '#step')
  io.outln(str.len(s))
  OK
#step
  s = str.concat('x', 'y')
  next
FL
    ;;
  collection_pool_churn)
    cat > "$SRC" <<'FL'
use native list with L
use native obj with O
#main
  xs = []
  o = {}
  for(5000, i, '#step')
  io.outln(L.len(xs))
  io.outln(O.len(o))
  OK
#step
  xs = L.pack(i, i + 1, i + 2)
  o = { qty: i, label: 'item' }
  next
FL
    ;;
  list_push_cow)
    cat > "$SRC" <<'FL'
use native list with L
#main
  xs = []
  for(1500, i, '#step')
  io.outln(L.len(xs))
  OK
#step
  xs = L.push(xs, i)
  next
FL
    ;;
  list_set_remove_cow)
    cat > "$SRC" <<'FL'
use native list with L
#main
  xs = [0, 1, 2, 3, 4, 5, 6, 7]
  for(5000, i, '#step')
  io.outln(L.len(xs))
  OK
#step
  xs = L.set(xs, 0, i)
  xs = L.remove_at(xs, 7)
  next
FL
    ;;
  object_set_cow)
    cat > "$SRC" <<'FL'
use native obj with O
#main
  o = { qty: 0, label: 'item', active: true }
  for(10000, i, '#step')
  O.get(o, 'qty') -> result
  io.outln(result)
  OK
#step
  o = O.set(o, 'qty', i)
  next
FL
    ;;
  vars_16) make_vars_case 16 ;;
  vars_64) make_vars_case 64 ;;
  vars_256) make_vars_case 256 ;;
  vars_1024) make_vars_case 1024 ;;
  nested_collections)
    cat > "$SRC" <<'FL'
use native list with L
use native obj with O
#main
  data = { user: { scores: [[1, 2], [3, 4], [5, 6]] } }
  for(20000, i, '#step')
  io.outln(result)
  OK
#step
  O.get(data, 'user.scores') -> rows
  L.get(rows, 1) -> row
  L.get(row, 1) -> result
  next
FL
    ;;
  handle_churn)
    cat > "$SRC" <<'FL'
use native list with L
use native obj with O
#main
  xs = []
  o = {}
  for(10000, i, '#step')
  io.outln(L.len(xs))
  io.outln(O.len(o))
  OK
#step
  xs = L.pack(i, i + 1)
  o = O.set(o, 'qty', i)
  next
FL
    ;;
  *) echo "unknown perf scenario: $SCENARIO" >&2; exit 2 ;;
esac

# Compilation is measured and reported separately; execution timings below never include it.
compile_line=$("$FLC" --time -o "$FB" "$SRC" 2>&1 >/dev/null | grep 'flc: time_ms' | tail -n 1)
compile=$(printf '%s\n' "$compile_line" | tr ' ' '\n' | sed -n 's/^total=//p' | head -n 1)
[ -n "$compile" ] || { echo "perf: failed to parse compile timing for $SCENARIO" >&2; exit 1; }

parse_run_ms() {
    run=$(printf '%s\n' "$1" | tr ' ' '\n' | sed -n 's/^run=//p' | head -n 1)
    [ -n "$run" ] || return 1
    case "$run" in
      *:*,*) m=$(printf '%s' "$run" | cut -d: -f1); rest=$(printf '%s' "$run" | cut -d: -f2); s=$(printf '%s' "$rest" | cut -d, -f1); ms=$(printf '%s' "$rest" | cut -d, -f2); echo $((m * 60000 + s * 1000 + ms)) ;;
      *) echo "$run" ;;
    esac
}
best=2147483647
total=0
i=1
while [ "$i" -le "$ROUNDS" ]; do
    line=$("$FLVM" --time "$FB" </dev/null 2>&1 >/dev/null | grep 'flvm: time_ms' | tail -n 1 || true)
    ms=$(parse_run_ms "$line") || { echo "perf: failed to parse runtime timing for $SCENARIO" >&2; exit 1; }
    [ "$ms" -lt "$best" ] && best=$ms
    total=$((total + ms))
    i=$((i + 1))
done
avg=$((total / ROUNDS))
echo "perf: scenario=$SCENARIO compile_ms=$compile best_run_ms=$best avg_run_ms=$avg rounds=$ROUNDS" >&2
