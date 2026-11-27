# FGIR Dynamic Export Storage (Archive 348)

## Scope

Replace only the fixed `FgirProgram.exports[256][64]` table with an owned, capacity-managed array of fixed-width export names. Preserve `.fmod` syntax and semantics, the 256-symbol limit, and the 64-byte symbol storage width.

## Representation and lifecycle

`FgirProgram` now contains:

```c
int export_count;
char (*exports)[FGIR_MAX_FUNC_NAME]; /* owned */
int export_capacity;
```

`flat_fgir_reserve_exports()` performs checked geometric growth, starting at one record and doubling up to `FGIR_MAX_FUNCTIONS`. It rejects capacities above the unchanged maximum and checks multiplication overflow before `realloc`. `flat_fgir_free()` releases the allocation; the existing full-struct reset returns pointer/count/capacity to zero.

`add_export()` retains the existing order and behavior:

1. Empty symbol: no-op.
2. Duplicate symbol: no-op.
3. 257th distinct symbol: existing `too many export symbols` diagnostic.
4. Otherwise reserve one slot, copy into the same 64-byte fixed-width record, and increment the count.

If allocation fails, the new diagnostic is `out of memory while growing FGIR exports`; the count is not incremented and `realloc` preserves the old allocation.

## Memory result

Measured on the same native ABI as archive 346:

| Measurement | Before | After |
|---|---:|---:|
| `sizeof(FgirProgram)` | 20,776 B | 4,408 B |
| Fixed export table | 16,384 B | pointer + capacity (12 B, including alignment effects in the containing struct) |
| Reduction in `sizeof(FgirProgram)` | — | 16,368 B |

The export array is allocated only when the first non-empty, non-duplicate export is parsed. Programs with no exports allocate no export records. A one-export module reserves one 64-byte record; growth is geometric and capped at 256 records.

## Tests added

- `flc[15]`: direct reserve/free lifecycle, zero reserve, initial capacity one, geometric growth, data preservation, maximum 256, rejection of 257 and overflow-sized requests, free/reset, and 128 repeated allocate/free cycles.
- `flc[16]`: parser accepts 256 distinct exports, empty export lines and duplicates remain no-ops, and the 257th distinct export is rejected with the existing diagnostic.
- `flc[17]`: `.fmod` with 256 exported function definitions links successfully; an export without a matching `def` retains the existing diagnostic.
- Existing complete `fmod` suite remains green.

## Validation performed

- `make all -j2`: PASS.
- Focused `flc`, `fmod`, and `native_use`: 39/39 PASS before the additional maximum-`.fmod` boundary case; the new `flc[17]` also passes independently.
- Combined `flc[16-17]` and `fmod` using an ASan/UBSan compiler build: 20/20 PASS.
- Direct export lifecycle harness under ASan/UBSan with leak detection: PASS.
- `make docs`: PASS, 50 sections.
- Full native `make test`: attempted with 1,164 selected tests, but the local execution was terminated by the 300-second tool limit at 320/1,164. This is not a full-suite PASS. The user must validate the complete suite on Termux.
- Fresh Emscripten/WASM validation remains pending in the user's Termux environment.

## Explicitly unchanged

- `FGIR_MAX_FUNCTIONS == 256` and `FGIR_MAX_FUNC_NAME == 64`.
- Export parser syntax, duplicate suppression, empty-symbol handling, symbol truncation behavior, and `.fmod` aliasing.
- Function and native-unit storage, optional-native union, linker merge behavior, bytecode, VM and runtime.

## Next audit candidate

After this conversion, `FgirProgram.optional_natives[32]` is the largest remaining fixed array in the structure: 4,096 B (`32 * sizeof(FlatOptionalNativeDecl)`, with each record 128 B). Audit it separately before deciding on dynamic storage; do not combine it with another implementation.
