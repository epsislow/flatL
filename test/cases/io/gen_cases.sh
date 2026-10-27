#!/usr/bin/env bash
# Generates io/* test cases. Run from repo root: test/cases/io/gen_cases.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
IO="$ROOT/test/cases/io"

mkcase() {
  local id="$1" desc="$2" prog="$3" run="$4" expect="$5" out="${6:-}" err="${7:-}"
  local dir
  dir=$(printf "%s/%03d_%s" "$IO" "$id" "$(echo "$desc" | tr ' A-Z' '_a-z' | tr -cd 'a-z0-9_')")
  mkdir -p "$dir"
  printf '%s\n' "$prog" > "$dir/program.fl"
  cat > "$dir/case.toml" <<EOF
suite = "io"
id = $id
desc = "$desc"
kind = "pipeline"

source = "program.fl"
compile = "{root}/bin/flc -o {tmp}/out.fb {src}"
run = "$run"
expect_exit = $expect
EOF
  if [[ -n "$out" ]]; then
    echo "stdout_contains = \"$out\"" >> "$dir/case.toml"
  fi
  if [[ -n "$err" ]]; then
    echo "stderr_contains = \"$err\"" >> "$dir/case.toml"
  fi
}

VM="{root}/bin/flvm {tmp}/out.fb"

mkcase 1 "outln string" "#main
  io.outln('hi')
  OK" "printf '' | $VM" 0 "hi"

mkcase 2 "out no newline" "#main
  io.out('ab')
  io.out('cd')
  io.outln('')
  OK" "printf '' | $VM" 0 "abcd"

mkcase 3 "outvar number" "#main
  n = 42
  io.outvar(n)
  OK" "printf '' | $VM" 0 "42"

mkcase 4 "out bool true" "#main
  ok = true
  io.outln(ok)
  OK" "printf '' | $VM" 0 "true"

mkcase 5 "out list" "#main
  xs = [1, 2]
  io.outln(xs)
  OK" "printf '' | $VM" 0 "[1, 2]"

mkcase 6 "out object" "#main
  o = { a: 1, b: 'x' }
  io.outln(o)
  OK" "printf '' | $VM" 0 "'a': 1"

mkcase 7 "inln arrow" "#main
  io.inln() -> line
  io.outln(line)
  OK" "printf 'hello\n' | $VM" 0 "hello"

mkcase 8 "inln assign" "#main
  line = io.inln()
  io.outln(line)
  OK" "printf 'world\n' | $VM" 0 "world"

mkcase 9 "inln prompt" "#main
  io.inln('Name:') -> name
  io.outln(name)
  OK" "printf 'Ann\n' | $VM" 0 "Name:Ann"

mkcase 10 "in_num" "#main
  io.in_num() -> n
  io.outln(n)
  OK" "printf '3.5\n' | $VM" 0 "3.5"

mkcase 11 "in_bool yes" "#main
  io.in_bool() -> ok
  io.outln(ok)
  OK" "printf 'yes\n' | $VM" 0 "true"

mkcase 12 "in_bool n" "#main
  io.in_bool() -> ok
  io.outln(ok)
  OK" "printf 'n\n' | $VM" 0 "false"

mkcase 13 "in_bool cancel q" "#main
  io.in_bool() -> ok
  io.outln('bad')
  OK

#panic.any
  io.outln('cancelled')
  OK" "printf 'q\n' | $VM" 0 "cancelled"

mkcase 14 "inln empty cancel" "#main
  io.inln() -> x
  OK

#panic.any
  io.outln('empty')
  OK" "printf '\n' | $VM" 0 "empty"

mkcase 15 "str to num arrow" "#main
  raw = '12'
  str.to_num(raw) -> n
  io.outln(n)
  OK" "printf '' | $VM" 0 "12"

mkcase 16 "str to bool assign" "#main
  b = str.to_bool('on')
  io.outln(b)
  OK" "printf '' | $VM" 0 "true"

mkcase 17 "str to num invalid assign fail" "#main
  n = 0
  n = str.to_num('not-a-num')
  n == 0
  OK" "printf '' | $VM" 0 ""

mkcase 18 "in_num invalid panic" "#main
  io.in_num() -> n
  OK

#panic.any
  io.outln('parse')
  OK" "printf 'xx\n' | $VM" 0 "parse"

mkcase 19 "foreach out" "#main
  xs = ['a', 'b']
  foreach(xs, k, '#loop')
  OK
#loop
  io.out(k)
  return" "printf '' | $VM" 0 "ab"

mkcase 20 "unhandled panic stderr" "#main
  io.inln() -> x
  OK" "printf '\n' | $VM" 1 "unhandled panic"

mkcase 21 "bool tokens false off" "#main
  b = str.to_bool('OFF')
  io.outln(b)
  OK" "printf '' | $VM" 0 "false"

mkcase 22 "bool token t" "#main
  b = str.to_bool('T')
  io.outln(b)
  OK" "printf '' | $VM" 0 "true"

mkcase 23 "in_num prompt" "#main
  io.in_num('Price:') -> p
  io.outln(p)
  OK" "printf '9\n' | $VM" 0 "Price:9"

mkcase 24 "assign arrow in_num" "#main
  io.in_num() -> x
  x == 2
  OK" "printf '2\n' | $VM" 0 ""

mkcase 25 "out number no ln" "#main
  io.out(1)
  io.out(2)
  io.outln(3)
  OK" "printf '' | $VM" 0 "123"

mkcase 26 "inln cancel word is data" "#main
  io.inln() -> s
  io.outln(s)
  OK" "printf 'cancel\n' | $VM" 0 "cancel"

mkcase 27 "panic eof" "#main
  io.inln() -> x
  OK

#panic.any
  io.outln('eof')
  OK" "printf '' | $VM" 0 "eof"

mkcase 28 "str to bool invalid" "#main
  ok = false
  ok = str.to_bool('maybe')
  ok == false
  OK" "printf '' | $VM" 0 ""

mkcase 29 "values outvar bool" "#main
  ok = false
  io.outvar(ok)
  OK" "printf '' | $VM" 0 "false"

mkcase 30 "mixed out and guard" "#main
  io.outln('start')
  1 == 1
  io.outln('end')
  OK" "printf '' | $VM" 0 "start"

echo "Generated io cases under $IO"
