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
| `createFrag(page)` | NUMBER fragment handle | `dom.createFrag` |
| `clone(node, deep)` | NUMBER cloned node handle | `dom.clone` |
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

Returns the text content of a node, including text from descendants. HTML character references are decoded by the HTML parser. This is the raw DOM text content: it does not filter hidden elements or normalize whitespace.

```flat
list.get(paragraphs, 0) -> paragraph
dom.text(paragraph) -> value
```

### `textContent(node) -> value`

Alias for `text(node)`. It returns all descendant text with the same behavior and is provided for callers familiar with the browser DOM API.

```flat
dom.textContent(paragraph) -> value
```

### `innerText(node) -> value`

Returns an approximate rendered-text representation without a browser layout engine. It skips descendants marked with `hidden`, `script`, `style`, `template`, `noscript`, and hidden input controls; collapses whitespace and inserts line breaks around common block-level elements. It does not evaluate stylesheets, computed CSS, layout, or visibility inherited from CSS, so it is not identical to the browser's `HTMLElement.innerText`.

```flat
dom.innerText(section) -> visible_text
```

### `html(node) -> value`

Serializes the element's child nodes and returns their HTML markup. The selected element's own start and end tags are not included. Text and attribute values are escaped by the Lexbor HTML serializer. An element with no children returns an empty string.

```flat
dom.html(section) -> inner_html
```

### `outerHtml(node) -> value`

Serializes the selected element and its descendants, including the element's own tag. The result is HTML markup, not a reconstructed or pretty-printed representation; serialization follows Lexbor's HTML serializer.

```flat
dom.outerHtml(section) -> section_html
```

Both functions require an element node handle. They return a FlatL string that remains valid independently of the document; the document can be closed after serialization.

### `attr(node, name) -> value`

Returns an element attribute as a string. A missing attribute returns an empty string. The node must refer to an element.

```flat
dom.select(page, 'a') -> links
list.get(links, 0) -> link
dom.attr(link, 'href') -> href
```

### Element metadata and navigation

`tag(node)` returns the lower-case HTML tag name. `id(node)` returns the `id` attribute as a string (or an empty string when missing). `hasAttr(node, name)` checks whether an attribute exists; this distinguishes a missing attribute from an attribute present with an empty value.

`classes(node)` returns the element's class tokens as a `LIST` of strings, preserving their document order. `hasClass(node, className)` checks for an exact class token (not a substring).

`parent(node)`, `next(node)`, and `prev(node)` return the related element handle, or `NULL` when there is no related element. Non-element nodes such as text nodes are skipped for sibling navigation. `children(node)` returns only direct element children, in document order.

`closest(node, cssSelector)` tests the node itself first, then walks up its ancestors and returns the first matching element. It returns `NULL` if no element matches. Invalid selectors cause `dom.closest` to fail.

