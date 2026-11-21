# JSON Query — Find, Check and Count

The native `json` module includes a query engine for evaluating conditions against JSON objects and arrays.

The query syntax is shared with the Adv.Filter language in FLEditor's JSONL Editor. It supports JSON paths, array wildcards, comparisons, string/list predicates and logical expressions.

## Native API

### `json.check(obj, expression)`

Returns `BOOL`. Evaluates the expression against the source value and returns `true` when at least one resolved value satisfies it (**ANY** semantics).

```flatL
data = json.parse_object('{"domainId":5,"name":"Example"}')

valid = json.check(
    data,
    "$.domainId is between 2 to 10 and $.name len > 4"
)
```

A missing path does not match. `is not null` matches when at least one resolved value is non-null; it does not make a missing path match.

### `json.find(obj, expression, returnPath)`

Returns a `LIST` containing values selected by `returnPath` for matching contexts.

```flatL
products = json.parse_list('[{"id":1,"price":20},{"id":2,"price":50},{"id":3,"price":80}]')

found = json.find(
    products,
    "$[*].price between 40 to 60",
    "$[@]"
)
```

Result:

```json
[{"id":2,"price":50}]
```

The third argument determines what is returned:

| `returnPath` | Meaning |
|---|---|
| `$` | Return the complete source value once if the expression matches |
| `$[@]` | Return the current matching array element |
| `$[@].id` | Return the `id` property from the matching element |
| `$.products[@]` | Return the matching product object |
| `$.products[@].variants[@]` | Return the matching nested variant |
| `$.products[@].variants[@].color` | Return the color of the matching nested variant |

The special `@` segment is used only in `json.find()`'s `returnPath`. It identifies the current element at the corresponding wildcard level in the query. Multiple `@` segments preserve nested wildcard context.

For example, with a source containing products and nested variants:

```flatL
variant = json.find(
    data,
    "$.products[*].variants[*].price between 40 to 60",
    "$.products[@].variants[@]"
)
```

Only variants satisfying the condition are returned. If the return path selects the parent product instead, the product is returned when at least one of its variants matches:

```flatL
products = json.find(
    data,
    "$.products[*].variants[*].price between 40 to 60",
    "$.products[@]"
)
```

### `json.count(obj, expression)`

Returns a `NUMBER` representing the number of matching contexts.

```flatL
total = json.count(
    products,
    "$[*].price between 40 to 60"
)
```

For wildcard queries, the count is based on the deepest wildcard path used by the expression. Each matching element at that level contributes one to the result.

## Query syntax

### JSON paths

`$` represents the source root.

```text
$
$.name
$.user.address.city
$.items[0].name
$.items[2].location
$.items[*].location
$."property with spaces"
```

Supported path segments:

- Object property names.
- Quoted property names.
- Zero-based array indexes.
- `[*]` to traverse every element of an array.
- `[]` to assert/resolve a list value for a `len` predicate.

### Comparisons

```text
=
!=
>
<
>=
<=
```

Examples:

```text
$.id = 10
$.price >= 20
$.status != "deleted"
```

Equality compares JSON values without implicit conversion. Relational comparisons are numeric.

### Inclusive ranges: `between`

`between` uses `to` as its range separator. Both endpoints are included, matching SQL's `BETWEEN` semantics.

```text
$.price between 10 to 50
$.temperature between -5.5 to 25.75
$.percentage between 0 to 100
```

This is equivalent to:

```text
$.price >= 10 and $.price <= 50
```

A non-numeric value does not match a numeric range.

### Logical operators

```text
and
or
not
```

Parentheses can group expressions.

```text
$.active is true and $.price > 10
```

```text
$.status = "active" or $.status = "pending"
```

```text
not ($.deleted is true)
```

### Existence, null and boolean values

A path by itself tests whether it resolves:

```text
$.user.name
```

Explicit checks:

```text
$.value is null
$.value is not null
$.enabled is true
$.enabled is false
```

A missing property is distinct from a property whose value is `null`. A missing path evaluates to `false`, including for `is not null`.

### Membership and list contents

```text
$.status in ["active", "pending"]
$.status not in ["deleted", "archived"]
$.tags has "blue"
$.tags has all ["red", "blue"]
```

`in` checks whether a resolved value is one of the supplied values.

`has` checks whether a list contains at least one supplied value. `has all` requires every supplied value to be present.

### String predicates

```text
$.message contains "error"
$.name starts with "Jo"
$.filename ends with ".json"
```

Negated forms are also supported:

```text
$.message not contains "error"
```

### Type checks

```text
$.value type object
$.value type number
$.value type string
$.value type bool
$.value type list
$.value type null
$.value not type null
```

Aliases `boolean` for `bool` and `array` for `list` are accepted.

### Length

`len` checks the length of a string or a list.

```text
$.name len > 4
$.items[] len >= 3
```

## Wildcard semantics: ANY

`[*]` resolves every element at that array level. Predicates use **ANY** semantics: the predicate is true when at least one resolved value matches.

For example:

```text
$.products[*].price between 40 to 60
```

is true if at least one product has a price in the inclusive interval.

This is the same matching behavior used by `json.check`, `json.find` and `json.count`.

`not` applies to the complete predicate result. For example:

```text
not ($.items[*].value is null)
```

means no resolved item has a null value. This differs from:

```text
$.items[*].value is not null
```

which is true when at least one resolved item is non-null.

## Implementation notes

The native implementation uses a character-based lexer, recursive-descent parser and AST evaluator. It does not use regular expressions or dynamic code execution.

The query is parsed once per native call. Path evaluation traverses runtime LIST and OBJECT values directly. `json.check` stops after a match; `json.find` collects requested values; `json.count` counts matching contexts without constructing a result list.

## Related

- [Native json](natives/json.md)
- [LIST / OBJECT guide](LIST_OBJECT.md)
- FLEditor JSONL Editor Adv.Filter: `docs/fleditor_site/js/fleditor-jsonl-adv-filter.js`
- Tests: `test/cases/json/`
