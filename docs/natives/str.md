# Native module `str`

**STRING** inspection, parsing, and construction. **Core** — always linked into `flvm` / `flatl`; call `str.*` directly. Optional alias:

```text
use native str with S
```

Then both `str.len('x')` and `S.len('x')` work.

Parsing helpers **`str.to_num`** / **`str.to_bool`** are also documented in [IO.md](../IO.md) §5 (same silent-fail rules on assign / `->`).

## Syntax

| Form | Example |
|------|---------|
| Arrow binding | `str.concat('a', 'b') -> s` |
| Assign | `s = str.slice(body, 0, 4)` |
| Literals | `'hello'` (INLINE ≤127 B) or long literals / pool-backed REF |

Native calls on the **right-hand side** of **`=`** use the same handlers as **`->`**.

### Argument parsing (commas inside strings)

Handlers split the call’s inner argument list on **top-level commas**. Commas **inside** single- or double-quoted STRING literals do **not** split arguments. Nesting **`[` / `]`** and **`(` / `)`** is respected for depth only (same as other natives).

Example — three parts, one call:

```text
str.concat('a', 'b', 'c') -> abc
```

Example — separator with comma in split:

```text
str.split('a,b,c', ',') -> parts
```

## STRING kinds (INLINE vs REF)

| Kind | When | `str.*` behavior |
|------|------|------------------|
| **INLINE** | Short literals, many `io` / `str` results | Full byte access |
| **REF** | Long literals, `__ref__(...)`, large `concat` / `repeat` / `slice` results | Pool-backed; same APIs |

Most mutating-style operations (**`concat`**, **`slice`**, **`trim`**, **`lower`**, …) **allocate a new STRING** (INLINE if it fits, otherwise REF). They do not modify the input in place.

## Error behavior

On **`… -> var`** or **`var = …`**, if a handler rejects arguments (wrong type, out-of-range slice, empty split separator, overflow past **`FLAT_POOL_MAX_CHUNK`**, etc.), the assign **fails silently**: **`var`** is unchanged. This matches **`str.to_num`** / **`io.in_num`** (see [IO.md](../IO.md)).

There is **no** PANIC path for `str.*` today.

## Function reference

### Conversions

| Call | Args | Returns | Description |
|------|------|---------|-------------|
| `to_num(s)` | 1 STRING | NUMBER | Parse decimal integer/float (`strtod` rules); invalid → silent fail |
| `to_bool(s)` | 1 STRING | BOOL | Trim; ASCII tokens per [IO.md](../IO.md) §6; invalid → silent fail |

```text
#main
  str.to_num('42') -> n
  str.to_bool('yes') -> ok
  io.outln(n)
  io.outln(ok)
  OK
```

### Length and repetition

| Call | Args | Returns | Description |
|------|------|---------|-------------|
| `len(s)` | 1 STRING | NUMBER | Byte length (UTF-8 bytes as stored; no Unicode grapheme logic) |
| `repeat(s, n)` | STRING, non-negative **whole** NUMBER | STRING | Repeat **`s`** **`n`** times; **`n == 0`** → `''`; product must fit **`FLAT_POOL_MAX_CHUNK`** |

```text
#main
  str.repeat('ab', 3) -> s
  io.outln(s)
  OK
```

### Concatenation and slicing

| Call | Args | Returns | Description |
|------|------|---------|-------------|
| `concat(s1, s2, …)` | 1–32 STRINGs | STRING | Variadic join; max **32** parts; total size ≤ **`FLAT_POOL_MAX_CHUNK`** |
| `slice(s, start, end)` | STRING, int, int | STRING | Substring **`[start, end)`** half-open; **`0 <= start <= end <= len(s)`** |

**`str.slice`** replaces the removed VM helper **`__slice__(s, start, len)`**. Migration:

| Old (removed) | New |
|---------------|-----|
| `__slice__(s, start, len)` | `str.slice(s, start, start + len)` |

Example — indices **`1..4`** on `'hello'` → `'ell'`:

```text
#main
  str.slice('hello', 1, 4) -> s
  io.outln(s)
  OK
```

Long body via **`__ref__`** (test/bootstrap only):

```text
body = __ref__('....long....')
head = str.slice(body, 0, 4)
```

### Search and predicates

