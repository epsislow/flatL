# DOM DocumentFragment construction — `createFrag`

**Implementation checkpoint:** archive 355

## Goal

Provide a lightweight, detached container for assembling DOM subtrees before inserting them into an element. The public name is `dom.createFrag(page)` (confirmed by the user); `DocumentFragment` remains the conceptual DOM type.

## API and semantics

- `createFrag(page)` creates an empty Lexbor `DocumentFragment` and returns an ordinary numeric node handle owned by `page`.
- `append(fragment, child)`, `prepend(fragment, child)` and `insertBefore(fragment, child, reference)` use the existing tree-mutation API.
- A fragment may be inserted into another fragment to compose a subtree.
- Inserting a fragment into an element transfers its children, in order, to the destination. The fragment itself remains valid and becomes empty, following DOM fragment-insertion semantics.
- `children(fragment)` returns its direct element children, allowing inspection before insertion.
- Fragments obey the existing document ownership rule: nodes from different documents cannot be combined, and all fragment handles are invalidated when their owner document closes.
- No separate fragment destroy operation is needed; the owning document controls lifetime.

## Example

```flat
use native dom
use native list

dom.parseHtml('<ul id="users"></ul>') -> page
dom.select(page, '#users') -> lists
list.get(lists, 0) -> users

dom.createFrag(page) -> frag
dom.createElement(page, 'li') -> item
dom.setText(item, 'Alice')
dom.append(frag, item)
dom.append(users, frag)
```

## Validation

- `dom[21]` covers fragment creation, child construction, nested fragment composition, ordered insertion, empty-after-insertion behavior, and final serialization.
- `make all -j2` — PASS.
- `bin/run_tests dom -v -e` — 21/21 PASS.
- `make docs` — PASS (50 sections).
- Full default suite attempt: 1,175 selected from 1,218 discovered; timed out during `flvm[53]`. `flvm[54-57]` passed separately (4/4); no full-suite PASS is claimed.

## Boundaries

- This is a Lexbor in-memory DOM feature, not a browser DOM bridge.
- A fragment is not an element; element-only APIs such as attribute/property access do not apply.
- The current `children()` behavior returns element children only, consistent with its existing element behavior.
