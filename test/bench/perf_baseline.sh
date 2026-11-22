#!/bin/sh
# Informational FlatL VM performance baselines.
# Usage: perf_baseline.sh REPO_ROOT
set -eu
ROOT="${1:?repo root}"
FLC="$ROOT/bin/flc"
FLVM="$ROOT/bin/flvm"
ROUNDS="${FLAT_PERF_ROUNDS:-5}"
TMP="${TMPDIR:-/tmp}/flatl_perf_baseline_$$"
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

parse_run_ms() {
    line=$1
    run=$(printf '%s\n' "$line" | tr ' ' '\n' | sed -n 's/^run=//p' | head -n 1)
    [ -n "$run" ] || return 1
    case "$run" in
        *:*,*)
            m=$(printf '%s' "$run" | cut -d: -f1)
            rest=$(printf '%s' "$run" | cut -d: -f2)
            s=$(printf '%s' "$rest" | cut -d, -f1)
            ms=$(printf '%s' "$rest" | cut -d, -f2)
            echo $((m * 60000 + s * 1000 + ms)) ;;
        *) echo "$run" ;;
    esac
}

bench() {
    name=$1
    src=$2
    fb="$TMP/$name.fb"
    "$FLC" -o "$fb" "$src"
    best=2147483647
    total=0
    i=1
    while [ "$i" -le "$ROUNDS" ]; do
        line=$("$FLVM" --time "$fb" </dev/null 2>&1 >/dev/null | grep 'flvm: time_ms' | tail -n 1 || true)
        ms=$(parse_run_ms "$line") || { echo "perf: failed to parse timing for $name" >&2; exit 1; }
        [ "$ms" -lt "$best" ] && best=$ms
        total=$((total + ms))
        i=$((i + 1))
    done
    avg=$((total / ROUNDS))
    echo "perf: scenario=$name best_ms=$best avg_ms=$avg rounds=$ROUNDS" >&2
}

# 1) VM loop with no dynamic string/collection allocations: exposes per-op VM overhead.
cat > "$TMP/pool_idle.fl" <<'FL'
#main
  for(100000, i, '#step')
  OK
#step
  next
FL

# 2) Repeated string creation/rebinding: exercises string-pool allocation/reaping.
cat > "$TMP/pool_strings.fl" <<'FL'
#main
  s = ''
  for(20000, i, '#step')
  io.outln(str.len(s))
  OK
#step
  s = str.concat('x', 'y')
  next
FL

# 3) Repeated COW list mutation: exercises collection allocation, cloning and release.
cat > "$TMP/collections.fl" <<'FL'
use native list with L
#main
  xs = []
  for(1000, i, '#step')
  L.len(xs) -> n
  io.outln(n)
  OK
#step
  L.push(xs, i) -> xs
  next
FL

# 4) Expression variable lookup at different table sizes. The expression evaluator
#    resolves a late variable name after many earlier names have been registered.
make_vars_case() {
    count=$1
    file=$2
    {
        echo 'use native math with m'
        echo '#main'
        i=0
        while [ "$i" -lt "$count" ]; do
            printf '  v%04d = %d\n' "$i" "$i"
            i=$((i + 1))
        done
        echo '  for(20000, i, '\''#step'\'')'
        echo '  io.outln(result)'
        echo '  OK'
        echo '#step'
        printf "  m.calc('v%04d + 1') -> result\n" "$((count - 1))"
        echo '  next'
    } > "$file"
}
make_vars_case 16 "$TMP/vars_16.fl"
make_vars_case 64 "$TMP/vars_64.fl"
make_vars_case 96 "$TMP/vars_96.fl"

bench pool_idle "$TMP/pool_idle.fl"
bench pool_strings "$TMP/pool_strings.fl"
bench collections "$TMP/collections.fl"
bench vars_16 "$TMP/vars_16.fl"
bench vars_64 "$TMP/vars_64.fl"
bench vars_96 "$TMP/vars_96.fl"
echo "perf: ok (informational baseline; no timing thresholds)" >&2
