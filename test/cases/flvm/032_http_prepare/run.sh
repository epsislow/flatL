#!/bin/sh
set -eu

root=$1
tmp=$2
cc=${CC:-gcc}

# Prefer pkg-config when it is executable and knows libcurl. Some minimal or
# restricted environments ship a non-executable pkg-config symlink; in that
# case use the same dynamic-link fallback as the top-level Makefile.
curl_flags=-lcurl
if pkg_config=$(command -v pkg-config 2>/dev/null); then
    if [ -x "$pkg_config" ]; then
        if resolved_flags=$("$pkg_config" --cflags --libs libcurl 2>/dev/null) &&
           [ -n "$resolved_flags" ]; then
            curl_flags=$resolved_flags
        fi
    fi
fi

# curl_flags intentionally undergoes shell word splitting into compiler flags.
# shellcheck disable=SC2086
"$cc" -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror \
    -I"$root/include" \
    -o "$tmp/http_prepare_test" \
    "$root/test/cases/flvm/032_http_prepare/http_prepare_test.c" \
    "$root/src/http/flat_http_curl.c" \
    $curl_flags

"$tmp/http_prepare_test"