```flat
 dom.select(page, 'section.products') -> sections
 list.get(sections, 0) -> section
 dom.select(section, 'article.product') -> products
 list.get(products, 0) -> product
 dom.tag(product) -> tagName
 dom.id(product) -> productId
 dom.textContent(product) -> productText
 dom.html(product) -> productInnerHtml
 dom.outerHtml(product) -> productHtml
 dom.innerText(product) -> visibleProductText
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

### Creating and modifying nodes
`createFrag(page)` creates an empty, detached HTML `DocumentFragment` owned by the document and returns its ordinary numeric node handle. A fragment is a temporary container: append or prepend nodes to it while assembling a subtree, then insert the fragment into an element. Inserting a fragment moves all of its children into the target in order; the fragment itself remains valid but becomes empty. Inserting a fragment into another fragment composes the children in the same way. Fragment handles, like other node handles, remain valid until the owning document is closed.

`children(fragment)` returns its direct element children, just as it does for an element. `append`, `prepend`, and `insertBefore` accept a fragment as the child argument. The fragment and target must belong to the same document.

```flat
dom.createFrag(page) -> frag
dom.createElement(page, 'li') -> item
dom.setText(item, 'Alice')
dom.append(frag, item)
dom.append(list, frag) -- moves the fragment children into list
```


`createElement(page, tag)` creates a detached HTML element owned by `page`. The tag name is normalized to lower case and must contain only ASCII letters, digits, `-`, `_`, or `:`. `createText(page, text)` creates a detached text node, while `createComment(page, text)` creates a detached comment node. These calls return ordinary numeric node handles and can be used with the same navigation, serialization, and mutation APIs as parsed nodes.

Tree operations require all participating nodes to belong to the same document:

- `append(parent, child)` inserts the child at the end of the parent's children.
- `prepend(parent, child)` inserts it before the first child.
- `insertBefore(parent, child, reference)` inserts it immediately before `reference`, which must already be a child of `parent`.
- `replace(parent, oldNode, newNode)` replaces the existing direct child `oldNode` with `newNode`.
- Inserting an attached node moves it from its previous parent. Inserting a node into itself or one of its descendants is rejected.
- `remove(node)` detaches the node from its parent. The node handle remains valid and the node can be inserted elsewhere in the same document.
- `clear(node)` detaches all direct children. Their handles remain valid; the nodes are not destroyed.

Detached nodes remain owned by their original document. As with all DOM handles, they become invalid when that document is closed. Nodes from different documents cannot be combined.

```flat
 dom.parseHtml('<main></main>') -> page
 dom.select(page, 'main') -> roots
 list.get(roots, 0) -> root
 dom.createElement(page, 'section') -> section
 dom.setAttr(section, 'id', 'content')
 dom.createText(page, 'Hello') -> label
 dom.append(section, label)
 dom.prepend(root, section)
 dom.outerHtml(root) -> markup
```

### `clone(node, deep) -> copy`

`clone(node, deep)` creates a detached copy of a node in the same owning document. `deep` must be a FlatL BOOL:

- `false` creates a shallow copy: the node itself and its attributes are copied, but it has no children.
- `true` creates a deep copy including the complete descendant subtree.

The original node is never detached or modified. The copy receives a new node handle, belongs to the same document, and can be inserted or modified independently. Cloning a `DocumentFragment` follows the same shallow/deep rule. Document, document-type and attribute nodes are not accepted. The clone remains valid until its owning document is closed.

```flat
 dom.clone(card, false) -> empty_card
 dom.clone(card, true) -> card_copy
 dom.append(container, card_copy)
```

### Writing attributes, data and properties

`setAttr(node, name, value)` writes an element attribute as a string; `removeAttr(node, name)` removes it. `setData(node, name, value)` writes a `data-*` attribute and follows the same prefix rule as `data()`: pass either `sku` or `data-sku`. These operations return `true` on success.

`setProp(node, name, value)` is a limited writable counterpart to `prop()`, not a general JavaScript property setter:

- Boolean properties `checked`, `disabled`, and `required` require a FlatL BOOL. `checked` is accepted only for checkbox/radio inputs.
- String properties `id`, `className`, `name`, `type`, `href`, `src`, `title`, and `value` require a FlatL STRING. `className` maps to the `class` attribute. For ordinary elements and input/button controls, `value` maps to the `value` attribute; for `textarea`, it replaces the text content.
- Read-only, unknown, unsupported, or incorrectly typed properties fail with `dom.setProp`.

`setText(node, text)` detaches the node's existing children and inserts one text node when `text` is non-empty. It does not interpret the string as HTML. Existing detached child handles remain valid.

`setHtml(node, html)` parses `html` as an HTML5 fragment using `node` as the parsing context, then replaces its direct children with the parsed fragment. This means context-sensitive markup is handled correctly (for example, `<tr>` content inserted into a `<tbody>`). Existing children are detached rather than destroyed, so their handles remain valid. The new nodes belong to the same document as `node`. The target must be an element; parse failures leave its existing children untouched.

`setHtml` is a parser operation, not a sanitizer. It does not execute scripts or browser event handlers, and it does not update a live browser DOM. Sanitize untrusted markup before passing it to this function.

```flat
 dom.setAttr(section, 'title', 'Products')
 dom.setData(section, 'sku', 'A-42')
 dom.setProp(section, 'className', 'panel featured')
 dom.setProp(section, 'required', true)
 dom.setText(section, 'Plain text, not markup')
 dom.setHtml(section, '<p>Parsed <strong>HTML</strong></p>')
