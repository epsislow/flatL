# FGIR dynamic schema declaration storage — archive 341

## Scope

This checkpoint changes only the outer `FgirProgram.schemas` array. `FgirSchemaDecl` remains self-contained: the schema name, field count, field keys, and field type specifications are still stored inline. Dynamic per-schema field arrays and dynamic function metadata are explicitly out of scope.

## Representation and ownership

- `FgirProgram.schemas` is now an owned `FgirSchemaDecl *`.
- `schema_count` remains the number of initialized declarations; `schema_capacity` tracks allocated slots.
- `flat_fgir_reserve_schemas()` grows geometrically from an initial capacity of eight and never exceeds `FGIR_SCHEMA_MAX` (64).
- Allocation-size multiplication is checked before `realloc`; newly allocated declaration slots are zeroed.
- The parser reserves before initializing/appending a declaration and increments `schema_count` only after reserve succeeds.
- The linker reserves before copying each entry schema and increments the count only after the value copy succeeds. Because `FgirSchemaDecl` remains entirely inline, the existing declaration value-copy behavior remains valid.
- `flat_fgir_free()` frees the schema array and resets its pointer/count/capacity with the rest of FGIR.
- The `flat_schema_build_catalog()` API remains unchanged and receives the same contiguous `FgirSchemaDecl` sequence.

## Language compatibility

- `FGIR_SCHEMA_MAX` remains 64.
- `FGIR_SCHEMA_FIELD_MAX`, key/spec lengths, schema validation and bytecode schema catalog behavior are unchanged.
- The parser still reports `too many schemas` for declaration 65.
- The linker still reports `too many schemas after link` if the combined declaration count exceeds the language limit.
- No AST, schema-field, function-table, VM, or scheduler refactor is included.

## Regression tests

- `flc[6]`: direct reserve/free lifecycle, zero-capacity no-op, geometric growth, data preservation across realloc, exact maximum, over-limit/overflow rejection, and 128 allocate/free cycles.
- `flc[7]`: compile and execute a program with exactly 64 schema declarations.
- `flc[8]`: compile and execute an entry that imports a block module while retaining an entry schema, exercising the linker's `merge_entry()` schema append.
- `flc[9]`: reject the 65th schema with the existing parser diagnostic.
- Existing `types` suite covers schema parsing, validation, catalog construction and runtime typed binding.

## Validation

- `make all -j2`: PASS.
- `make docs`: PASS, 50 sections generated.
- `bin/run_tests 'flc[6-9]' -v -e`: 4/4 PASS.
- `bin/run_tests 'flc[1-9]' -v -e`: 8/8 PASS (IDs 1–4 and 6–9; ID 5 is unused).
- `bin/run_tests types -v -e`: 251/251 PASS.
- `bin/run_tests fmod -v -e`: 18/18 PASS.
- `bin/run_tests native_use -v -e`: 6/6 PASS.
- The reserve/free harness passed ASan+UBSan with leak detection. A separately sanitizer-built `flc` compiled the 64-schema and schema-link cases and rejected the 65-schema case with the expected diagnostic; no sanitizer report was emitted.
- Full `make test` was attempted; the outer execution window terminated it at 280/1,156 selected tests. This is not recorded as a full-suite pass.
- Fresh Emscripten rebuild and `make fleditor-wasm-api-smoke` / `make fleditor-wasm-test` are pending in the user's Linux/Termux SDK environment.

## Structure-size impact

Measured on the same native ABI as archive 340:

| Structure/member | Before archive 341 | After archive 341 |
|---|---:|---:|
| `FgirSchemaDecl` | 24,644 B | 24,644 B |
| `FgirProgram.schemas` embedded reservation | 1,577,216 B | 8 B pointer |
| `FgirProgram` | 2,737,792 B | 1,160,584 B |

The first schema allocation reserves eight declarations (197,152 bytes (`8 * sizeof(FgirSchemaDecl)`)). Programs with no schemas allocate no schema declaration array. The fixed language maximum remains 64.

## Next isolated candidate

The next candidate is the fixed `FgirProgram.functions[FGIR_MAX_FUNCTIONS]` array (1,008,640 bytes in archive 340). It must receive its own access/copy/ownership audit and a separate checkpoint. Do not combine it with dynamic schema fields or any other FGIR storage changes.
