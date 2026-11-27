# FGIR dynamic optional-native union storage — archive 350

## Scope

Archive 350 replaces only `FgirProgram.optional_natives[32]` with an owned dynamic array. The per-unit `FgirNativeUnit.decls[32]` arrays and `FlatBytecode.optional_natives[32]` remain fixed and unchanged. FNAT/FNUN bytecode layouts and VM manifest ownership are unchanged.

## Representation and lifecycle

`FgirProgram` now stores:

```c
FlatOptionalNativeDecl *optional_natives;
int optional_native_count;
int optional_native_capacity;
```

`flat_fgir_reserve_optional_natives()` grows geometrically from one 128-byte record, capped at `FGIR_MAX_OPTIONAL_NATIVES == 32`. It checks the requested capacity, negative capacity state, multiplication overflow, and allocation failure. `flat_fgir_free()` releases the allocation and resets the complete FGIR object.

The union builder reserves before each unique append. It keeps `optional_native_count == 0` until the complete union has been built successfully. Its return values are:

- `0`: success;
- `-1`: more than 32 unique non-core modules;
- `-2`: allocation failure.

The linker preserves the existing overflow diagnostic for `-1` and reports a distinct out-of-memory diagnostic for `-2`.

## Semantics preserved

- Core modules are filtered from FNAT.
- Module names are deduplicated, not aliases.
- First-seen module ordering is preserved across native units and declarations.
- FGIR union aliases remain empty.
- Original source aliases remain in each `FgirNativeUnit.decls` and FNUN.
- The 32-module limit is unchanged.
- `FlatBytecode.optional_natives[32]`, FNAT serialization, FNUN serialization and VM runtime manifest remain unchanged.

## Tests

- `flc[18]`: zero/core-only no-allocation behavior, reserve lifecycle, geometric growth, data preservation, max 32/overflow rejection, 128 repeated allocate/free cycles, core filtering, duplicate suppression, first-seen ordering, empty union aliases, source alias preservation, and 33rd unique module rejection without committing a partial count.
- `flc[19]`: real compiler/linker/bytecode round-trip with all 20 optional modules supported by the build, repeated `http`, and a core `mem` alias. `fbdump` decodes the serialized bytecode and verifies FNAT order/content while FNUN retains original aliases.
- Existing focused `flc[1-19]`, `fmod` and `native_use`: 42/42 PASS.
- Direct lifecycle/union harness under ASan/UBSan with leak detection: PASS.

## Native ABI measurement

| Measurement | Before | After |
|---|---:|---:|
| `sizeof(FgirProgram)` | 4,408 B | 320 B |
| fixed optional-native table | 4,096 B | 0 B embedded |
| persistent layout reduction | — | 4,088 B |
| `sizeof(FlatOptionalNativeDecl)` | 128 B | 128 B |

Programs with no optional native modules do not allocate this table. A program with one optional module allocates one 128-byte record; capacity grows geometrically to the unchanged 32-record maximum.

## Validation status

- `make all -j2`: PASS.
- Focused compiler/linker/native-use tests: 42/42 PASS.
- Direct ASan/UBSan lifecycle and union harness with leak detection: PASS.
- Full default native suite: pending user-side Termux validation.
- Emscripten/WASM suite: pending user-side Termux validation.
