# FGIR Optional-Native Manifest Storage Audit (Archive 349)

## Scope

Audit only `FgirProgram.optional_natives[FGIR_MAX_OPTIONAL_NATIVES]` as the next remaining fixed FGIR metadata table after dynamic block, instruction, schema, function, native-unit, and export storage.

This archive is audit-only. It does not change C sources, parser/linker behavior, bytecode, runtime behavior, or test behavior.

## Native ABI measurements

Measurements were taken from the archive 348 headers using the host C compiler:

| Item | Size |
|---|---:|
| `sizeof(FgirProgram)` | 4,408 B |
| `sizeof(FlatOptionalNativeDecl)` | 128 B |
| `FgirProgram.optional_natives[32]` | 4,096 B |
| `offsetof(FgirProgram, optional_natives)` | 40 B |
| `offsetof(FgirProgram, optional_native_count)` | 4,136 B |
| `offsetof(FgirProgram, functions)` | 4,144 B |

A temporary header-only layout variant replacing the inline table with a pointer and capacity field measured `sizeof(FgirProgram) == 320 B` on the same ABI. This projects a reduction of 4,088 B before allocating any optional-native records. The temporary variant was outside the repository; no project source was modified for the measurement.

## Data-flow and semantics audit

The FGIR table is not the same object as the fixed `FlatBytecode.optional_natives[32]` manifest in `include/fb_format.h`. The latter remains part of the decoded bytecode/runtime representation and is explicitly out of scope.

The FGIR union is built in exactly one place:

- `src/common/link.c` calls `flat_native_use_build_fnat_union(out)` once, after all entry/module/fmod native units have been attached and merged, and before validation, call-site resolution, lowering, and compilation.
- `src/common/native_use.c:flat_native_use_build_fnat_union()` iterates `FgirProgram.native_units` in order and each unit's declarations in source order.
- Core native modules are excluded from this optional manifest.
- Duplicate module names are suppressed by comparing only `.module`; the first occurrence determines order.
- Each union record's `.alias` is explicitly set to the empty string. Aliases remain attached to the per-unit declarations and are used for source-level call resolution; the final FNAT union is module-only.
- The union count is committed to `optional_native_count` only after the complete traversal succeeds.

The compiler reads the FGIR union only when `optional_native_count > 0`, and passes the pointer/count directly to `flat_fb_write_native_section()`. There are no other FGIR-table readers or writers. The bytecode writer's input type remains `const FlatOptionalNativeDecl *`, so a dynamic FGIR pointer preserves that interface and the serialized FNAT representation.

## Ownership, lifetime, and pointer audit

- `FgirProgram` is zero-initialized by CLI/WASM/linker allocation paths and released through `flat_fgir_free()`.
- `flat_parse_unit()` first calls `flat_fgir_free(out)`, resets the complete structure, and then builds native-unit declarations. It does not build the optional-native union itself.
- `flat_link_program()` builds the union after all units have been attached; its failure cleanup calls `flat_fgir_free(out)`.
- The no-import path parses directly into `out`, attaches one native unit, and reaches the same final union-building call.
- The import path reuses a separate `FgirProgram` scratch object and attaches each unit's declarations to `out`; it does not shallow-copy `FgirProgram` objects.
- No pointer or reference into `optional_natives` is retained across an append or beyond compilation's immediate call to the FNAT writer.
- No whole-`FgirProgram` assignment, `memcpy`, or clone path was found.

The table can therefore follow the existing FGIR owned-array lifecycle: add pointer plus capacity metadata, reserve before writing each new unique module, and free it in `flat_fgir_free()`. The decoded `FlatBytecode` manifest and runtime manifest pointers must remain unchanged.

## Error-path observation

`flat_native_use_build_fnat_union()` currently returns `-1` both when the 32-module maximum is exceeded and for any future reserve/allocation failure. `flat_link_program()` currently maps every nonzero result to `too many optional native modules in program`.

The dynamic implementation should distinguish allocation failure from the existing 33rd-module overflow diagnostic. One minimal option is to preserve `0` for success, `-1` for the existing maximum violation, and use a separate negative result (for example `-2`) for allocation failure; the linker can then emit a specific out-of-memory diagnostic. Keep `optional_native_count == 0` until the union has been built successfully, preserving the current commit-at-end behavior.

## Recommended isolated implementation

Convert only the FGIR outer union table:

```c
FlatOptionalNativeDecl *optional_natives; /* owned */
int optional_native_count;
int optional_native_capacity;
```

Add `flat_fgir_reserve_optional_natives(FgirProgram *, size_t)` with checked geometric growth, starting at one 128-byte record and capped at `FGIR_MAX_OPTIONAL_NATIVES == 32`. Keep `FlatOptionalNativeDecl` and its 64-byte module plus 64-byte alias fields unchanged; although the final union currently leaves aliases empty, retaining the record type avoids changing the FNAT writer contract or conflating FGIR storage with bytecode format.

Update `flat_native_use_build_fnat_union()` to reserve before each unique append, preserve core filtering, first-seen ordering, module-only duplicate suppression and empty aliases, and distinguish OOM from the 33rd unique module. Free the allocation in `flat_fgir_free()`.

## Required tests for the implementation archive

1. Direct reserve/lifecycle C harness: zero state, zero reserve, initial capacity one, geometric growth, preservation of module and alias bytes, maximum 32, rejection of 33 and overflow-sized requests, failed-reserve state preservation, free/reset, and repeated allocate/free cycles.
2. Union builder coverage: core-module exclusion, duplicate module suppression across units, first-seen ordering, empty alias in the union, and count committed only on success.
3. Boundary coverage: direct reserve accepts 32 and rejects 33. If a synthetic union-builder harness is practical, exercise 32 unique non-core declarations and reject the 33rd while retaining the existing diagnostic.
4. Integration coverage: existing `native_use`, `fmod`, and linker suites; ensure modules declared in multiple units still produce one FNAT entry and source aliases still resolve through each unit's declarations.
5. Compiler/bytecode coverage: no optional natives produces no FNAT section; one or more optional modules serialize and decode with identical module order and empty aliases. Keep `FlatBytecode.optional_natives[32]` unchanged.
6. ASan/UBSan with leak detection for reserve/free, union build, repeated link/free, and failure paths. Do not introduce a general allocator framework solely to force OOM if none exists.
7. `make all -j2`, `make docs`, full native suite, and user-side Emscripten/WASM suite.

## Explicitly out of scope

- `FlatBytecode.optional_natives[32]` and bytecode FNAT layout.
- `FgirNativeUnit.decls[32]` and per-unit aliases.
- Native registry, plugin loading, VM/runtime manifest ownership, and source-level native alias resolution.
- Changes to `FGIR_MAX_OPTIONAL_NATIVES == 32`, module-name width, deduplication semantics, module ordering, or core-module filtering.
- Any other FGIR table in the same implementation.

## Decision

Proceed with a separate dynamic FGIR optional-native union table, keeping the maximum at 32 and preserving the existing `FlatOptionalNativeDecl` representation. The expected native `sizeof(FgirProgram)` reduction is 4,088 B, from 4,408 B to approximately 320 B before any optional-native records are allocated. Keep the decoded bytecode manifest fixed and unchanged.
