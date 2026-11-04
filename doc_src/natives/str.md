# Native module `str`

**STRING** inspection, parsing, and construction. **Core** — always linked into `flvm` / `flatl`; call `str.*` directly. Optional alias:

```text
use native str with S
```

Then both `str.len('x')` and `S.len('x')` work.

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

## Line modifiers and errors

See [SPECIFICATION.md](../SPECIFICATION.md) §1.2.2. On **recoverable** failure (wrong type, invalid parse, out-of-range slice, empty split separator, size limit, …):

| Modifier | Effect |
|----------|--------|
| *(none)* | **PANIC** — reason **`str.<fn>`** (e.g. `str.to_num`) |
| **`?`** | No bind; next line runs |
| **`~`** | No bind; **`#or.*`** for that line |

Use **`?`** when you want the old silent-fail behavior (binding unchanged).

## Function reference

### Conversions (`str.to_*`)

No stdin.

| Call | Args | Returns | Panic (no modifier) |
|------|------|---------|---------------------|
| `to_num(s)` | 1 STRING | NUMBER | **`str.to_num`** — invalid number text |
| `to_bool(s)` | 1 STRING | BOOL | **`str.to_bool`** — token not in BOOL table |

#### BOOL tokens (for `to_bool` / `io.in_bool`)

Single token after trim, ASCII case-insensitive:

| true | false |
|------|-------|
| `true`, `t`, `yes`, `y`, `on`, `1` | `false`, `f`, `no`, `n`, `off`, `0` |

```text
#main
  str.to_num('42') -> n
  str.to_bool('yes') -> ok
  io.outln(n)
  io.outln(ok)
  OK
```

### Length and repetition

| Call | Args | Returns | Panic (no modifier) |
|------|------|---------|---------------------|
| `len(s)` | 1 STRING | NUMBER | **`str.len`** — arg not STRING |
| `repeat(s, n)` | STRING, non-negative **whole** NUMBER | STRING | **`str.repeat`** — bad types, negative/non-integer **`n`**, or product &gt; **`FLAT_POOL_MAX_CHUNK`** |

```text
#main
  str.repeat('ab', 3) -> s
  io.outln(s)
  OK
```

### Concatenation and slicing

| Call | Args | Returns | Panic (no modifier) |
|------|------|---------|---------------------|
| `concat(s1, s2, …)` | 1–32 STRINGs | STRING | **`str.concat`** — wrong types, &gt;32 parts, or total &gt; **`FLAT_POOL_MAX_CHUNK`** |
| `slice(s, start, end)` | STRING, int, int | STRING | **`str.slice`** — bad types or indices outside **`0 … len(s)`** |

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

| Call | Args | Returns | Panic (no modifier) |
|------|------|---------|---------------------|
| `index_of(haystack, needle)` | 2 STRINGs | NUMBER | **`str.index_of`** — arg not STRING (success returns index or **−1**) |
| `contains(haystack, needle)` | 2 STRINGs | BOOL | **`str.contains`** — arg not STRING |
| `starts_with(s, prefix)` | 2 STRINGs | BOOL | **`str.starts_with`** — arg not STRING |
| `ends_with(s, suffix)` | 2 STRINGs | BOOL | **`str.ends_with`** — arg not STRING |

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

| Call | Args | Returns | Panic (no modifier) |
|------|------|---------|---------------------|
| `trim(s)` | 1 STRING | STRING | **`str.trim`** — arg not STRING |
| `lower(s)` | 1 STRING | STRING | **`str.lower`** — arg not STRING |
| `upper(s)` | 1 STRING | STRING | **`str.upper`** — arg not STRING |

Non-ASCII code points are copied unchanged.

### Split and join

| Call | Args | Returns | Panic (no modifier) |
|------|------|---------|---------------------|
| `split(s, sep)` | 2 STRINGs | LIST | **`str.split`** — bad types, empty **`sep`**, or &gt;1024 parts |
| `join(sep, list)` | STRING, LIST | STRING | **`str.join`** — bad types, non-STRING element, or output too large |

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

| Call | Args | Returns | Panic (no modifier) |
|------|------|---------|---------------------|
| `replace(s, old, new)` | 3 STRINGs | STRING | **`str.replace`** — bad types, empty **`old`**, or output too large |
| `replace_all(s, old, new)` | 3 STRINGs | STRING | **`str.replace_all`** — same as **`replace`** |

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
