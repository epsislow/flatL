#!/bin/sh
set -eu
root=$1
tmp=$2
flc=${FLC_BIN:-$root/bin/flc}

for module_count in 31 32; do
    mkdir -p "$tmp/lib"
    i=1
    while [ "$i" -le "$module_count" ]; do
        cat > "$tmp/lib/m$i.fmod" <<EOM
mod('lib$i')
use native math with n$i
export f$i
def f$i(x) -> r
  n$i.calc('x + 1') -> r
  return
EOM
        i=$((i + 1))
    done

    {
        i=1
        while [ "$i" -le "$module_count" ]; do
            printf 'use fmod "lib/m%s.fmod" as m%s\n' "$i" "$i"
            i=$((i + 1))
        done
        printf '#main\n  OK\n'
    } > "$tmp/main.fl"

    if [ "$module_count" -eq 31 ]; then
        "$flc" -o "$tmp/out.fb" "$tmp/main.fl"
        test -s "$tmp/out.fb"
    else
        if "$flc" -o "$tmp/out.fb" "$tmp/main.fl" >"$tmp/native-unit-overflow.log" 2>&1; then
            echo "33 linked native units unexpectedly compiled" >&2
            exit 1
        fi
        grep -F 'too many linked native units' "$tmp/native-unit-overflow.log" >/dev/null
    fi
    rm -rf "$tmp/lib"
done

echo native-unit-link-boundary-ok
