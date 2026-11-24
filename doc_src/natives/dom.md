# Native module `dom`

**HTML5 parser, DOM access, and CSS selectors** backed by [Lexbor](https://github.com/lexbor/lexbor). **Optional** — declare before use:

```flat
use native dom
use native list
use native obj
```

DOM is disabled by default. It is enabled at build time; see [Build](#build).

## API overview

| Call | Returns | Panic / failure |
|------|---------|-----------------|
| `parseHtml(html)` | NUMBER document handle | `dom.parseHtml` |
| `select(page, cssSelector)` | LIST of NUMBER node handles | `dom.select` |
| `text(node)` | STRING | `dom.text` |
| `attr(node, name)` | STRING (missing attribute → `""`) | `dom.attr` |
| `formFields(form)` | OBJECT | `dom.formFields` |
| `close(page)` | BOOL (`true`) | `dom.close` |
| `tag(node)` | STRING tag name | `dom.tag` |
| `hasAttr(node, name)` | BOOL | `dom.hasAttr` |
| `classes(node)` | LIST of STRING | `dom.classes` |
| `hasClass(node, className)` | BOOL | `dom.hasClass` |
| `parent(node)` | NUMBER node handle or NULL | `dom.parent` |
| `children(node)` | LIST of NUMBER node handles | `dom.children` |
| `next(node)` / `prev(node)` | NUMBER node handle or NULL | `dom.next` / `dom.prev` |
| `closest(node, cssSelector)` | NUMBER node handle or NULL | `dom.closest` |
| `value(control)` | STRING control value | `dom.value` |
| `data(node, name)` | STRING `data-*` value (missing → `""`) | `dom.data` |
| `inputType(input)` | STRING input type | `dom.inputType` |
| `checked(input)` | BOOL | `dom.checked` |
| `disabled(control)` | BOOL effective disabled state | `dom.disabled` |
| `required(control)` | BOOL | `dom.required` |
| `options(select)` | LIST of option node handles | `dom.options` |
| `selectedOptions(select)` | LIST of selected option node handles | `dom.selectedOptions` |

Document and node handles are numeric resources. A document owns every node handle obtained from it; `close(page)` invalidates all of them.

## Functions

### `parseHtml(html) -> page`

Parses an HTML string using the HTML5 parsing algorithm and returns a document handle (`NUMBER`).

```flat
html = '<main><p>Hello</p></main>'
dom.parseHtml(html) -> page
```

The parser performs HTML5 error recovery for malformed markup. The input limit is 16 MiB.

### `select(page, cssSelector) -> nodes`

Finds matching elements using a CSS selector and returns a `LIST` of numeric node handles. The first argument may be either a document handle or an element node handle. When an element is supplied, selection is scoped to that element and searches its descendants. The root element itself is not included. An unmatched selector returns an empty list.

```flat
dom.select(page, 'main p') -> paragraphs
list.len(paragraphs) -> count
```

CSS selectors can use the supported Lexbor selector syntax, including combinators, attribute selectors, selector groups, and pseudo-classes.

### `text(node) -> value`

Returns the text content of a node, including text from descendants. HTML character references are decoded by the HTML parser.

```flat
list.get(paragraphs, 0) -> paragraph
dom.text(paragraph) -> value
```

### `attr(node, name) -> value`

Returns an element attribute as a string. A missing attribute returns an empty string. The node must refer to an element.

```flat
dom.select(page, 'a') -> links
list.get(links, 0) -> link
dom.attr(link, 'href') -> href
```

### Element metadata and navigation

`tag(node)` returns the lower-case HTML tag name. `hasAttr(node, name)` checks whether an attribute exists; this distinguishes a missing attribute from an attribute present with an empty value.

`classes(node)` returns the element's class tokens as a `LIST` of strings, preserving their document order. `hasClass(node, className)` checks for an exact class token (not a substring).

`parent(node)`, `next(node)`, and `prev(node)` return the related element handle, or `NULL` when there is no related element. Non-element nodes such as text nodes are skipped for sibling navigation. `children(node)` returns only direct element children, in document order.

`closest(node, cssSelector)` tests the node itself first, then walks up its ancestors and returns the first matching element. It returns `NULL` if no element matches. Invalid selectors cause `dom.closest` to fail.

```flat
 dom.select(page, 'section.products') -> sections
 list.get(sections, 0) -> section
 dom.select(section, 'article.product') -> products
 list.get(products, 0) -> product
 dom.tag(product) -> tagName
 dom.parent(product) -> container
 dom.closest(product, 'main') -> main
 dom.classes(product) -> classNames
 dom.hasClass(product, 'featured') -> featured
```

All returned node handles belong to the same document as their source node and are invalidated by `close(page)`.

### `formFields(form) -> fields`

Extracts successful form controls inside the supplied element into an `OBJECT`, keyed by each control's `name`:

- Includes named `input`, `textarea`, and `select` controls.
- Excludes disabled controls, unnamed controls, unchecked checkboxes/radio buttons, and submit/button/reset/image inputs.
- Checked checkboxes and radio buttons are included; when their `value` attribute is absent, their value is `"on"`.
- A single `select` returns its selected option (or its first enabled option when none is explicitly selected). If an option has no `value` attribute, its text content is used.
- A `multiple` select returns a `LIST` of selected values. No selected options produces an empty list.
- Repeated field names are represented as a `LIST`, preserving document order.
- A disabled `fieldset` and its descendants are excluded.

```flat
use native list
use native obj

dom.select(page, 'form#login') -> forms
list.get(forms, 0) -> form
dom.formFields(form) -> fields
obj.get(fields, '"username"') -> username
obj.get(fields, '"tag"') -> tags
```

`formFields` accepts an element handle as its container; normally this is a `form` element selected from the document. It extracts values only; it does not submit the form.

### Control values and data attributes

`value(control)` reads the parsed value of an `input`, `textarea`, `select`, or `button`:

- `input` and `button`: the `value` attribute; checkbox/radio inputs without a `value` attribute return `"on"`.
- `textarea`: its text content.
- Single- or multiple-select: the first selected option's value; when no option is explicitly selected, the first enabled option is used. An option without a `value` attribute uses its text content.
- A control without a value returns an empty string. Unsupported element types cause `dom.value` to fail.

`data(node, name)` reads a `data-*` attribute. Pass either `sku` or `data-sku`; the prefix is added when omitted. Attribute names are treated case-insensitively. A missing attribute returns an empty string.

```flat
 dom.select(page, 'input[name="email"]') -> inputs
 list.get(inputs, 0) -> email
 dom.value(email) -> email_value
 dom.data(product, 'sku') -> sku
```

### Form control inspection

- `inputType(input)` returns the normalized, lower-case input type. An omitted `type` defaults to `text`.
- `checked(input)` returns whether a checkbox or radio input has the `checked` attribute. Other input types are rejected.
- `disabled(control)` checks the `disabled` attribute and inherited disabled state from a disabled `fieldset` or `optgroup`. Inputs inside the first `legend` of a disabled fieldset are not treated as disabled.
- `required(control)` checks the `required` attribute.
- `options(select)` returns all descendant `option` elements as node handles, in document order.
- `selectedOptions(select)` returns selected option node handles. For a single select with no explicit selection, it returns the first enabled option; for a multiple select with no selected options, it returns an empty list.

These functions inspect the parsed HTML state. FlatL's DOM module does not run browser scripts, perform user interaction, or maintain live form-control state; for example, `checked()` reflects the markup's `checked` attribute.

```flat
 dom.select(page, 'input[type="checkbox"]') -> checks
 list.get(checks, 0) -> check
 dom.checked(check) -> is_checked
 dom.disabled(check) -> is_disabled
 dom.required(check) -> is_required
 dom.inputType(check) -> type
```

`text(node)` already returns the full descendant text content (the DOM `textContent` concept); a separate `textContent()` alias is not provided.

### `close(page)`

Destroys the document and invalidates every node handle obtained from it. Node handles are not independently closable. Returns `true` on success.

```flat
dom.close(page)
```

## Complete example

```flat
use native dom
use native list

#main
  html = '<html><body><a class="item" href="/one">First</a><a class="item" href="/two">Second</a></body></html>'
  dom.parseHtml(html) -> page
  dom.select(page, 'a.item') -> links
  list.get(links, 0) -> first
  dom.text(first) -> label
  dom.attr(first, 'href') -> href
  io.outln(label)
  io.outln(href)
  dom.close(page)
  OK
```

## Build

DOM is disabled by default.

```sh
make rebuild dom=0
```

Use a Lexbor installation visible to the compiler and linker:

```sh
make rebuild dom=1
```

Or compile the bundled minimal Lexbor amalgamation from `external/lexbor/`:

```sh
make rebuild dom_compile=1
```

`dom_compile=1` automatically enables `dom`. The bundled source is generated from Lexbor v3.0.0 and contains the `html` and `selectors` modules plus their dependencies. It is compiled only when DOM is enabled. The default FlatL build does not compile or link Lexbor.

For the browser/WASM build, bundled Lexbor is required:

```sh
make rebuild dom_compile=1
```

The system-library mode (`dom=1 dom_compile=0`) is for native builds.

## Resource model and limits

- A document handle identifies an open document. A node handle identifies a node within one document.
- Node handles are scoped to their owning document; passing a document handle where an element is expected is rejected.
- Closing a document invalidates its node handles. Reused document slots receive a new token, so old handles do not become valid again.
- Up to 64 documents may be open simultaneously.
- A document may expose up to 100,000 distinct node handles.
- HTML input is limited to 16 MiB; CSS selectors are limited to 4 KiB.
- All still-open documents are destroyed automatically when the VM run ends.

## Tests

The suite is `test/cases/dom/`. Run it with:

```sh
bin/run_tests dom -v
```

The suite covers HTML parsing and recovery, CSS selector groups/combinators/pseudo-classes, text and attribute access (including long values and missing attributes), empty results, form control rules and repeated names, multiple document lifetimes, invalid selectors, invalid handle types, document close validation, and stale node handles after document reuse. Cases declare `mods = "dom"` so they are skipped when DOM is not built.

## Current scope

This version supports HTML parsing, document- and element-scoped CSS selection, DOM element metadata and navigation, text and form-control value extraction, data attributes, form-control inspection, form field extraction, and document cleanup. XML parsing, XPath, HTML serialization, and form submission are not part of this version.
