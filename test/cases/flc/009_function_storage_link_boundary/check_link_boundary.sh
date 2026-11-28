#!/bin/sh
set -eu
root=$1
tmp=$2
flc=${FLC_BIN:-$root/bin/flc}

for module_count in 128 129; do
    entry_count=128
    mkdir -p "$tmp/lib"
    module_executor=$(realpath --relative-to="$tmp/lib" "$root/blocks/executor.fl")
    entry_executor=$(realpath --relative-to="$tmp" "$root/blocks/executor.fl")

    {
        printf "mod('lib')\nuse \"%s\" as #fl_\nfunc:executor #fl_executor.global.scope\nfunc:callVM 1\nexport modf1\n\n" "$module_executor"
        i=1
        while [ "$i" -le "$module_count" ]; do
            printf 'def modf%s(x) -> r\n  r = x\n  return\n\n' "$i"
            i=$((i + 1))
        done
    } > "$tmp/lib/m.fmod"

    {
        printf 'use fmod "lib/m.fmod" as m\nuse "%s" as #fl_\nfunc:executor #fl_executor.global.scope\nfunc:callVM 1\n' "$entry_executor"
        i=1
        while [ "$i" -le "$entry_count" ]; do
            printf 'def entryf%s(x) -> r\n  r = x\n  return\n\n' "$i"
            i=$((i + 1))
        done
        printf '#main\n  OK\n'
    } > "$tmp/main.fl"

    if [ "$module_count" -eq 128 ]; then
        "$flc" -o "$tmp/out.fb" "$tmp/main.fl"
        test -s "$tmp/out.fb"
    else
        if "$flc" -o "$tmp/out.fb" "$tmp/main.fl" >"$tmp/link-overflow.log" 2>&1; then
            echo "257 linked functions unexpectedly compiled" >&2
            exit 1
        fi
        grep -F 'too many functions after link' "$tmp/link-overflow.log" >/dev/null
    fi
done

echo function-link-boundary-ok
