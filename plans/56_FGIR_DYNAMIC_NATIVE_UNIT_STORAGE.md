# FGIR Dynamic Native-Unit Storage

## Scope

Archive 346 converts only the outer `FgirProgram.native_units` table from an embedded fixed array to an owned, capacity-managed array. Each `FgirNativeUnit` remains self-contained with its existing inline `decls[FGIR_MAX_OPTIONAL_NATIVES]` array. The optional-native union, fmod exports, FNUN bytecode representation, linker semantics, and VM are unchanged.

## Representation and ownership

`FgirProgram` now stores:

```c
FgirNativeUnit *native_units; /* owned */
int native_unit_count;
int native_unit_capacity;
```

`flat_fgir_reserve_native_units()` performs checked geometric growth capped at `FGIR_MAX_NATIVE_UNITS` (32), zeroes newly allocated records, and starts at capacity one. The one-record initial capacity is intentional because each `FgirNativeUnit` is 4,100 bytes; allocating eight records on the first append would reserve 32.8 KiB for a program that needs only one unit.

`flat_fgir_free()` releases the outer array. `FgirNativeUnit` and `FlatOptionalNativeDecl` remain pointer-free inline records, so the existing `memcpy` of declarations and bytecode conversion remain safe.

## Append and linker initialization

- `flat_native_use_add_unit()` now reserves before append, fills the new record, then increments `native_unit_count` only after successful allocation and copy.
- `prepare_unit_decls()` now uses `flat_native_use_add_unit()` rather than directly writing `native_units[0]`, ensuring linker-created local programs follow the same reserve/ownership path.
- `attach_unit_to_out()` continues copying declarations from the module scratch program into the distinct output program; no pointer is retained across an append.
- Existing limits and diagnostics are preserved: 32 native units and `too many linked native units` for the 33rd unit.

## Tests added

- `flc[13]`: reserve/free lifecycle, zero reserve, exact one-record initial capacity, geometric growth, declaration preservation across realloc, exact maximum, 33/overflow rejection, reset, and 128 allocate/free cycles.
- `flc[14]`: linker accepts 31 imported fmods plus the entry (32 native units) and rejects 32 imported fmods plus the entry (33 units) with the existing diagnostic. Each imported module declares a distinct native alias, exercising per-unit declaration preservation and FNUN compilation.
- Existing `fmod` (18 cases) and `native_use` (6 cases) suites cover fmod merge, alias scoping, and native resolution semantics.

## Native layout impact

Measured on the native ABI after the implementation:

| Item | Before | After |
|---|---:|---:|
| `sizeof(FgirNativeUnit)` | 4,100 B | 4,100 B |
| `FgirProgram.native_units` reservation | 131,200 B embedded | 8 B pointer |
| `sizeof(FgirProgram)` | 151,960 B | 20,776 B |

The `FgirProgram` object shrinks by 131,184 bytes. A program that needs one native unit allocates one 4,100-byte record; programs with no native-unit metadata allocate none. Capacity grows 1, 2, 4, 8, 16, 32 as needed.

## Validation

- `make all -j2`: PASS.
- `bin/run_tests 'flc[13-14]' -v -e`: 2/2 PASS.
- `bin/run_tests 'flc[1-14]' fmod native_use -v -e`: 37/37 PASS.
- The lifecycle harness passed combined ASan/UBSan with leak detection.
- A sanitizer-built `flc` passed the 32-unit success and 33-unit rejection linker cases with ASan/UBSan and leak detection.
- `make docs`: PASS, 50 sections.
- A full `make test` attempt was made while the local `bin/flc` still came from a sanitizer build configured with optional DOM disabled. Since Make does not track compiler-flag changes, the subsequent non-clean `make all` did not rebuild that binary; the run therefore showed 18 expected DOM-configuration failures and was stopped by the 300-second execution limit at 286/1,162. This is not treated as a valid full-suite result.
- After `make clean && make all -j2` with default features, the 18 DOM tests passed, the three previously interrupted FLVM tests (`flvm[38,40,46]`) passed, and `flc[13-14]` passed again. A complete default `make test` and fresh Emscripten/WASM rebuild remain pending user-side validation. The implementation does not change the bytecode format or WASM-specific code.

## Explicitly out of scope

- Dynamic `FgirNativeUnit.decls` storage.
- Dynamic `optional_natives` and `exports` tables.
- Changes to native module parsing, alias semantics, FNUN serialization, bytecode format, VM, worker runtime, or plugin loading.

## User-side validation confirmation

The user subsequently applied archive 346 on Termux and confirmed:

- `make test`: 1,162/1,162 PASS in 2:16.493.
- `make fleditor-wasm-test`: complete aggregate WASM suite PASS, including smoke, environment, multifile, CSV, order-flow, variable, API/lifecycle, limits/recovery/memory checks.
- WASM linear memory remained 42,729,472 bytes after 100 repeated runs.

Archive 346 is therefore confirmed by the user on both native Termux and WASM test suites.
