#!/bin/sh
# Print path to a Linux Node binary (skip WSL shims that exec Windows node.exe).

is_windows_node_shim() {
  c=$1
  case "$c" in
    *node.exe | *.exe) return 0 ;;
  esac
  if [ -f "$c" ] && head -c 2 "$c" 2>/dev/null | grep -q '^#!'; then
    if grep -qi 'node\.exe' "$c" 2>/dev/null; then
      return 0
    fi
  fi
  return 1
}

node_platform() {
  c=$1
  "$c" -p 'process.platform' 2>/dev/null | tr -d '\r\n'
}

is_elf_executable() {
  c=$1
  file -b "$c" 2>/dev/null | grep -qi 'ELF'
}

try_node() {
  c=$1
  [ -n "$c" ] && [ -x "$c" ] || return 1
  if is_windows_node_shim "$c"; then
    return 1
  fi
  if ! "$c" --version >/dev/null 2>&1; then
    return 1
  fi
  plat=$(node_platform "$c")
  if [ "$plat" = "linux" ]; then
    printf '%s\n' "$c"
    return 0
  fi
  case "$c" in
    /usr/bin/node | /usr/bin/nodejs | /usr/local/bin/node | /usr/local/bin/nodejs | /snap/bin/node)
      if is_elf_executable "$c"; then
        printf '%s\n' "$c"
        return 0
      fi
      ;;
  esac
  return 1
}

for c in /usr/bin/node /usr/local/bin/node /usr/bin/nodejs /usr/local/bin/nodejs /snap/bin/node; do
  if try_node "$c"; then
    exit 0
  fi
done

if [ -n "${NVM_DIR:-}" ] && [ -d "$NVM_DIR/versions/node" ]; then
  for c in "$NVM_DIR"/versions/node/*/bin/node; do
    [ -e "$c" ] || continue
    if try_node "$c"; then
      exit 0
    fi
  done
fi

PATH_SAVED=$PATH
PATH=/usr/bin:/bin:/usr/local/bin:/snap/bin
export PATH
for c in $(command -v node 2>/dev/null) $(command -v nodejs 2>/dev/null); do
  [ -n "$c" ] || continue
  if try_node "$c"; then
    exit 0
  fi
done
PATH=$PATH_SAVED
export PATH

exit 1
