# Native module `re`

PCRE2 regular expressions on **STRING** (byte-oriented; UTF flags off by default). **Optional** — declare before use:

```text
use native re
```

Backend: **PCRE2 8-bit**, **no JIT**. Default build bundles sources from `external/pcre2/` (`re_compile=1`). System library: `make … re=1 re_compile=0` (`pkg-config libpcre2-8` on Linux / MSYS2 UCRT64). Disable: `re=0`.

**Availability:** native ✅ · WASM ✅ (bundled PCRE2 required — `re_compile=1`). Matrix: [README.md § WASM](./README.md#wasm-availability).

Recoverable failures → **PANIC** **`re.<fn>…`** unless **`?`** / **`~`** ([SPECIFICATION.md](../SPECIFICATION.md) §1.2.2).

## Limits (Re1)

| Limit | Value |
|-------|-------|
| Pattern length | **≤ 8192** octets (same max as `tN`) |
| Matches / replacements / split parts | **≤ 1024** per call |
| JIT | **off** |

## Functions

| Call | Returns | Notes / panic |
|------|---------|---------------|
| `re.test(pattern, subject)` | BOOL | `true` if any match; **`re.test.pattern`** / **`.args`** / **`.limit`** |
| `re.match(pattern, subject)` | LIST of STRING | First match **only if it starts at offset 0**; capturi; **`.nomatch`** if none / not at start |
| `re.find(pattern, subject)` | LIST of STRING | First match **anywhere**; **`.nomatch`** if none |
| `re.replace(pattern, subject, replacement)` | STRING | **All** matches (`$1`… PCRE2 substitution); no match → original subject |
| `re.split(pattern, subject)` | LIST of STRING | Split on matches; no match → one-element list with subject |

### Captures (O11)

`match` / `find` return a **LIST**:

| Index | Content |
|-------|---------|
| `0` | Full match |
| `1..n` | Numbered groups; unused optional group → `""` |

Named groups are **not** exposed in Re1.

```text
use native re
use native list

#main
  re.match('(\d+)-(\d+)', '12-34') -> m
  io.outln(list.get(m, 0))
  io.outln(list.get(m, 1))
  io.outln(list.get(m, 2))
  OK
```

### `re.test` / `re.find` / `re.replace` / `re.split`

```text
use native re
use native list

#main
  io.outln(re.test('abc', 'xxabc'))
  re.find('b+', 'aaabb') -> g
  io.outln(list.get(g, 0))
  io.outln(re.replace('a', 'banana', 'o'))
  re.split(',', 'a,b') -> parts
  io.outln(list.len(parts))
  OK
```

In FlatL string literals, write digit classes as `'\d+'` (a single backslash in source is kept as `\`).

## Related

- Build: `re` / `re_compile` (mirror of `dom` / `dom_compile`)
- Implementation: `src/lib/mod_re.c`, `src/lib/flat_re_pcre2.c`, `external/pcre2/`
