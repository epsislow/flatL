# Block-scoped panic `.any` — archive 352

## Scope

Implement the previously planned general block-scoped wildcard handler `#panic.#target.any`. This is a general panic-routing feature, not a task-only special case.

## Resolution order

1. Exact handler for the faulting instruction (`#panic.#target.index` or line-label form).
2. `.any` handler for the faulting block (`#panic.#target.any`).
3. `.any` handlers for active caller blocks, nearest caller first.
4. Global fallback `#panic.any`.

Recovery transfers control to the handler in the same VM/task and VCX. A called block can handle its own panic before the caller wildcard is considered.

## Implementation

- Parser/linker line-label validation recognizes the trailing `any` as a wildcard suffix, not a line-label alias.
- Compiler resolves explicit `PANIC` to a block-scoped wildcard before the global handler.
- VM runtime resolves wildcard handlers for the faulting block and then walks active call return sites to find caller block wildcards.
- Existing exact-site and global handler behavior is preserved.

## Tests

- `panic[7]`: native panic in a block handled by its `.any`.
- `panic[8]`: panic in a nested called block handled by the caller block `.any`.
- `panic[9]`: explicit `PANIC` compiled to block-scoped `.any`.
- `flvm[57]`: spawned child-task VM recovers through its block-scoped `.any`.

## Validation

- `make all -j2` — PASS.
- `bin/run_tests 'panic[1-9]' -v -e` — PASS (9/9).
- `bin/run_tests 'flvm[57]' -v -e` — PASS (1/1).
- Full default suite — not run in this checkpoint.

## Completion

This closes the planned v1.0 block-scoped panic `.any` item. Work is paused after archive 352 as requested; v2.0 platform validation and v2.5 UI Runtime are not started here.
