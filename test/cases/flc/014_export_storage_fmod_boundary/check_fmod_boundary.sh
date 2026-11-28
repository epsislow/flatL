#!/bin/sh
set -eu
root=$1
tmp=$2
flc=${FLC_BIN:-$root/bin/flc}
mkdir -p "$tmp/lib"
cp "$root/blocks/executor.fl" "$tmp/executor.fl"

cat > "$tmp/lib/max.fmod" <<EOM
mod('maxexports')
use "../executor.fl" as #fl_
func:executor #fl_executor.global.scope
EOM
i=1
while [ "$i" -le 256 ]; do
    printf 'export fn%s\n' "$i" >> "$tmp/lib/max.fmod"
    i=$((i + 1))
done
i=1
while [ "$i" -le 256 ]; do
    cat >> "$tmp/lib/max.fmod" <<EOM

def fn$i(a) -> r
  r = a
  return
EOM
    i=$((i + 1))
done
cat > "$tmp/max-entry.fl" <<EOM
use fmod "lib/max.fmod" as m
use "executor.fl" as #fl_
use native vcx with V
#main
  m.fn256(9) -> result
  OK
EOM
"$flc" -o "$tmp/max-exports.fb" "$tmp/max-entry.fl"
test -s "$tmp/max-exports.fb"

cat > "$tmp/lib/missing.fmod" <<EOM
mod('missingexport')
use "../executor.fl" as #fl_
func:executor #fl_executor.global.scope
export missing
EOM
cat > "$tmp/missing-entry.fl" <<'EOM'
use fmod "lib/missing.fmod" as m
#main
  OK
EOM
if "$flc" -o "$tmp/missing-export.fb" "$tmp/missing-entry.fl" >"$tmp/missing-export.log" 2>&1; then
    echo "fmod export without def unexpectedly compiled" >&2
    exit 1
fi
grep -F 'fmod: export missing has no def' "$tmp/missing-export.log" >/dev/null

echo export-fmod-boundary-ok
