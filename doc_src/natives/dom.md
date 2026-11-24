# Native `dom`

Optional HTML5 DOM support backed by [Lexbor](https://github.com/lexbor/lexbor).

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

## API

Declare the optional module in the source file header:

```flat
use native dom
use native list
```

### `dom.parseHtml(html) -> page`

Parses an HTML string using the HTML5 parsing algorithm and returns a document handle (`NUMBER`).

```flat
dom.parseHtml('<main><p>Hello</p></main>') -> page
```

### `dom.select(page, cssSelector) -> nodes`

Finds matching nodes using a CSS selector and returns a `LIST` of numeric node handles. The root document itself is not included in the result.

```flat
dom.select(page, 'main p') -> paragraphs
list.len(paragraphs) -> count
```

### `dom.text(node) -> value`

Returns the text content of a node, including descendant text.

```flat
list.get(paragraphs, 0) -> paragraph
dom.text(paragraph) -> value
```

### `dom.attr(node, name) -> value`

Returns an element attribute as a string. A missing attribute returns an empty string.

```flat
dom.select(page, 'a') -> links
list.get(links, 0) -> link
dom.attr(link, 'href') -> href
```

### `dom.formFields(form) -> fields`

Extracts successful form controls into an `OBJECT`, keyed by each control's `name`:

- Includes named `input`, `textarea`, and `select` controls.
- Excludes disabled controls, unnamed controls, unchecked checkboxes/radio buttons, and submit/button/reset/image inputs.
- Checked checkboxes and radio buttons are included; when their `value` attribute is absent, their value is `"on"`.
- A single `select` returns its selected option (or its first enabled option when none is explicitly selected). A `multiple` select returns a `LIST` of selected values.
- Repeated field names are represented as a `LIST`, preserving document order.

```flat
use native list
use native obj

dom.select(page, 'form#login') -> forms
list.get(forms, 0) -> form
dom.formFields(form) -> fields
obj.get(fields, '"username"') -> username
obj.get(fields, '"tag"') -> tags
```

`formFields` accepts an element handle as its container; normally this is a `form` element selected from the document.

### `dom.close(page)`

Destroys the document and invalidates every node handle obtained from it. Node handles are not independently closable.

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

## Resource model and limits

- A document handle and node handles are numeric resource IDs scoped to the current VM run.
- A document owns its complete Lexbor tree and all node handles created from it.
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

Current cases cover parsing and CSS selection, text and attribute access, form field extraction, document close validation, invalid selectors, and stale node handles after document reuse. Cases declare `mods = "dom"` so they are skipped when DOM is not built.

## Current scope

This initial version supports HTML parsing, CSS selection, text extraction, attribute access, and document cleanup. XML parsing, XPath, and HTML serialization are not part of this version.
