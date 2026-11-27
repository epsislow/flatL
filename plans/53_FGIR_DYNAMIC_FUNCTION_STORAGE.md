# FGIR Dynamic Function Storage

## Scope

Archive 343 converts only `FgirProgram.functions` from an embedded `FGIR_MAX_FUNCTIONS` array to an owned dynamic array. The function record layout, language limit, bytecode format, VM, executor and independent fmod exports table are unchanged.

## Representation and ownership

`FgirProgram` now stores:

```c
FgirFunction *functions; /* owned */
int function_count;
int function_capacity;
```

`flat_fgir_reserve_functions()` performs checked geometric growth, capped at `FGIR_MAX_FUNCTIONS` (256). Newly allocated capacity is zeroed. `flat_fgir_free()` releases the function allocation and resets its pointer/count/capacity through the existing whole-program reset.

`FgirFunction` remains self-contained: it has no pointer members or nested owned allocations. Parser/linker value copies therefore remain valid; no clone helper is needed. If pointer members are added to `FgirFunction` later, linker copy semantics must be revisited.

## Parser and linker changes

Both parser append paths reserve before writing a new record. The `def` sugar clause path specifically reserves **before obtaining either** the prototype pointer or destination pointer, because `realloc()` may move the table. The prototype index remains stable.

Both linker append paths reserve before copying module or entry functions. The existing diagnostics are preserved:

- `too many functions after fmod link`
- `too many functions after link`

Allocation failures have dedicated out-of-memory diagnostics. The 256-function limit is unchanged.

## Tests added

- `flc[10]`: reserve lifecycle, geometric growth, data preservation, max/overflow rejection, free/reset and 128 repeated allocation/free cycles.
- `flc[11]`: parser accepts exactly 256 functions and rejects 257 with the existing diagnostic.
- `flc[12]`: linker merges 128 fmod + 128 entry functions and rejects 129 + 128 with the existing linker diagnostic.
- `func[38]`: `def` sugar prototype at slot 8 remains valid while appending slot 9 forces capacity growth.
- Existing fmod/function/native-use tests cover unchanged semantics and both linker paths.

## Native layout

Measured with the native compiler after the change:

| Item | Before | After |
|---|---:|---:|
| `sizeof(FgirFunction)` | 3,940 B | 3,940 B |
| `sizeof(FgirProgram)` | 1,160,584 B | 151,960 B |
| Embedded/dynamic function table | 1,008,640 B embedded | allocated on demand |

`sizeof(FgirProgram)` is reduced by 1,008,624 bytes. Function storage is now allocated only when a program declares or links functions.

## Validation status

The implementation checkpoint's validation is recorded in `plans/02_ROADMAP.md`. The lifecycle harness and parser/linker boundary scripts pass under ASan/UBSan with leak detection. The targeted `flc[1-12]` and `func[38]` tests pass, and `make docs` passes (50 sections). A local `make test` attempt reached 280/1,160 before the 300-second outer execution limit terminated it; the full suite is therefore incomplete. A real Emscripten rebuild and Node.js WASM validation still require the user's Termux/Emscripten environment.

## Explicitly out of scope

- `FgirProgram.exports[FGIR_MAX_FUNCTIONS][FGIR_MAX_FUNC_NAME]` remains fixed-size.
- `FGIR_MAX_FUNCTIONS` remains 256.
- No changes to function semantics, overload dispatch, function metadata, compiler serialization, bytecode, runtime VM or task scheduler.
