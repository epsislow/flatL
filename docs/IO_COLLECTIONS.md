# flatL — JSON & collection I/O (planned)

**Status:** **implemented** in VM (v0.4) — `src/common/value_json.c`, tests in `test/cases/json/`.  
Builds on [IO.md](./IO.md) (streams, prompts, `->`, PANIC on `io.in*`) and [LANGUAGE_TYPES.md](./LANGUAGE_TYPES.md) (LIST/OBJECT).

When implemented, natives live under **`json.*`** and **`io.in_list` / `io.in_object` / `io.in_bools`**.

---

## 1. Layering

| API | stdin | On invalid parse |
|-----|--------|------------------|
| `json.stringify(v)` | — | — |
| `json.parse_list(s)` | — | assign / `->` **fails silently** (like `str.to_num`) |
| `json.parse_object(s)` | — | same |
| `json.parse_bools(s)` | — | same |
| `json.parse(s)` | — | any JSON value (object, array, string, number, bool); same silent fail |
| `io.in_list(prompt?)` | one line | **PANIC** `io.parse` (like `io.in_num`) |
| `io.in_object(prompt?)` | one line | **PANIC** `io.parse` |
| `io.in_bools(prompt?)` | one line | **PANIC** `io.parse` |

`io.in_*` ≈ read line (with optional prompt on stdout) then call the matching `json.parse_*`.

Binding: `io.in_list('…') -> xs`, `xs = io.in_list('…')`.

**Empty line (Enter only):** after trim, an empty line is **not** cancel for collection reads:

| API / parser | Empty line → |
|--------------|----------------|
| `io.in_list`, `json.parse_list` | empty LIST `[]` |
| `io.in_object`, `json.parse_object` | empty OBJECT `{}` |

This differs from `io.inln` / `io.in_num` / `io.in_bool`, where an empty line is cancel (PANIC).

**EOF** (e.g. Ctrl+D with no newline): still **PANIC** / cancel like other `io.in*`.

**`io.in_bools`:** empty line → empty LIST `[]` (zero bools), same spirit as `io.in_list`.

**Whitespace-only line** (spaces/tabs, then Enter): after trim → same as empty line (`[]`, `{}`, or bool `[]`).

### 1.1 `json.parse` vs typed parsers (empty string)

These are **different** APIs; do not conflate them:

| Input (after trim) | `json.parse(s)` | `json.parse_list(s)` | `json.parse_object(s)` |
|--------------------|-----------------|----------------------|-------------------------|
| `""` | **NULL** (see §1.2) | empty LIST `[]` | empty OBJECT `{}` |
| `"[1,2]"` | LIST value | (use `parse_list` or `parse`) | — |
| `"{"` | silent fail | silent fail | silent fail |

**Stdin:** `io.in_*` reads a line first; **Enter** → trimmed `""` → row above (`[]` / `{}`, not NULL).  
Only the **`json.parse("")`** call returns NULL, not “empty object”.

### 1.2 JSON `null` and `json.parse`

- **`null` in JSON text** (e.g. `{"x":null}`, `[null]`) decodes to a dedicated **`VAL_NULL`** runtime tag (add with CJ7 — distinct from **`VAL_EMPTY`**, which is internal/unassigned, not user JSON).
- **`json.stringify`** on NULL → `null`.
- **`json.parse("")`** (empty string argument) → **NULL** (`VAL_NULL`), not parse error.
- **Source literal `null`:** reserved keyword like **`true`** / **`false`** in expressions (`io.outln(null)`, `x = null`). The parser must **not** treat `null` as a variable name lookup first — otherwise `io.outln(null)` would mean “print variable `null`”.
- **`io.outln(null)`** (and `io.out` / `io.outvar` on a NULL value): print the JSON token **`null`** + newline for `outln` (same as `json.stringify` for that value).
- Variable named `null` is **disallowed** once the keyword exists (or shadowing forbidden at compile time).
- **Guards:** **`x == null`** and **`null == x`** are supported (NULL equals NULL; NULL not equal to any other type). **`x == true` / `false`** pattern unchanged for BOOL.

**Why not “null = empty object”?** In JSON, `null` ≠ `{}`. Objects and lists can **contain** null values: `{"a": null}`.

Typed parsers **reject** wrong top-level kind:

| Call | Top-level must be | Example invalid |
|------|-------------------|-----------------|
| `json.parse_list(s)` | array (simple line or `[…]`) | `{` , `"[]"` alone as object N/A |
| `json.parse_object(s)` | JSON object `{…}` | `"[1,2,3]"` , `"[]"` |