| Call | Args | Returns | Description |
|------|------|---------|-------------|
| `index_of(haystack, needle)` | 2 STRINGs | NUMBER | First byte index of **`needle`** in **`haystack`**, or **−1**; empty **`needle`** → **0** |
| `contains(haystack, needle)` | 2 STRINGs | BOOL | **`index_of >= 0`**; empty **`needle`** → **true** |
| `starts_with(s, prefix)` | 2 STRINGs | BOOL | Prefix match (bytes) |
| `ends_with(s, suffix)` | 2 STRINGs | BOOL | Suffix match (bytes) |

```text
#main
  str.index_of('banana', 'ana') -> i
  str.contains('banana', 'xyz') -> no
  str.starts_with('banana', 'ban') -> yes
  str.ends_with('banana', 'na') -> yes2
  io.outln(i)
  io.outln(no)
  io.outln(yes)
  io.outln(yes2)
  OK
```

### Trim and case (ASCII)

| Call | Args | Returns | Description |
|------|------|---------|-------------|
| `trim(s)` | 1 STRING | STRING | Strip leading/trailing **`isspace`** bytes |
| `lower(s)` | 1 STRING | STRING | **`A`–`Z`** → **`a`–`z`** only |
| `upper(s)` | 1 STRING | STRING | **`a`–`z`** → **`A`–`Z`** only |

Non-ASCII code points are copied unchanged.

### Split and join

| Call | Args | Returns | Description |
|------|------|---------|-------------|
| `split(s, sep)` | 2 STRINGs | LIST | Split on every occurrence of **`sep`**; **`sep`** must be non-empty; max **1024** parts (**`FLAT_LIST_MAX_ELEMS`**) |
| `join(sep, list)` | STRING, LIST | STRING | Join LIST elements (each must be STRING) with **`sep`** between; empty list → `''` |

Requires optional **`list`** only if you bind the LIST for further work; **`split`** returns a LIST without a `use native list` line when you only pass it to **`join`** in one expression chain — but typical style declares **`list`**:

```text
use native list with L

#main
  str.split('a,b,c', ',') -> xs
  L.len(xs) -> n
  str.join('-', xs) -> s
  io.outln(n)
  io.outln(s)
  OK
```

Trailing empty segments: if **`s`** ends with **`sep`**, the final part after the last separator is included (may be empty STRING).

### Replace

| Call | Args | Returns | Description |
|------|------|---------|-------------|
| `replace(s, old, new)` | 3 STRINGs | STRING | Replace **first** occurrence of **`old`**; **`old`** must be non-empty |
| `replace_all(s, old, new)` | 3 STRINGs | STRING | Replace **all** non-overlapping occurrences |

```text
#main
  str.replace('foo bar foo', 'foo', 'baz') -> once
  str.replace_all('foo bar foo', 'foo', 'baz') -> all
  io.outln(once)
  io.outln(all)
  OK
```

## Limits (summary)

| Limit | Value | Applies to |
|-------|-------|------------|
| INLINE STRING | 127 bytes | Automatic choice in `emit` |
| `concat` parts | 32 | Argument count |
| `split` parts | 1024 | **`FLAT_LIST_MAX_ELEMS`** |
| Output size | **`FLAT_POOL_MAX_CHUNK`** (16 MiB) | **`concat`**, **`repeat`**, **`join`**, **`replace_*`** |
| Slice indices | **`0 … len`** | **`str.slice`** only non-negative |

See [LANGUAGE_TYPES.md](../LANGUAGE_TYPES.md) for pool and REF details.

## Related helpers

| Helper | Status |
|--------|--------|
| **`__ref__('…')`** | Still available for tests / long literals in pool |
| **`__slice__(…)`** | **Removed** — use **`str.slice`** |

## Source layout

| File | Role |
|------|------|
| `src/lib/mod_str.c` | All `str.*` handlers |
| `src/lib/native_dispatch.c` | `OP_NATIVE` routing |
| `src/lib/native_registry.c` | Name table |
| `include/flat_native.h` | IDs `FLAT_NATIVE_STR_*` |

## Tests

Under **`test/cases/str/`** (len, repeat, concat, slice, search, trim, split/join, case, replace). See also **`test/cases/values/001_long_ref`** for **`str.slice`** on REF.