```

These functions modify the Lexbor DOM tree in memory. They do not update a browser page or FLEditor's live UI DOM; that requires a separate browser-side bridge.

### `prop(node, name)`

Reads a supported HTML/DOM property. This is a deliberately limited property accessor, not a JavaScript browser DOM implementation.

Supported names:
- String properties: `tagName` (uppercase), `id`, `className`, `name`, `type`, `value`, `href`, `src`, `title`.
- Boolean properties: `checked`, `disabled`, `required`.
- `checked` is supported only for checkbox and radio inputs. `type` is normalized for input elements.
- `value` follows the same parsed-markup rules as `value(node)`, including textarea text, select option values, and the checkbox/radio default `"on"`.
- Unknown or inapplicable properties return `NULL`. Missing string attributes return an empty string.

```flat
dom.select(page, 'input.email') -> fields
list.get(fields, 0) -> email
dom.prop(email, 'tagName') -> tag
dom.prop(email, 'type') -> input_type
dom.prop(email, 'name') -> field_name
dom.prop(email, 'required') -> is_required
dom.prop(email, 'value') -> current_value
```

These values describe the parsed HTML, not live browser properties after script execution or user interaction.

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

DOM defaults to the bundled Lexbor amalgamation in `external/lexbor/` (`dom_compile=1`).

```sh
make rebuild
```

Disable DOM:

```sh
make rebuild dom_compile=0
# or
make rebuild dom=0
```

Use a Lexbor installation visible to the compiler and linker (native only):

```sh
make rebuild dom=1 dom_compile=0
```

`dom_compile=1` defaults `dom` to `1`. The bundled source is generated from Lexbor v3.0.0 and contains the `html` and `selectors` modules plus their dependencies. Explicit `dom=0` still disables DOM even when `dom_compile` remains at its default.

**Availability:** native ✅ · WASM ✅ (bundled Lexbor only — `dom_compile=1`). Matrix: [README.md § WASM](./README.md#wasm-availability).

For the browser/WASM build, bundled Lexbor is required whenever DOM is enabled (default rebuild already does that).

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

This version supports HTML parsing, document- and element-scoped CSS selection, DOM element metadata and navigation, text and form-control value extraction, data attributes, form-control inspection, limited property access, HTML serialization, form field extraction, DocumentFragment construction, shallow/deep node cloning, and document cleanup. XML parsing, XPath, and form submission are not part of this version.


## Worker-mode status

DOM worker-mode support is currently `PENDING_THREAD_SAFETY_VALIDATION`; this does not mean the module is permanently unsupported. FlatL gives each VM its own DOM runtime and documents. Before enabling concurrent workers, the project must validate independent-context DOM operations on supported Lexbor builds. If that cannot be certified, the planned fallback is `SERIALIZED` through one process-wide gate covering both DOM native operations and Lexbor-backed document destruction during VM teardown. No worker-mode concurrency guarantee is currently made. See `plans/08_EXTERNAL_DEPENDENCY_AUDIT.md` in the source repository.


### Concurrent independent-context validation (archive 295)

The bundled Lexbor 3.0.0 amalgamation has a focused pthread regression in `test/cases/flvm/033_lexbor_concurrency/`. Eight threads independently create, parse, CSS-select, mutate, serialize and destroy 2,000 documents per run. The test passes normally and under ThreadSanitizer and AddressSanitizer/UndefinedBehaviorSanitizer in the current validation environment. This is a direct Lexbor test, not a concurrent complete-VM test. DOM worker-mode status therefore remains `PENDING_THREAD_SAFETY_VALIDATION`; complete-VM and supported system-Lexbor validation are still required before selecting `THREAD_SAFE`. `SERIALIZED` remains the fallback and must include document destruction during VM teardown.