**`io.in_object`** with line `[1,2,3]` → **PANIC** `io.parse` (not coerced to LIST).

Malformed JSON (`[`, `{`, `[true,,false]`, trailing garbage) → **`json.parse*`** silent fail; **`io.in_*`** → **PANIC** `io.parse`.

---

## 2. Objects — one line of JSON

`io.in_object('json here:') -> source`

User types a **single JSON object** after trim, e.g.:

```text
{"names": ["Mark", "Joe", "Carmen", "Elizabeth"]}
```

- Keys and string values use **JSON** syntax (double-quoted strings).
- Not flatL literal syntax (`{ id: 'a' }`) on stdin.
- Nested arrays/objects follow JSON.

`json.parse_object(s)` accepts the same grammar (must decode to OBJECT).

**Empty input:** `json.parse_object("")` or whitespace-only → `{}` (same as `io.in_object` on Enter).  
This is **not** `json.parse("")`, which yields **NULL** (§1.1).

---

## 3. Lists — two input modes

After trim, one line:

### 3.1 Starts with `[` → JSON array

Full **JSON array** parse: mixed types, quoted strings with JSON escapes, nesting.

Examples:

```text
[1, 2, "TV"]
["ab\n\r\"  sadasda ", 432, "\\,43"]
```

String escapes follow **JSON** (`\n`, `\r`, `\"`, `\\`, `\uXXXX`, etc.).  
A value like `"\,43"` is the string `\,43` (comma is not a separator inside JSON strings).

### 3.2 Does not start with `[` → simple comma list

Split on **unescaped** commas (see §4). Trim spaces around each token.

**Homogeneous typing** (only **non-empty** tokens decide the mode; see §3.3 for empty slots):

1. If **every non-empty** token parses as a **NUMBER** (same rules as `str.to_num` / `io.in_num` on the token alone) → `LIST` of **NUMBER**.
2. If **any non-empty** token is not a number → **all non-empty** tokens become **STRING** elements (literal text after trim and unescape); empty slots become `""`.

Examples:

| Input | Result |
|--------|--------|
| `1,3,5,7,8,10` | NUMBER list |
| `1.3 , -23, -6, -3.0321` | NUMBER list |
| `42` | one NUMBER |
| `hello` | one STRING `"hello"` |
| `Mark, Joe, -3423, 1.05` | all STRING (`"Mark"`, `"Joe"`, `"-3423"`, `"1.05"`) — mixed content forces strings; numeric-looking pieces are **not** numbers |
| `TV, Phone, HC2000, PS4` | all STRING |
| `1, 2, TV` | all STRING `"1"`, `"2"`, `"TV"` |
| `1,,2` | NUMBER list `[1, 0, 2]` (empty slot → `0` in number lists) |

**Document clearly:** in simple mode, `-3423` next to `Mark` is the string `"-3423"`, not a number. For **BOOL** lists use **`io.in_bools`** or a JSON array `[true, false]`.

### 3.3 Split rule: `n` commas → `n + 1` elements

Simple-mode lists use the usual **comma split** (respecting §4 escapes):

- If the line contains **`n`** unescaped comma separators, there are **`n + 1`** element slots.
- Same rule as typical `split(",")` in other languages: `,` → 2 slots, `,,` → 3, no comma → 1 slot.
- One value **without** any comma is a **single-element** list, e.g. `32` → `[32]`, `hello` → `["hello"]`.

A slot with no characters after trim is an **empty token** (see below).

| List mode | Empty slot becomes |
|-----------|-------------------|
| **NUMBER** (every **non-empty** token parses as a number) | **NUMBER `0`** |
| **STRING** (any non-empty token is not a number) | **STRING `""`** |

If there are **no non-empty** tokens (e.g. `,,` or a line that is only commas), mode is **NUMBER** and every slot is **`0`**.

Examples (simple number mode):

| Input | Commas `n` | Slots `n+1` | Result |
|--------|------------|-------------|--------|
| `32` | 0 | 1 | `[32]` |
| `1,,2` | 2 | 3 | `[1, 0, 2]` |
| `1,2,` | 2 | 3 | `[1, 2, 0]` |
| `1,2,,` | 3 | 4 | `[1, 2, 0, 0]` |
| `,1` | 1 | 2 | `[0, 1]` |
| `,,1` | 2 | 3 | `[0, 0, 1]` |
| `,` | 1 | 2 | `[0, 0]` |
| `,,` | 2 | 3 | `[0, 0, 0]` |
| `,,3` | 2 | 3 | `[0, 0, 3]` |

