# Native module `json`

**JSON text ↔ runtime values** (LIST, OBJECT, STRING, NUMBER, BOOL, **NULL**). **Core** — always linked into `flvm` / `flatl`; call `json.*` directly (no `use native json`).

Normative grammar, empty-input rules, and **`io.in_*`** pairing: [IO_COLLECTIONS.md](../IO_COLLECTIONS.md).  
Guide and examples: [LIST_OBJECT.md](../LIST_OBJECT.md).  
Line modifiers: [SPECIFICATION.md](../SPECIFICATION.md) §1.2.2.

Implementation: `src/lib/mod_json.c`, `src/common/value_json.c`.

## Calls

| Call | Input | Result | Panic (no modifier) |
|------|--------|--------|---------------------|
| `json.parse(s)` | JSON text (any top-level value) | Value; `""` → **NULL** | **`json.parse`** — invalid JSON (non-empty) |
| `json.parse_list(s)` | Array or **simple line** | LIST; `""` → `[]` | **`json.parse_list`** — invalid text |
| `json.parse_object(s)` | JSON object `{…}` | OBJECT; `""` → `{}` | **`json.parse_object`** — invalid or wrong shape |
| `json.parse_bools(s)` | JSON bool array or tokens | LIST of BOOL; `""` → `[]` | **`json.parse_bools`** — invalid tokens |
| `json.stringify(v)` | Any value | JSON STRING (OBJECT keys sorted) | **`json.stringify`** — value not serializable or buffer limit |

Use **`?`** on a line to keep the previous binding when parse fails (former silent behavior).

**Datetimes:** JSON string values (e.g. `"2026-09-30T12:00:00Z"`) are ordinary **STRING**s after **`json.parse_object`** — no automatic date typing. Normalise with **`str.to_datetime`**, **`str.to_date`**, or **`date.parse_auto`** ([date.md](./date.md)).

**`json.parse("")`** → **NULL** (`null` keyword in programs). This differs from **`json.parse_object("")`** → `{}` and **`json.parse_list("")`** → `[]`.

**`io.in_list` / `io.in_object` / `io.in_bools`** read one stdin line then apply the matching parser; invalid input → **PANIC** `io.parse` (or **`io.in_list`**, etc. as native reason). Use **`?`** / **`~`** on those lines if needed. See [IO.md](../IO.md) and [io.md](./io.md).

## Examples

```text
o = json.parse_object('{"ok":true}')
xs = json.parse_list('Mark, Joe')
? n = json.parse_list('[')
s = json.stringify(o)
io.outln(json.parse('null'))
```

```text
#main
  ~ json.parse_object('{') -> o   #.bad
  OK
#or.#main.bad
  io.outln('bad json')
  OK
```

## Related

- [io.md](./io.md) — `io.in_list`, `io.in_object`, `io.in_bools`  
- [obj.md](./obj.md) / [list.md](./list.md) — optional in-memory APIs  
- Tests: `test/cases/json/`, `test/cases/modifier/`
