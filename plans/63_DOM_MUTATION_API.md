# DOM construction and mutation API

**Implementation checkpoint:** archive 353 (initial mutation API slice)

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

## Validation in archive 353

- `make all -j2` — PASS.
- `make docs` — PASS.
- `bin/run_tests dom -v -e` — 19/19 PASS.
- Full default `bin/run_tests -v -e` discovered 1,216 tests and selected 1,173; the run timed out during `flvm[53]`. It is not claimed as a full-suite PASS.
- `dom[19]` covers element/text/comment creation, append/prepend/insert-before/replace, moving, remove/clear, attribute/data/property writes, and text replacement.

## Follow-up candidates

- Add an HTML fragment parser path and `setHtml()` only after defining fragment context and error semantics; do not implement it by concatenating strings into the document.
- Consider a `createFragment()` API if fragment insertion is useful independently of `setHtml()`.
- Expand writable property coverage only where there is a clear typed mapping and tests; keep parsed DOM values distinct from browser live state.
- Add stress tests for many document open/close cycles and detached-handle lifetimes.
