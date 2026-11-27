# FGIR parser/storage audit — checkpoint 340

## Scope and method

This is a source audit of archive 339 only. No C implementation or language behavior was changed. The audit covers the parser's intermediate representation, fixed-capacity metadata in `FgirProgram`, ownership/copy sites, and the next low-risk incremental storage change.

## Important architecture finding: there is no separate AST

The current compiler does not build a conventional AST and then lower it to FGIR. `flat_parse_unit()` in `src/common/parse.c` reads source lines with a fixed `FGIR_MAX_LINE + 8` buffer and emits `FgirProgram`/`FgirBlock`/`FgirInstr` records directly. `LocalBlockFrame[64]` is parser nesting state, not an AST. Therefore an “AST allocation” refactor is not applicable to this codebase as it currently exists.

The next storage audit should target the fixed-capacity metadata still embedded in `FgirProgram`, not invent an AST layer.

## Native structure-size measurements

Measured with the archive 339 `include/fgir.h` and host compiler:

| Structure/member | Size |
|---|---:|
| `FgirInstr` | 3,164 B |
| `FgirBlock` | 672 B |
| `FgirFunction` | 3,940 B |
| `FgirSchemaDecl` | 24,644 B |
| `FgirNativeUnit` | 4,100 B |
| `FgirProgram` | 2,737,792 B |
| `FgirProgram.functions[256]` | 1,008,640 B |
| `FgirProgram.schemas[64]` | 1,577,216 B |
| `FgirProgram.native_units[32]` | 131,200 B |
| `FgirProgram.exports[256]` | 16,384 B |

The two largest remaining fixed arrays (`schemas` and `functions`) account for about 94.4% of `FgirProgram`'s current size. The block/instruction dynamic-storage change from archive 339 is already effective; this audit does not revisit it.

## Parser and schema observations

- `flat_parse_unit()` resets the supplied program, initializes function metadata, and parses source directly into FGIR.
- The source-line buffer is `FGIR_MAX_LINE + 8` (520 bytes); it is not a large per-program allocation.
- Local parser nesting uses `LocalBlockFrame local_frames[64]`, also small relative to FGIR metadata.
- Schema declarations are capped at `FGIR_SCHEMA_MAX == 64`.
- Each `FgirSchemaDecl` embeds 128 field keys and 128 field type specifications. This makes one declaration 24,644 bytes even if it has very few fields.
- Schema declaration storage is touched in `parse.c`, appended in `link.c`, and passed as a contiguous declaration array to `flat_schema_build_catalog()` in `flat_schema.c`.
- The linker currently copies a schema declaration by value. This remains safe if only the *outer schema declaration array* becomes dynamic, because each `FgirSchemaDecl` still owns its field arrays inline. It would cease to be a safe shallow copy only in a later step that makes a schema's fields pointer-owned.

## Function metadata observations

- `FgirFunction` is 3,940 bytes, and the fixed 256-entry array reserves about 1.01 MB in every `FgirProgram`.
- Function entries are used throughout parser, `flat_func.c`, `flat_func_tag.c`, compiler, and linker.
- Parser and linker currently assign/copy complete `FgirFunction` values. Since all fields are inline today, these copies are valid.
- Converting the function array to dynamic storage is feasible, but it has a wider access/copy-site surface than schema declarations and should be a separate later checkpoint.

## Ownership/copy-site audit

- The schema list has one parser append path, one linker append path, and one catalog-consumer API. Outer-array growth can preserve element addresses only until a reserve; code must not retain `FgirSchemaDecl *` across a reserve. Current parser code uses the current schema index and obtains the pointer inside the parse loop.
- `merge_entry()` copies schemas by value. With an outer dynamic array and inline fields this is still a valid value copy.
- Function storage has numerous indexed readers and two linker append loops; parser stores function indices (`open_fn_idx`) rather than long-lived pointers across parsing operations. The parser must nevertheless reacquire a `FgirFunction *` after any reserve that could move the array.
- `flat_fgir_free()` is the central FGIR cleanup point. Any future dynamic schema/function arrays must be released there and reset consistently on parser failure/reuse.
- No separate AST ownership or AST clone/free API exists in this snapshot.

## Recommended first implementation step — completed in archive 341

**Archive 341 makes only `FgirProgram.schemas` a capacity-managed dynamic array.** `FgirSchemaDecl` itself remains unchanged (including its inline field arrays) in this step. Implementation and validation details are recorded in `51_FGIR_DYNAMIC_SCHEMA_STORAGE.md`.

Reasons:

1. It removes the largest remaining unconditional reservation: 1,577,216 bytes per `FgirProgram` with zero schemas.
2. The call-site surface is small: `fgir.h`, `fgir.c`, `parse.c`, `link.c`, and focused schema/link tests.
3. Existing schema value-copy semantics remain valid because `FgirSchemaDecl` remains self-contained.
4. It provides a clean ownership checkpoint before considering dynamic per-schema fields or dynamic function metadata.

Implementation constraints for that step:

- Preserve `FGIR_SCHEMA_MAX` and all schema validation/error behavior.
- Add a checked `flat_fgir_reserve_schemas()` helper with bounded geometric growth and overflow-safe allocation.
- Free the schema array in `flat_fgir_free()`.
- Reserve before parser append and linker merge; do not increment counts until the append succeeds.
- Reacquire schema pointers after reserve; do not retain pointers across possible reallocations.
- Keep `flat_schema_build_catalog()`'s input contract unchanged in this step.
- Add focused tests for zero/one/multiple schemas, entry schema merge during link, maximum/overflow rejection, and repeated schema reserve/free lifecycle.
- Run focused schema/link suites and ASan/UBSan before moving to the next structure.
- Do not combine this with dynamic schema fields or dynamic function storage.

## Later candidates (not part of archive 341)

1. Dynamic `FgirFunction` list: potentially removes another 1,008,640-byte unconditional reservation, but requires a broader audit of parser, linker, function resolution, tags and compiler references.
2. Dynamic per-schema field arrays: reduces the 24,644-byte per-schema footprint, but changes schema clone/merge ownership and the `flat_schema_build_catalog()` input contract. Treat as a separate step after the outer schema list is stable.
3. Revisit `native_units` and export metadata only if measurements show material benefit; they are much smaller than schemas/functions.

## Validation baseline

The user confirmed on Termux that archive 339's fresh WASM rebuild passes:

- `make fleditor-wasm-api-smoke`
- `make fleditor-wasm-test`
- compile high-water: 42,729,472 bytes; unchanged after 100 repeated runs.

The user also reported the dynamic FGIR version is faster in their environment. Preserve these as archive 339 comparison points. No performance claim is made by this source-only audit.

## Archive 341 result

The outer schema declaration array is now dynamically allocated with geometric growth, bounded by the unchanged `FGIR_SCHEMA_MAX == 64`. `FgirSchemaDecl` and its inline field arrays remain unchanged. The new implementation, tests, measured structure-size reduction, and validation status are documented in `51_FGIR_DYNAMIC_SCHEMA_STORAGE.md`.
