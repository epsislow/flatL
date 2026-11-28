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

write_var_blocks() {
    count=$1
    value=$2
    prefix=$3
    blocks=$(((count + 99) / 100))
    long_x=$(printf '%0128d' 0 | tr '0' 'x')
    {
        echo '#main'
        echo '  jump #init_000'
        block=0
        while [ "$block" -lt "$blocks" ]; do
            printf '#init_%03d\n' "$block"
            first=$((block * 100))
            last=$((first + 100))
            [ "$last" -gt "$count" ] && last=$count
            i=$first
            while [ "$i" -lt "$last" ]; do
                if [ "$value" = "string" ]; then
                    printf "  %s%04d = '%s-%04d'\n" "$prefix" "$i" "$long_x" "$i"
                elif [ "$value" = "list" ]; then
                    printf '  %s%04d = []\n' "$prefix" "$i"
                elif [ "$value" = "object" ]; then
                    printf '  %s%04d = {}\n' "$prefix" "$i"
                else
                    printf '  %s%04d = %d\n' "$prefix" "$i" "$i"
                fi
                i=$((i + 1))
            done
            if [ "$block" -lt $((blocks - 1)) ]; then
                printf '  jump #init_%03d\n' "$((block + 1))"
            else
                echo '  jump #run'
            fi
            block=$((block + 1))
        done
    } > "$SRC"
}

make_vars_case() {
    count=$1
    write_var_blocks "$count" number v
    {
        echo '#run'
        echo "  for(20000, i, '#step')"
        echo '  io.outln(result)'
        echo '  OK'
        echo '#step'
        printf "  m.calc('v%04d + 1') -> result\n" "$((count - 1))"
        echo '  next'
    } >> "$SRC"
    # Native math variable lookup is intentional in this legacy scenario.
    { echo 'use native math with m'; cat "$SRC"; } > "$TMP/vars_with_math.fl"
    mv "$TMP/vars_with_math.fl" "$SRC"
}

make_pool_vars_case() {
    count=$1
    value=$2
    write_var_blocks "$count" "$value" v
    {
        echo '#run'
        echo "  for(10000, i, '#step')"
        echo '  io.outln(i)'
        echo '  OK'
        echo '#step'
        echo '  next'
    } >> "$SRC"
}

make_collection_scan_case() {
    count=$1
    value=$2
    write_var_blocks "$count" "$value" v
    {
        echo '#run'
        echo "  for(10000, i, '#step')"
        echo '  io.outln(i)'
        echo '  OK'
        echo '#step'
        echo '  next'
    } >> "$SRC"
}

make_vm_lookup_case() {
    count=$1
    target=$2
    write_var_blocks "$count" number v
    {
        echo '#run'
        echo '  x = 0'
        echo "  for(50000, i, '#step')"
        echo '  io.outln(x)'
        echo '  OK'
        echo '#step'
        printf '  x = v%04d\n' "$target"
        echo '  next'
    } >> "$SRC"
}

make_vcx_lookup_case() {
    count=$1
    target=$2
    {
        echo 'use native vcx with V'
        echo '#main'
        echo '  c = V.create(300)'
        echo '  V.set(c)'
        echo '  jump #init_000'
        block=0
        blocks=$(((count + 99) / 100))
        while [ "$block" -lt "$blocks" ]; do
            printf '#init_%03d\n' "$block"
            first=$((block * 100))
            last=$((first + 100))
            [ "$last" -gt "$count" ] && last=$count
            i=$first
            while [ "$i" -lt "$last" ]; do
                printf '  v%04d = %d\n' "$i" "$i"
                i=$((i + 1))
            done
            if [ "$block" -lt $((blocks - 1)) ]; then
                printf '  jump #init_%03d\n' "$((block + 1))"
            else
                echo '  V.set(1)'
                echo '  jump #run'
            fi
            block=$((block + 1))
        done
        echo '#run'
        echo "  for(20000, i, '#step')"
        echo '  io.outln(result)'
        echo '  OK'
        echo '#step'
        printf "  V.var(c, 'v%04d') -> result\n" "$target"
        echo '  next'
    } > "$SRC"
}

make_list_cow_case() {
    count=$1
    {
        echo 'use native list with L'
        echo '#main'
        printf '  xs = L.range(0, %d)\n' "$((count - 1))"
        echo "  for(2000, i, '#step')"
        echo '  io.outln(L.len(xs))'
        echo '  OK'
        echo '#step'
        echo '  xs = L.set(xs, 0, i)'
        echo '  next'
    } > "$SRC"
}

