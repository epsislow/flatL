# Native module `json`

**JSON text ↔ runtime values** (LIST, OBJECT, STRING, NUMBER, BOOL, **NULL**). **Core** — always linked into `flvm` / `flatl`; call `json.*` directly (no `use native json`).

Normative grammar, empty-input rules, and **`io.in_*`** pairing: [IO_COLLECTIONS.md](../IO_COLLECTIONS.md).  
Guide and examples: [LIST_OBJECT.md](../LIST_OBJECT.md).

Implementation: `src/lib/mod_json.c`, `src/common/value_json.c`.

## Calls

| Call | Input | Result | Invalid / wrong kind |
|------|--------|--------|----------------------|
| `json.parse(s)` | JSON text (any top-level value) | Value or **NULL** for `""` | Silent fail (binding unchanged) |
| `json.parse_list(s)` | Array or **simple line** | LIST; `""` → `[]` | Silent fail |
| `json.parse_object(s)` | JSON object `{…}` | OBJECT; `""` → `{}` | Silent fail |
| `json.parse_bools(s)` | JSON bool array or tokens | LIST of BOOL; `""` → `[]` | Silent fail |
| `json.stringify(v)` | Any value | JSON STRING (OBJECT keys sorted) | — |

**`json.parse("")`** → **NULL** (`null` keyword in programs). This differs from **`json.parse_object("")`** → `{}` and **`json.parse_list("")`** → `[]`.

**`io.in_list` / `io.in_object` / `io.in_bools`** read one stdin line (optional prompt) then apply the matching parser; invalid input → **PANIC** `io.parse` (not silent). See [IO.md](../IO.md) and [io.md](./io.md).

## Examples

```text
o = json.parse_object('{"ok":true}')
xs = json.parse_list('Mark, Joe')
s = json.stringify(o)
io.outln(json.parse('null'))
```

## Related

- [io.md](./io.md) — `io.in_list`, `io.in_object`, `io.in_bools`  
- [obj.md](./obj.md) / [list.md](./list.md) — optional in-memory APIs  
- Tests: `test/cases/json/` (`bin/run_tests json`, `make json_generate_tests`)