String mode (same empty-slot rules):

| Input | Result |
|--------|--------|
| `a,,b` | `["a", "", "b"]` |
| `1,,TV` | `["1", "", "TV"]` |
| `hello,` | `["hello", ""]` |

Empty slots **do not** force string mode; only **non-empty** non-numeric tokens do.

### 3.4 Enter vs commas (zero elements vs `n+1` slots)

| Input after trim | Meaning |
|------------------|---------|
| **Empty line** (Enter only, or whitespace-only) | **Zero elements:** `[]` for list / `{}` for object — **no comma split** |
| **Any line with at least one comma** | Apply §3.3: **`n` commas → `n + 1` slots** (empty slots → `0` / `""` / `false` by mode) |

So **Enter** is **`[]`**, not `[0]`. A lone **`,`** is **two** slots → number list **`[0, 0]`** (not one element).

**`io.in_bools`** simple mode: same **`n + 1`** rule; empty slot → **`false`** (e.g. `,` → `[false, false]`).

---

## 4. Escapes in simple comma lists

JSON mode uses JSON rules only.

In **simple** mode, tokens support **backslash escapes** before splitting (or when reading each token):

| Escape | Meaning |
|--------|---------|
| `\\` | `\` |
| `\,` | literal `,` (not a separator) |
| `\"` | `"` |
| `\n`, `\r`, `\t` | common whitespace (optional set, align with JSON subset) |

Unescaped commas separate tokens. This allows labels that contain commas without switching to JSON mode.

Implementation must scan left-to-right so `a\,b,c` → two tokens `a,b` and `c`.

---

## 5. Booleans — `io.in_bools` (not simple-list bools)

Simple lists are **only NUMBER-homogeneous or all-STRING**. There is no `true`/`false` token typing in simple mode (`true` is the string `"true"`).

For BOOL lists, use:

**`io.in_bools(prompt?) -> xs`**

- **Simple mode** (no leading `[`): comma split + escapes like §3–§4.
- **JSON mode** (starts with `[`): strict JSON array of bools only — **`[true,,false]`** is invalid JSON → **PANIC** on `io.in_*`; silent fail on `json.parse_bools`.

**Tokens (simple mode):**

- Each **non-empty** token must be a valid **`io.in_bool`** token ([IO.md §6](./IO.md#6-bool-tokens-no-truthy)).
- **Empty slot** (`,,`, trailing comma, leading comma): **`false`**, same idea as **`0`** in number lists.
- **Cancel tokens** on any **single token** (`q`, `cancel`, `c`, `quit`, … — same set as `io.in_bool`) → **PANIC** `io.user_cancel`, not “false in the list”.
- Invalid bool token → **PANIC** `io.parse` on `io.in_*`; silent fail on public **`json.parse_bools(s)`** (same API family as `parse_list` / `parse_object`).

Examples (simple mode):

| Input | Result |
|--------|--------|
| `yes,no,1,0` | LIST of BOOL (per token rules) |
| `yes,,no` | `[true, false, false]` |
| `true,` | `[true, false]` |

---

## 6. Output

### 6.1 `json.stringify(v)`

Canonical **JSON** for any value (nested LIST/OBJECT, mixed arrays, etc.). Used for logs, copy-paste interchange, and **`io.out` / `io.outln` on OBJECT**.

**OBJECT key order:** keys are emitted in **sorted (lexicographic UTF-8 byte order)** order, stable for diffs and tests (not insertion order).

### 6.2 `io.out` / `io.outln` / `io.outvar`

| Value type | `io.out` / `io.outln` / `io.outvar` format |
|------------|-----------------------------------------------|
| STRING, NUMBER, BOOL | Unchanged from [IO.md §2](./IO.md#2-output-ioout) (STRING raw, etc.) |
| **OBJECT** | **`json.stringify(obj)`** (one JSON object; no extra newline beyond `outln`) |
| **LIST** | **Simple line** if homogeneous (§6.3); otherwise **`json.stringify(list)`** |
| **LIST** (empty) | **Empty string** for `io.out`; `io.outln` / `io.outvar` → blank line (only `\n`) |

Programmers who want JSON for a list call **`json.stringify(list)`** explicitly (or print an OBJECT).

### 6.3 Simple LIST line (stdout)

Matches the **simple comma-list** read grammar (§3.2) so round-trip is natural: output of a homogeneous list should be readable by `io.in_list` on the next line.

**NUMBER list** (every element is NUMBER):

- Join with **`,`** (no spaces required; implementation may use no space after comma).
- Every element is printed, **including `0`** — e.g. `[1, 2, 3, 0, 0]` → `1,2,3,0,0`.
- Use the same numeric formatting as NUMBER elsewhere (e.g. `%g`-style); **`0` must appear as `0`**, not omitted.

**STRING list** (every element is STRING):

- Join with **`,`** between elements.
- Each element wrapped in **double quotes**, with **JSON string escaping** inside quotes (`\"`, `\\`, `\n`, `\r`, `\t`, `\uXXXX` as needed).
- **Empty strings** appear as **`""`** — e.g. `["Mark", "Carmen", ""]` → `"Mark","Carmen",""` (spacing after commas is optional; escapes must be correct).

Examples:

```text
io.outln([1, 0, 2, 0, 0])     → 1,0,2,0,0\n
io.outln(['Mark', 'Carmen', '']) → "Mark","Carmen",""\n
io.outln({ a: 1 })            → {"a":1}\n   (JSON object)
```

**Not simple line:** LIST with mixed types (NUMBER + STRING), nested LIST/OBJECT elements, or BOOL elements → **`json.stringify(list)`** on `io.out` / `io.outln`.

**BOOL list:** not simple-line in v1; use **`json.stringify`** on output (or future dedicated format). Input remains **`io.in_bools`**.

---

## 7. Toolchain integration (CJ7 / D12)

**Recommendation:**

1. Implement **`json.parse` / `parse_list` / `parse_object` / `parse_bools` / `stringify`** in C (`src/common/` or `src/lib/json.c`) as pure functions on `FlatValue`.
2. Wire **`io.in_list` / `io.in_object` / `io.in_bools`** to existing stdin + line read, then call typed parsers (same as today’s `io.in_num` → parse).
3. **Phase 1:** register **`json.*`** (and new **`io.in_*`**) via **`OP_IO`** / compile stubs if D12 registry is not ready.
4. **Phase 2 (D12):** move to native module table; drop duplicated opcode strings.

One parser implementation; no second grammar in the VM.

---

## 8. Size and depth limits

**Product rule:** no small artificial cap (no “max 100 elements” in the language spec).

**Implementation reality:**

- Values still live in the **existing pool**; allocation failure → PANIC or parse fail (host-defined).
- **Nesting depth:** hard limit **128** levels (arrays/objects nested inside each other). Deeper JSON → parse fail / `io.in_*` PANIC `io.parse`. Documented in TOOLCHAIN; not a “max element count”, only depth.

Favor **no user-facing element cap**; fail when the host cannot allocate or depth exceeds 128.

---

## 9. Tests (when implemented)

- JSON object line; nested arrays
- Simple number / simple all-string / single token
- Mixed `1, 2, TV` → strings
- `Mark, Joe, -3423, 1.05` → all strings
- Empty line → `[]` / `{}` / bool `[]`; empty slots: `1,2,` → `[1,2,0]`; `,,` → `[0,0,0]`; `a,,b` → strings
- JSON escapes in `[...]` including `"\,43"`
- Simple mode `\,` comma in token
- `io.in_bools`: `yes,no,1,0` mapping; invalid token
- Round-trip: `stringify` → `parse_object`; homogeneous list `outln` → `in_list`
- LIST out: `1,0,2,0,0` and `"Mark","Carmen",""` with escapes in strings
- OBJECT out: JSON via `stringify`
- PANIC vs silent fail (`io.in_*` vs `json.parse_*`)
- `json.parse("")` → NULL; `parse_object("")` → `{}`; `parse_list("")` → `[]`
- `json.parse_object("[]")` invalid; `io.in_object` with `[1,2,3]` PANIC
- `io.in_bools`: empty slot → false; token `q` → cancel PANIC; `[true,,false]` invalid
- JSON `null` in nested values; `stringify` / round-trip

---

## 10. Related plans

- [.cursor/plans/collections-json.plan.md](../.cursor/plans/collections-json.plan.md) — `json.*` natives (CJ7)
- [.cursor/plans/debug-io.plan.md](../.cursor/plans/debug-io.plan.md) — I/O baseline (v0.3)
