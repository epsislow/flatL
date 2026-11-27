#!/bin/sh
set -eu
root=$1
tmp=$2
flc=${FLC_BIN:-$root/bin/flc}

write_program() {
    count=$1
    src=$2
    duplicate=$3
    : > "$src"
    printf 'export\nexport , ,\n' >> "$src"
    i=1
    while [ "$i" -le "$count" ]; do
        printf 'export exp%s\n' "$i" >> "$src"
        i=$((i + 1))
    done
    if [ "$duplicate" -eq 1 ]; then
        printf 'export exp1, exp2, exp1\n' >> "$src"
    fi
    printf '#main\n  OK\n' >> "$src"
}

write_program 256 "$tmp/exports-256.fl" 1
"$flc" -o "$tmp/exports-256.fb" "$tmp/exports-256.fl"
test -s "$tmp/exports-256.fb"

write_program 257 "$tmp/exports-257.fl" 0
if "$flc" -o "$tmp/exports-257.fb" "$tmp/exports-257.fl" >"$tmp/exports-257.log" 2>&1; then
    echo "257 distinct exports unexpectedly compiled" >&2
    exit 1
fi
grep -F 'too many export symbols' "$tmp/exports-257.log" >/dev/null

echo export-parser-boundary-ok
