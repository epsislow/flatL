# DOM Node Cloning

## Goal

Add a small, explicit clone operation to the Lexbor-backed DOM API so callers can duplicate a parsed or constructed node without rebuilding its structure manually.

## API

`dom.clone(node, deep) -> copy`

- `node` is an existing node handle.
- `deep` is a required FlatL BOOL.
- `false`: clone only the node and its attributes; no child nodes are copied.
- `true`: clone the complete descendant subtree.
- The original node is not modified or detached.
- The clone is detached and receives a new node handle in the same owning document.
- Document, document-type and attribute nodes are rejected. Element, text, comment and DocumentFragment nodes are supported.
- Clone handles follow the normal document ownership and close-time invalidation rules.

## Implementation

Use Lexbor's `lxb_dom_node_clone()` which imports the node into its existing owner document. Register the native signature, plugin handler and public documentation. Do not expose cross-document cloning in this API; callers can only clone within the source document.

## Tests

`test/cases/dom/022_clone/` validates shallow and deep element cloning, copied attributes, copied descendant text, detached insertion, original-node preservation, text-node cloning, and shallow/deep fragment cloning.

## Scope boundary

This clones the Lexbor in-memory DOM. It does not clone browser live-DOM state, JavaScript listeners, or runtime properties that are not represented in the parsed DOM tree.

## Validation

- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- `bin/run_tests dom -v -e` — PASS (22/22).
- Full default suite — 1,176 selected from 1,219 discovered; timed out during `flvm[53]`. All visibly completed tests before timeout passed; full-suite PASS is not claimed.
