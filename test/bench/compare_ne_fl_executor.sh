#!/bin/sh
# Microbench: #ne_ (native-exec) vs #fl_ (executor.fl) with flvm --time.
# Usage: compare_ne_fl_executor.sh REPO_ROOT
set -e

ROOT="${1:?repo root}"
FLC="$ROOT/bin/flc"
FLVM="$ROOT/bin/flvm"
ROUNDS="${FLAT_NE_FL_BENCH_ROUNDS:-3}"
REQUIRE_FASTER="${FLAT_NE_FL_REQUIRE_FASTER:-0}"
SLACK_PERCENT="${FLAT_NE_FL_BENCH_SLACK:-5}"

TMP="${TMPDIR:-/tmp}/flatl_ne_fl_bench_$$"
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

pair() {
    name=$1
    fl_src=$2
    ne_src=$3
    eval "FL_${name}_src=\$fl_src"
    eval "NE_${name}_src=\$ne_src"
}

pair loop_unique \
    "$ROOT/test/cases/bench/004_fl_loop_unique/program.fl" \
    "$ROOT/test/cases/bench/005_ne_loop_unique/program.fl"
pair nested_triple \
    "$ROOT/test/cases/bench/006_fl_nested_unique_loop/program.fl" \
    "$ROOT/test/cases/bench/007_ne_nested_unique_loop/program.fl"
pair mixed_global_unique \
    "$ROOT/test/cases/bench/008_fl_mixed_global_unique_loop/program.fl" \
    "$ROOT/test/cases/bench/009_ne_mixed_global_unique_loop/program.fl"
pair nested_two_inner \
    "$ROOT/test/cases/bench/011_fl_nested_unique_inner_loop/program.fl" \
    "$ROOT/test/cases/bench/010_ne_nested_unique_inner_loop/program.fl"

# shellcheck disable=SC2086
for name in loop_unique nested_triple mixed_global_unique nested_two_inner; do
    eval fl=\$FL_${name}_src
    eval ne=\$NE_${name}_src
    "$FLC" -o "$TMP/${name}_fl.fb" "$fl"
    "$FLC" -o "$TMP/${name}_ne.fb" "$ne"
done

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

fail=0
# shellcheck disable=SC2086
for name in loop_unique nested_triple mixed_global_unique nested_two_inner; do
    ms_fl=$(bench_run_ms "$TMP/${name}_fl.fb")
    ms_ne=$(bench_run_ms "$TMP/${name}_ne.fb")
    if [ "$ms_fl" -le 0 ] || [ "$ms_ne" -le 0 ]; then
        echo "ne_fl_bench: fail ($name: could not parse flvm --time)" >&2
        fail=1
        continue
    fi
    ratio_pct=$((ms_ne * 100 / ms_fl))
    echo "ne_fl_bench: scenario=$name fl_ms=$ms_fl ne_ms=$ms_ne ne_vs_fl_pct=$ratio_pct rounds=$ROUNDS" >&2
  if [ "$REQUIRE_FASTER" = "1" ]; then
        max_ne=$((ms_fl + ms_fl * SLACK_PERCENT / 100))
        if [ "$ms_ne" -gt "$max_ne" ]; then
            echo "ne_fl_bench: fail ($name: ne ${ms_ne}ms > fl ${ms_fl}ms + ${SLACK_PERCENT}% slack)" >&2
            fail=1
        fi
    fi
done

if [ "$fail" -ne 0 ]; then
    exit 1
fi

echo "ne_fl_bench: ok (ne #_ vs fl #_; set FLAT_NE_FL_REQUIRE_FASTER=1 to gate ne <= fl+slack)" >&2
exit 0