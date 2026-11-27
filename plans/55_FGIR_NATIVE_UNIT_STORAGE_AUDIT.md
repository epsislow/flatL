# FGIR Native-Unit Storage Audit

## Scope

This is an audit-only checkpoint. It does not change FGIR layout, native-use behavior, bytecode, linking, or runtime behavior. The audit identifies the next isolated fixed-storage candidate after archives 339, 341, and 343.

## Native ABI measurements

Measured with the same native compiler/headers as the archive 344 source tree:

| Structure/member | Size |
|---|---:|
| `sizeof(FlatOptionalNativeDecl)` | 128 B |
| `sizeof(FgirNativeUnit)` | 4,100 B |
| `FgirProgram.native_units[32]` | 131,200 B |
| `FgirProgram.optional_natives[32]` | 4,096 B |
| `FgirProgram.exports[256][64]` | 16,384 B |
| `sizeof(FgirProgram)` | 151,960 B |

The embedded native-unit table is therefore the dominant remaining unconditional FGIR reservation. Replacing only the outer `native_units[32]` array with an owned pointer plus count/capacity is expected to reduce `sizeof(FgirProgram)` by 131,192 bytes, to approximately 20,768 bytes on this ABI, before allocating any native-unit records.

## Ownership and record semantics

`FgirNativeUnit` currently contains only:

```c
typedef struct {
    FlatOptionalNativeDecl decls[FGIR_MAX_OPTIONAL_NATIVES];
    int decl_count;
} FgirNativeUnit;
```

`FlatOptionalNativeDecl` contains only fixed-size character arrays. Neither type contains pointers, nested owned allocations, or external references. The records can therefore remain self-contained and be copied by value if the outer table becomes dynamic. No clone helper is required for the proposed outer-array-only change.

The audit found no whole-`FgirProgram` assignment or `memcpy` that would duplicate an owned native-unit pointer. Programs are zero-initialized, released through `flat_fgir_free()`, and linker output is reset through that lifecycle.

## Append and access sites

### Append / initialization

- `flat_native_use_add_unit()` in `src/common/native_use.c` is the only general append operation. It enforces `FGIR_MAX_NATIVE_UNITS` (32), increments `native_unit_count`, and fills the selected record.
- `prepare_unit_decls()` in `src/common/link.c` initializes the module's single local native-unit record at index zero after scanning the source file. This direct initialization must reserve capacity before obtaining `&mod->native_units[0]`.
- `attach_unit_to_out()` reads the module's record and passes its declarations to `flat_native_use_add_unit()` on the distinct output program. This does not alias source and destination storage.

### Readers

- `flat_native_use_build_fnat_union()` iterates the table and reads declaration module names.
- Native-use resolution helpers in `native_use.c` obtain declaration pointers from an indexed unit and consume them synchronously; no pointer is retained across an append.
- `compile.c` copies each unit's inline declarations into the separate `FlatFbNativeUnit` bytecode representation. This is a conversion boundary, not a shallow copy of the `FgirProgram` table.
- VM runtime readers consume bytecode-owned `FlatFbNativeUnit` storage, not `FgirProgram.native_units`.

No parser append path or runtime consumer directly mutates the FGIR native-unit table.

## Limits and compatibility constraints

- Keep `FGIR_MAX_NATIVE_UNITS == 32`.
- Keep `FGIR_MAX_OPTIONAL_NATIVES == 32` and the per-unit declaration layout unchanged in this stage.
- Preserve the exact diagnostics `too many linked native units` and `too many use native directives in one file`.
- Keep `FlatFbNativeUnit`, FNUN serialization, bytecode format, VM lookup, and native alias resolution unchanged.
- Preserve the zero-initialized `FgirProgram` lifecycle and ensure `flat_fgir_free()` releases/reset native-unit storage.

## Proposed isolated implementation

1. Replace only `FgirProgram.native_units[FGIR_MAX_NATIVE_UNITS]` with an owned `FgirNativeUnit *native_units`, retaining `native_unit_count` and adding `native_unit_capacity`.
2. Add `flat_fgir_reserve_native_units()` with checked allocation arithmetic, geometric growth capped at 32, and zero-initialized newly allocated slots. Because each record is 4,100 bytes, consider starting at capacity one rather than the generic eight-record initial allocation; measure and document the choice.
3. Free the outer array in `flat_fgir_free()`.
4. Reserve before append in `flat_native_use_add_unit()` and before direct index-zero initialization in `prepare_unit_decls()`.
5. Keep `FgirNativeUnit` and its `decls[32]` inline for this checkpoint. Do not combine outer-table allocation with dynamic per-unit declaration arrays.
6. Add lifecycle tests, exact 32/33 unit boundary tests, linker aggregation tests, and native-use alias-scope regressions. Run failure and repeated-free paths under ASan/UBSan with leak detection.

## Explicitly out of scope

- Dynamic `FgirNativeUnit.decls` storage.
- Dynamic `optional_natives` or `exports` tables.
- Changes to native module parsing, alias semantics, FNUN serialization, bytecode format, VM, worker runtime, or plugin loading.

## Decision

The outer native-unit table is the next justified isolated FGIR storage target: it accounts for 131,200 bytes (about 86% of the current `FgirProgram` size). Its append surface is small, record types are self-contained, and bytecode conversion already copies data into a separate owned representation. Proceed with an outer-table-only implementation in the next checkpoint.
