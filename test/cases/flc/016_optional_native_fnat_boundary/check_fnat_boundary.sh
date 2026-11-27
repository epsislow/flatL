#!/bin/sh
set -eu
root=$1
tmp=$2
flc=${FLC_BIN:-$root/bin/flc}
fbdump=${FBDUMP_BIN:-$root/bin/fbdump}
mkdir -p "$tmp/lib"
mods='http dom env path fs enc re hash bytes math math.trig math.stats math.mdim list obj vcx executor csv task channel'

# The first core declaration must remain available in FNUN source metadata but
# must not enter the optional FNAT union. Repeated http is deduplicated and the
# first-seen order is preserved in the serialized manifest.
i=1
for module in $mods; do
    {
        printf "mod('optional_unit_%s')\n" "$i"
        if [ "$i" -eq 1 ]; then
            printf 'use native mem with M\n'
        fi
        printf 'use native %s with n%s\n' "$module" "$i"
        printf 'export fn%s\n' "$i"
        printf 'def fn%s(x) -> r\n' "$i"
        printf '  r = x\n'
        printf '  return\n'
    } > "$tmp/lib/m$i.fmod"
    i=$((i + 1))
done
# A final unit repeats the first optional module under a different alias.
cat > "$tmp/lib/duplicate.fmod" <<'EOM'
mod('optional_duplicate')
use native http with http_again
export duplicate
def duplicate(x) -> r
  r = x
  return
EOM

{
    i=1
    for module in $mods; do
        printf 'use fmod "lib/m%s.fmod" as m%s\n' "$i" "$i"
        i=$((i + 1))
    done
    printf 'use fmod "lib/duplicate.fmod" as dup\n'
    printf '#main\n  OK\n'
} > "$tmp/main.fl"

"$flc" -o "$tmp/manifest.fb" "$tmp/main.fl"
"$fbdump" --sections-only "$tmp/manifest.fb" > "$tmp/manifest.dump"
sed -n '/=== optional natives (FNAT) ===/,/=== FNUN ===/p' "$tmp/manifest.dump" |
    sed '1d;$d; s/^  //; /^$/d' > "$tmp/manifest.actual"
printf '%s\n' $mods > "$tmp/manifest.expected"
cmp "$tmp/manifest.expected" "$tmp/manifest.actual"
if grep -Fx 'mem' "$tmp/manifest.actual" >/dev/null; then
    echo 'core module unexpectedly serialized in FNAT' >&2
    exit 1
fi
if grep -F 'http with' "$tmp/manifest.actual" >/dev/null; then
    echo 'FGIR union alias unexpectedly serialized in FNAT' >&2
    exit 1
fi
# FNUN must retain both source aliases, proving that only the FGIR union was
# normalized and that per-unit declaration data was not modified.
grep -F 'http with n1' "$tmp/manifest.dump" >/dev/null
grep -F 'http with http_again' "$tmp/manifest.dump" >/dev/null
grep -F 'mem with M' "$tmp/manifest.dump" >/dev/null

echo optional-native-fnat-boundary-ok
