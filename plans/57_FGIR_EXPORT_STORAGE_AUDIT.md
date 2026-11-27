# FGIR Export-Table Storage Audit (Archive 347)

## Scope

Audit the fixed `FgirProgram.exports[FGIR_MAX_FUNCTIONS][FGIR_MAX_FUNC_NAME]` table as the next isolated FGIR storage candidate after dynamic block, instruction, schema, function, and native-unit storage.

This archive is audit-only. It does not change C code, parser behavior, linker behavior, bytecode, or runtime semantics.

## Native ABI measurements

Measurements were taken from the archive 346 headers using the host C compiler:

| Item | Size |
|---|---:|
| `sizeof(FgirProgram)` | 20,776 B |
| `sizeof(FgirNativeUnit)` | 4,100 B |
| `sizeof(FlatOptionalNativeDecl)` | 128 B |
| `FgirProgram.optional_natives[32]` | 4,096 B |
| `FgirProgram.exports[256][64]` | 16,384 B |

`exports` currently accounts for about 78.9% of the remaining `FgirProgram` size. Replacing only the fixed table with `char (*exports)[FGIR_MAX_FUNC_NAME]` plus an integer capacity is projected to reduce `sizeof(FgirProgram)` to 4,408 B on this ABI, saving 16,368 B before any export records are allocated. The estimate was checked by compiling a temporary header-only layout variant; repository source was not modified for that measurement.

The table is allocated only when an export is actually added, so ordinary `.fl` programs with no exports would not need this allocation.

## Access and ownership audit

The complete source search found only two logical access areas:

1. `src/common/flat_func.c:add_export()`:
   - ignores an empty symbol;
   - checks existing entries for duplicates;
   - preserves the 256-symbol limit and the diagnostic `too many export symbols`;
   - appends one fixed-width `FGIR_MAX_FUNC_NAME` symbol and increments `export_count`.
2. `src/common/flat_func.c:export_has()` and `flat_func_prepare_fmod_unit()`:
   - `export_has()` performs a read-only linear lookup;
   - `flat_func_prepare_fmod_unit()` validates every declared export against the function table and then uses the export names to expose the selected function names under the import alias.

No export-table access exists in the linker merge loops, compiler serialization, bytecode loader, disassembler, VM, or runtime. The export manifest is consumed during `.fmod` preparation before the functions/blocks are merged into the output program. `merge_fmod_unit()` intentionally does not merge export metadata into the final output.

No pointer or reference into the export table is retained across an append. The duplicate scan and append happen within `add_export()`, while `flat_func_prepare_fmod_unit()` only reads exports and mutates function records. A future reserve may therefore move the export allocation without invalidating any stored references.

## Lifecycle audit

`FgirProgram` has no shallow-copy/assignment or clone path in the current source. Programs are zero-initialized, parsed into, and released through `flat_fgir_free()`:

- `flat_parse_unit()` first calls `flat_fgir_free(out)`, zeroes the program, and initializes parser metadata.
- `flat_link_program()` frees and resets its output before linking; its reusable module scratch is also freed before each parse.
- CLI (`flc`, `flatl`) and WASM compilation paths call `flat_fgir_free()` before freeing their `FgirProgram` allocation.
- Link failure cleanup calls `flat_fgir_free(out)`.

The export allocation can follow the existing owned-array lifecycle: free it in `flat_fgir_free()` and rely on the existing full-struct reset. No deep clone or linker copy support is needed.

## Recommended isolated implementation

Convert only the export-name table:

```c
char (*exports)[FGIR_MAX_FUNC_NAME]; /* owned */
int export_count;
int export_capacity;
```

Add `flat_fgir_reserve_exports(FgirProgram *, size_t)` with checked geometric growth, capped at the unchanged 256-export maximum. Since export records are small and most modules expose very few names, start with capacity one and grow 1, 2, 4, 8, ... rather than eagerly reserving eight records. Preserve the existing duplicate behavior, empty-symbol behavior, symbol truncation behavior, and maximum-count diagnostic. On allocation failure, return a distinct out-of-memory diagnostic without incrementing `export_count`.

Free the allocation in `flat_fgir_free()`. Do not change `FGIR_MAX_FUNCTIONS`, export syntax, symbol length, function count limits, `.fmod` name mapping, or the final linked program representation.

## Required tests for the implementation archive

1. Reserve/lifecycle C harness: zero state, first allocation, growth and data preservation, capacity 256, reject 257, invalid/overflow request, free/reset, and repeated allocate/free cycles.
2. Parser boundaries: zero exports, one export, duplicates, empty symbols, exactly 256 distinct exports, and rejection of the 257th with the existing `too many export symbols` diagnostic.
3. Allocation failure path: ensure count and previously stored symbols remain intact when reserve fails (use the project's existing allocation-failure mechanism if one is available; do not introduce a general allocator framework solely for this test).
4. `.fmod` semantic coverage: existing fmod suite, multiple exports, alias mapping, missing-exported-def diagnostic, and a generated maximum-boundary module if practical.
5. ASan/UBSan with leak detection for reserve/free, parse failure, duplicate handling, and fmod preparation/link failure.
6. `make all -j2`, `make docs`, full native suite, and user-side Emscripten/WASM suite.

## Explicitly out of scope

- `optional_natives[32]` (4,096 B): audit separately after exports.
- Any changes to `FgirNativeUnit.decls[32]`.
- Changes to export count/length limits, syntax, visibility rules, function naming, linker merge semantics, bytecode, or VM.
- Combining export storage with optional-native storage in one implementation.

## Decision

Proceed with a separate dynamic export-table implementation in the next archive, keeping the maximum at 256 and the fixed-width 64-byte symbol representation. The expected reduction in the baseline `sizeof(FgirProgram)` is 16,368 B, from 20,776 B to approximately 4,408 B on the measured native ABI.
