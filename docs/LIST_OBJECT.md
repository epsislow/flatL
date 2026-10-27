# flatL — LIST, OBJECT, and JSON I/O

User guide (normative detail: [IO_COLLECTIONS.md](./IO_COLLECTIONS.md)).  
Types overview: [LANGUAGE_TYPES.md](./LANGUAGE_TYPES.md).

---

## 1. Values in memory

| Type | Example in source | Notes |
|------|-------------------|--------|
| LIST | `xs = [1, 'a']` | Pool-backed; `foreach(xs, item, '#loop')` |
| OBJECT | `o = { id: 1, name: 'x' }` | Keys in source: identifiers or quoted strings |
| NULL | `x = null`, `json.parse('')` | Keyword **`null`**; not a variable name |

Helpers (until native `list.*` / `obj.*`): `__list__`, `__list_push__`, `__obj_set__`, `__obj_get__`.

---

## 2. JSON module (`json.*`)

| Call | Role |
|------|------|
| `json.parse(s)` | Any JSON value; **`json.parse('')` → null** |
| `json.parse_list(s)` | List; **`''` → `[]`** |
| `json.parse_object(s)` | Object; **`''` → `{}`** |
| `json.parse_bools(s)` | List of BOOL; **`''` → `[]`** |
| `json.stringify(v)` | JSON text (object keys **sorted**) |

Invalid parse on assign / `->`: **silent fail** (like `str.to_num`).

---

## 3. Reading from stdin

| Call | Empty line (Enter) | Bad line |
|------|--------------------|----------|
| `io.in_list(prompt?)` | `[]` | PANIC `io.parse` |
| `io.in_object(prompt?)` | `{}` | PANIC |
| `io.in_bools(prompt?)` | `[]` | PANIC (token `q`/`cancel` → user cancel) |

**Simple list** (line does not start with `[`):

- **`n` commas → `n + 1` elements** (same as `split(",")`).
- All non-empty tokens numbers → NUMBER list; empty slots → **`0`**.
- Any non-number token → all STRING; empty slots → **`""`**.
- Example: `1,2,` → `[1, 2, 0]`; Enter alone → `[]` (not `[0]`).

**JSON line** (starts with `[` or `{`): strict JSON; max nesting depth **128**.

---

## 4. Writing to stdout

| Value | `io.out` / `io.outln` |
|-------|------------------------|
| OBJECT | `json.stringify` |
| LIST (all numbers) | `1,2,0` |
| LIST (all strings) | `"a","b",""` (JSON escapes inside quotes) |
| LIST (mixed / nested) | `json.stringify` |
| NULL | `null` |

Use **`json.stringify(x)`** when you always want JSON.

---

## 5. Examples

```text
#main
  io.in_list('Nums:') -> nums
  io.outln(nums)
  o = json.parse_object('{"ok":true}')
  io.outln(o)
  OK
```

```text
#main
  xs = json.parse_list('Mark, Joe')
  io.outln(xs)
  OK
```

---

## 6. Tests

```bash
bin/run_tests json -v
make test
```

Suite: `test/cases/json/` (**40** cases; regenerate: `make json_generate_tests`).
