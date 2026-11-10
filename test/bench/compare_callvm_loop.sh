#!/bin/sh
# AM-1 microbench: compare func:callVM 0 vs 1 on a tight inc() loop.
# Usage: compare_callvm_loop.sh REPO_ROOT
set -e

ROOT="${1:?repo root}"
FLC="$ROOT/bin/flc"
FLVM="$ROOT/bin/flvm"
FBDUMP="$ROOT/bin/fbdump"
SRC_A="$ROOT/test/cases/bench/001_loop_desugar/program.fl"
SRC_B="$ROOT/test/cases/bench/002_loop_opcode/program.fl"
ROUNDS="${FLAT_CALLVM_BENCH_ROUNDS:-3}"
SLACK_PERCENT="${FLAT_CALLVM_BENCH_SLACK:-5}"

TMP="${TMPDIR:-/tmp}/flatl_callvm_bench_$$"
mkdir -p "$TMP"
FB_A="$TMP/desugar.fb"
FB_B="$TMP/opcode.fb"
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

"$FLC" -o "$FB_A" "$SRC_A"
"$FLC" -o "$FB_B" "$SRC_B"

# Extract run= duration from flvm: time_ms (field ms or M:SS,mmm).
parse_run_ms() {
    line=$1
    run=$(printf '%s\n' "$line" | tr ' ' '\n' | sed -n 's/^run=//p' | head -n 1)
    if [ -z "$run" ]; then
        echo 0
        return 1
    fi
    case "$run" in
        *:*,*)
            m=$(printf '%s' "$run" | cut -d: -f1)
            rest=$(printf '%s' "$run" | cut -d: -f2)
            s=$(printf '%s' "$rest" | cut -d, -f1)
            ms=$(printf '%s' "$rest" | cut -d, -f2)
            echo $((m * 60000 + s * 1000 + ms))
            ;;
        *)
            echo "$run"
            ;;
    esac
}

bench_run_ms() {
    fb=$1
    best=999999999
    i=1
    while [ "$i" -le "$ROUNDS" ]; do
        line=$(printf '' | "$FLVM" --time "$fb" 2>&1 >/dev/null | sed -n 's/.*flvm: time_ms.*/&/p' | head -n 1)
        ms=$(parse_run_ms "$line") || ms=999999999
        if [ "$ms" -lt "$best" ]; then
            best=$ms
        fi
        i=$((i + 1))
    done
    echo "$best"
}

MS_A=$(bench_run_ms "$FB_A")
MS_B=$(bench_run_ms "$FB_B")

if [ "$MS_A" -le 0 ] || [ "$MS_B" -le 0 ]; then
    echo "callvm_loop: fail (could not parse flvm --time run=)" >&2
    exit 1
fi

# Ratio B/A with integer percent (100 = parity).
ratio_pct=$((MS_B * 100 / MS_A))
max_b=$((MS_A + MS_A * SLACK_PERCENT / 100))

echo "callvm_loop: desugar run_ms=$MS_A opcode run_ms=$MS_B ratio_pct=$ratio_pct rounds=$ROUNDS" >&2

if [ -x "$FBDUMP" ]; then
    echo "callvm_loop: fbdump summary (desugar vs opcode):" >&2
    "$FBDUMP" --summary "$FB_A" 2>&1 | sed -n '/^=== summary ===/,$p' | sed 's/^/  A /' >&2 || true
    "$FBDUMP" --summary "$FB_B" 2>&1 | sed -n '/^=== summary ===/,$p' | sed 's/^/  B /' >&2 || true
fi

if [ "$MS_B" -gt "$max_b" ]; then
    echo "callvm_loop: fail (opcode ${MS_B}ms > desugar ${MS_A}ms + ${SLACK_PERCENT}% slack)" >&2
  exit 1
fi

echo "callvm_loop: ok (opcode <= desugar + ${SLACK_PERCENT}% slack; AM-1 P1 ~20% faster is informational only)" >&2
exit 0
