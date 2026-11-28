# DOM construction and mutation API

**Implementation checkpoint:** archive 355 (mutation API, HTML fragment replacement and DocumentFragment construction)

## Existing base

The DOM module already exposes parsed HTML, CSS selection, node navigation, serialization, form inspection, `data(node, name)`, and the read-only `prop(node, name)` subset. The mutation layer extends those APIs rather than introducing a second node/property model.

## API added in archive 353

- Construction: `createElement(page, tag)`, `createText(page, text)`, `createComment(page, text)`.
- Tree operations: `append(parent, child)`, `prepend(parent, child)`, `insertBefore(parent, child, reference)`, `replace(parent, oldNode, newNode)`, `remove(node)`, `clear(node)`.
- Attribute/data writes: `setAttr(node, name, value)`, `removeAttr(node, name)`, `setData(node, name, value)`.
- Property/text writes: `setProp(node, name, value)`, `setText(node, text)`.

## Ownership and behavior contract

- Every created node belongs to the document supplied at construction time.
- Tree mutations reject nodes from different documents and reject cycles.
- Insertion uses Lexbor's DOM pre-insert/replace operations, so moving an attached node detaches it from its previous parent according to DOM insertion semantics.
- `remove()` and `clear()` detach nodes; they do not destroy them. Their handles remain valid until the owning document is closed.
- Closing the document invalidates all its handles, including detached nodes.
- `setText()` treats input as text, never as HTML. Existing children are detached rather than destroyed.
- `setProp()` is deliberately limited and typed: boolean properties require BOOL, supported string properties require STRING. It is not a JavaScript DOM property implementation.
- Lexbor tree mutations affect only FlatL's in-memory document. They do not update a browser/FLEditor live DOM; that requires a separate browser bridge.

## API added in archive 354

- HTML fragment replacement: `setHtml(node, html)`. It parses with Lexbor using the target element as the HTML5 fragment context, then replaces direct children.
- Existing children are detached, not destroyed, preserving their handles. New nodes belong to the target document. The target must be an element. Parse failure leaves the existing subtree unchanged.
- The operation does not execute scripts or browser event handlers and is not an HTML sanitizer. Untrusted markup must be sanitized by the caller.

## Validation in archive 354

- `make all -j2` — PASS.
- `make docs` — PASS.
- `bin/run_tests dom -v -e` — 20/20 PASS.
- Full default suite was attempted but timed out during `flvm[53]`; no full-suite PASS is claimed.
- `dom[20]` covers nested HTML parsing, serialization, context-sensitive `<tr>` parsing in `<tbody>`, empty replacement, and preservation of detached child handles.

## Follow-up candidates

- [x] Add an independently constructible fragment API; implemented as the shorter `createFrag(page)` name. See [64 — DOM DocumentFragment](64_DOM_DOCUMENT_FRAGMENT.md).
- Expand writable property coverage only where there is a clear typed mapping and tests; keep parsed DOM values distinct from browser live state.
- Add stress tests for many document open/close cycles and detached-handle lifetimes.


## Validation in archive 355 (including this API)

- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- `bin/run_tests dom -v -e` — 21/21 PASS.
- Full default `bin/run_tests -v -e` discovered 1,218 tests and selected 1,175; it timed out during `flvm[53]`. All suites reached before that point passed. `flvm[54-57]` passed separately (4/4). No full-suite PASS is claimed.
- `dom[21]` covers fragment creation, nested fragment composition, ordered insertion, empty fragment handles after insertion, and final serialization.
