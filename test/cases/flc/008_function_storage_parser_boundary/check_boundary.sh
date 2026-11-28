#!/bin/sh
set -eu
root=$1
tmp=$2
flc=${FLC_BIN:-$root/bin/flc}

for count in 256 257; do
    src="$tmp/functions-$count.fl"
    out="$tmp/functions-$count.fb"
    : > "$src"
    i=1
    while [ "$i" -le "$count" ]; do
        printf 'def fn%s() -> r\n  r = 1\n  return\n\n' "$i" >> "$src"
        i=$((i + 1))
    done
    printf '#main\n  OK\n' >> "$src"
    if [ "$count" -eq 256 ]; then
        "$flc" -o "$out" "$src"
        test -s "$out"
    else
        if "$flc" -o "$out" "$src" >"$tmp/functions-$count.log" 2>&1; then
            echo "257-function input unexpectedly compiled" >&2
            exit 1
        fi
        grep -F 'too many functions' "$tmp/functions-$count.log" >/dev/null
    fi
done

echo function-parser-boundary-ok
