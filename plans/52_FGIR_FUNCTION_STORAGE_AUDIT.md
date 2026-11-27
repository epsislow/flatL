# FGIR Function Storage Audit

## Scope

Audit the fixed `FgirProgram.functions[FGIR_MAX_FUNCTIONS]` table as the next isolated FGIR storage candidate after dynamic blocks, instructions, and schemas. This is an audit-only checkpoint: no C implementation, parser behavior, linker behavior, or language limit changes are made here.

## Measured layout

Measured with the repository's native C compiler and the current `include/fgir.h`:

| Item | Size |
|---|---:|
| `sizeof(FgirFunction)` | 3,940 bytes |
| `FgirProgram.functions[256]` | 1,008,640 bytes |
| `sizeof(FgirProgram)` | 1,160,584 bytes |

The function table is the largest remaining fixed allocation in `FgirProgram`. Replacing the embedded array with an owned pointer plus count/capacity is expected to reduce `sizeof(FgirProgram)` by approximately 1,008,632 bytes (the embedded array replaced by one pointer), before any function records are allocated. The exact final size must be measured after implementation.

## Data ownership and copy semantics

`FgirFunction` currently contains only inline scalar and character-array fields. It contains no owned pointers, dynamic buffers, or references requiring deep clone. Its current value-copy operations are therefore safe:

- `merge_fmod_unit()` appends each `mod->functions[i]` by value.
- `merge_entry()` appends each `entry->functions[i]` by value.
- parser header/sugar paths fill a function record in place.
- `flat_func_prepare_fmod_unit()` mutates function names and implementation labels in place.

Unlike `FgirBlock`, no `flat_fgir_function_clone()` helper is needed if the representation remains self-contained. If pointer members are added to `FgirFunction` in the future, this conclusion must be revisited.

## Append and access sites

### Parser

`src/common/parse.c` has two append paths:

1. A normal `def` declaration checks `function_count`, takes `&out->functions[function_count]`, parses the header, then increments the count.
2. A `def` sugar clause checks the limit, takes both `proto = &out->functions[open_fn_sugar_base]` and `fn = &out->functions[function_count]`, parses the clause, then increments the count.

The sugar path is the key pointer-invalidation hazard. Capacity must be reserved **before taking either pointer**. `open_fn_idx` and `open_fn_sugar_base` are integer indexes and remain valid across reallocations. `finish_open_def()` obtains a short-lived pointer from an index and does not retain it across parser iterations.

The parser's existing 256-function diagnostic and rollback behavior after overload validation must remain unchanged. Failed parse attempts must leave the count consistent; unused capacity may remain allocated until `flat_fgir_free()`.

### Linker

`src/common/link.c` has two value-copy append loops:

- `merge_fmod_unit()` appends module functions.
- `merge_entry()` appends entry functions.

Both must reserve output capacity before writing and preserve the existing `too many functions after fmod link` / `too many functions after link` diagnostics. Because `FgirFunction` is self-contained, ordinary assignment remains correct.

### Readers and consumers

The remaining readers use indexes or short-lived pointers into the table:

- `flat_func_at()`, `flat_func_find()`, overload checks and call resolution in `flat_func.c` / `flat_func_tag.c`.
- function reclassification and validation in `flat_func.c`.
- call-site resolution and FFUN serialization in `compile.c`.
- runtime `FlatFbFunctionEntry` consumers in VM/executor code operate on the compiled bytecode table, not `FgirProgram.functions`.

No consumer was found retaining an `FgirFunction *` across a parser append or linker append. This must be rechecked after implementation, especially where a function pointer and a subsequent reserve could coexist.

## Related fixed table that is out of scope

`FgirProgram.exports[FGIR_MAX_FUNCTIONS][FGIR_MAX_FUNC_NAME]` is a separate fixed array used for fmod export names. It shares the numeric limit macro but is not the function-definition table. Do not convert it in this stage. `FGIR_MAX_FUNCTIONS` and the language limit remain 256.

## Proposed isolated implementation

- Replace `FgirFunction functions[FGIR_MAX_FUNCTIONS]` with an owned `FgirFunction *functions`, `function_count`, and `function_capacity`.
- Add `flat_fgir_reserve_functions()` using checked geometric growth, capped at `FGIR_MAX_FUNCTIONS`.
- Release the pointer in `flat_fgir_free()` and keep the zero-initialized program lifecycle contract.
- Reserve in both parser append paths before taking any `FgirFunction *` pointers.
- Reserve in both linker merge paths before value-copy append.
- Preserve inline `FgirFunction` fields and assignment semantics; do not introduce nested allocations or clone helpers.
- Do not modify exports, schemas, blocks, instructions, function semantics, bytecode format, VM, or scheduler in this stage.

## Required implementation tests

1. Reserve lifecycle: zero reserve, first allocation, geometric growth, data preservation, maximum 256, reject 257 and overflow-sized requests, free/reset, and repeated allocate/free cycles.
2. Parser boundaries: parse zero/one function, parse exactly 256 functions, reject the 257th with the existing diagnostic.
3. Function sugar: force capacity growth while parsing a multi-clause `def` and verify the prototype's metadata is preserved. This specifically guards the stale `proto` pointer hazard.
4. Linker: merge functions from imported modules and the entry program; test the aggregate 256 limit and rejection above it.
5. Semantics: compile and execute existing function, overload, multi-return, tag, callVM, and fmod tests unchanged.
6. ASan/UBSan with leak detection: exercise parse failure, overload rollback, link failure, successful link, and repeated `flat_fgir_free()` lifecycle.
7. Complete native `make test`, then Emscripten rebuild and Node.js WASM suites on the user's environment. Confirm compile high-water remains within the 128 MiB regression ceiling.

## Decision

Dynamic outer function-table storage is a justified next step: it removes about 1.0 MB of unconditional reservation per `FgirProgram`, while the function records remain self-contained and existing copy semantics remain valid. Proceed as a separate implementation checkpoint only after this audit; do not combine it with exports or any other FGIR storage change.