make_object_cow_case() {
    count=$1
    {
        echo 'use native obj with O'
        echo '#main'
        echo '  o = {}'
        echo '  jump #obj_init_000'
        block=0
        blocks=$(((count + 99) / 100))
        while [ "$block" -lt "$blocks" ]; do
            printf '#obj_init_%03d\n' "$block"
            first=$((block * 100))
            last=$((first + 100))
            [ "$last" -gt "$count" ] && last=$count
            i=$first
            while [ "$i" -lt "$last" ]; do
                printf "  o = O.set(o, 'k%03d', %d)\n" "$i" "$i"
                i=$((i + 1))
            done
            if [ "$block" -lt $((blocks - 1)) ]; then
                printf '  jump #obj_init_%03d\n' "$((block + 1))"
            else
                echo '  jump #run'
            fi
            block=$((block + 1))
        done
        echo '#run'
        echo "  for(100, i, '#step')"
        echo "  O.get(o, 'k000') -> result"
        echo '  io.outln(result)'
        echo '  OK'
        echo '#step'
        echo "  o = O.set(o, 'k000', i)"
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
  for(200, i, '#step')
  io.outln(L.len(xs))
  io.outln(O.len(o))
  OK
#step
  L.pack(i, 1, 2) -> xs
  o = { qty: i, label: 'item' }
  next
FL
    ;;
  list_push_cow)
    cat > "$SRC" <<'FL'
use native list with L
#main
  xs = []
  for(500, i, '#step')
  io.outln(L.len(xs))
  OK
#step
  L.push(xs, i) -> xs
  next
FL
    ;;
  list_set_remove_cow)
    cat > "$SRC" <<'FL'
use native list with L
#main
  xs = [0, 1, 2, 3, 4, 5, 6, 7]
  for(500, i, '#step')
  io.outln(L.len(xs))
  OK
#step
  L.set(xs, 0, i) -> xs
  L.remove_at(xs, 7) -> tmp
  L.push(tmp, i) -> xs
  next
FL
    ;;
  object_set_cow)
    cat > "$SRC" <<'FL'
use native obj with O
#main
  o = { qty: 0, label: 'item', active: true }
  for(200, i, '#step')
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
  for(200, i, '#step')
  io.outln(L.len(xs))
  io.outln(O.len(o))
  OK
#step
  xs = L.pack(i, 1)
  o = O.set(o, 'qty', i)
  next
FL
    ;;
  pool_strings_256) make_pool_vars_case 256 string ;;
  pool_numbers_256) make_pool_vars_case 256 number ;;
  pool_strings_1024) make_pool_vars_case 1024 string ;;
  pool_numbers_1024) make_pool_vars_case 1024 number ;;
  coll_lists_128) make_collection_scan_case 128 list ;;
  coll_objects_128) make_collection_scan_case 128 object ;;
  coll_numbers_128) make_collection_scan_case 128 number ;;
  vm_lookup_first_256) make_vm_lookup_case 256 0 ;;
  vm_lookup_last_256) make_vm_lookup_case 256 255 ;;
  vcx_lookup_first_256) make_vcx_lookup_case 256 0 ;;
  vcx_lookup_last_256) make_vcx_lookup_case 256 255 ;;
  list_cow_8) make_list_cow_case 8 ;;
  list_cow_64) make_list_cow_case 64 ;;
  list_cow_256) make_list_cow_case 256 ;;
  list_cow_1024) make_list_cow_case 1024 ;;
  object_cow_8) make_object_cow_case 8 ;;
  object_cow_32) make_object_cow_case 32 ;;
  object_cow_64) make_object_cow_case 64 ;;
  object_cow_96) make_object_cow_case 96 ;;
  *) echo "unknown perf scenario: $SCENARIO" >&2; exit 2 ;;
esac

# Compilation is measured and reported separately; execution timings below never include it.
if ! "$FLC" --time -o "$FB" "$SRC" >"$TMP/compile.log" 2>&1; then
    cat "$TMP/compile.log" >&2
    echo "perf: compilation failed for $SCENARIO" >&2
    exit 1
fi
compile_line=$(grep 'flc: time_ms' "$TMP/compile.log" | tail -n 1)
compile=$(printf '%s\n' "$compile_line" | tr ' ' '\n' | sed -n 's/^total=//p' | head -n 1)
[ -n "$compile" ] || { cat "$TMP/compile.log" >&2; echo "perf: failed to parse compile timing for $SCENARIO" >&2; exit 1; }

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
    if ! "$FLVM" --time "$FB" </dev/null >"$TMP/run.log" 2>&1; then
        cat "$TMP/run.log" >&2
        echo "perf: execution failed for $SCENARIO" >&2
        exit 1
    fi
    line=$(grep 'flvm: time_ms' "$TMP/run.log" | tail -n 1)
    ms=$(parse_run_ms "$line") || { cat "$TMP/run.log" >&2; echo "perf: failed to parse runtime timing for $SCENARIO" >&2; exit 1; }
    [ "$ms" -lt "$best" ] && best=$ms
    total=$((total + ms))
    i=$((i + 1))
done
avg=$((total / ROUNDS))
echo "perf: scenario=$SCENARIO compile_ms=$compile best_run_ms=$best avg_run_ms=$avg rounds=$ROUNDS" >&2
