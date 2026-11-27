# FGIR dynamic block and instruction storage — archive 339

## Motivation

Archive 338 reduced the linker peak by avoiding unnecessary full `FgirProgram` objects, but the representation still embedded 1,024 blocks in every program and 128 full `FgirInstr` records in every block. On the native ABI this made `sizeof(FgirProgram)` 418,121,328 bytes, `sizeof(FgirBlock)` 405,648 bytes, and `sizeof(FgirInstr)` 3,164 bytes. The browser/WASM compile high-water remained unacceptable.

## Representation change

- `FgirProgram.blocks` is now an owned pointer with `block_count` and `block_capacity`.
- `FgirBlock.instr` is now an owned pointer with `instr_count` and `instr_capacity`.
- Both arrays grow geometrically from an initial capacity of eight, with checked bounds and checked allocation sizes.
- Existing language limits are unchanged: `FGIR_MAX_BLOCKS` remains 1,024 and `FGIR_MAX_INSTR` remains 128 per block. The change is about reserved memory, not language capacity.
- `FgirProgram` and its dynamic members must be zero-initialized before first use and released with `flat_fgir_free()`.

## Ownership and copy rules

- `flat_fgir_free()` releases each block's instruction allocation, then the block array, and resets the program.
- `flat_fgir_reserve_blocks()` and `flat_fgir_reserve_instr()` own capacity growth and zero newly reserved elements.
- `flat_fgir_block_clone()` performs a deep copy of instruction storage; raw structure assignment of `FgirBlock` is not valid because it would copy the owned pointer.
- Linker append/merge paths now use deep clone semantics. Temporary cloned blocks release their own instruction storage after append.
- Parser and linker reuse paths free prior scratch storage before resetting/reparsing.
- CLI, flatl, and WASM compilation paths release the linked FGIR before freeing the containing program object.
- Function-call lowering reserves instruction capacity before `memmove`/`memcpy` insertion.

## Regression coverage

- New native `flc[4]` creates 65 additional blocks with 17 instructions each, forcing repeated block and instruction capacity growth, then compiles and executes the entry block.
- Existing `fmod`, `func`, `native`, `native_use`, local block, line-label, call-block and jump-limit suites cover parser, linker clone/merge and lowering paths.
- The Node.js WASM memory test now asserts a 128 MiB ceiling for a simple no-import compile and compiles a generated 81-block / 17-instruction workload to exercise both dynamic arrays in the real WASM module.
- The test remains Node.js `.mjs`; no Python test runner is introduced.

## Validation so far

- `make all -j4`: PASS.
- Focused compiler/linker suites: 113/113 PASS.
- New `flc[4]`: PASS.
- AddressSanitizer: `flc[4]` passed three consecutive runs with leak detection enabled.
- AddressSanitizer import/link smoke (`examples/use_demo/program.fl`): three consecutive compile-and-run cycles passed with leak detection enabled.
- Native structure sizes after the change: `FgirInstr=3164`, `FgirBlock=672`, `FgirProgram=2737792` bytes.
- Native `/usr/bin/time -v` measurements: simple one-block compile about 5,976 KB RSS; 65-block / 17-instruction workload about 14,100 KB RSS; one-module import example about 6,528 KB RSS.
- Full `make test`: PASS, 1,152/1,152 default-category tests, 4:30.190.
- This environment has no Emscripten SDK, so the changed WASM module cannot be rebuilt here. The user must run `make rebuild` and `make fleditor-wasm-test` on Linux/Termux to validate the 128 MiB ceiling and dynamic-growth workload against a fresh artifact.

## Follow-up audit

The direct `FgirBlock` copy sites in the parser/linker/lowering path were audited. The linker now deep-clones blocks; no shallow `FgirBlock` assignment remains in `src`. Continue auditing public FGIR API callers and failure paths as new FGIR fields are introduced. A later optimization may consider dynamically allocating other fixed-size FGIR metadata, but the dominant block/instruction reservation has been removed in this step.
