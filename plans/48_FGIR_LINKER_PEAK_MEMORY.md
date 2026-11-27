# FGIR linker peak-memory reduction — archive 338

## User-side measurement that triggered the audit

After rebuilding the WASM target on Termux, the user ran both focused and aggregate WASM suites successfully. The new limits/recovery test reported:

- explicit linear-memory growth: `25,559,040 -> 42,729,472` bytes;
- compile high-water: `1,263,337,472` bytes;
- after 100 subsequent runs: `1,263,337,472` bytes.

The high-water value is `HEAPU8.byteLength`, so it is the size of the WASM linear memory after its largest growth, not a live-allocation count and not proof of a leak. The unchanged value after 100 runs is evidence against continued per-run growth, but does not reduce the large initial compile footprint.

## Source-level allocation trace

`sizeof(FgirProgram)` on the native ABI is 418,121,328 bytes. `flat_wasm.c::compile_linked_entry()` allocates the final output `FgirProgram` before calling `flat_link_program()`. The old linker then allocated both:

1. one reusable `mod` scratch `FgirProgram`, unconditionally, even when the entry had no imports;
2. a separate `entry_prog` `FgirProgram` after processing imported modules.

For a no-import entry, these three simultaneously live fixed-capacity programs explain the observed order of magnitude: roughly three `FgirProgram` allocations plus allocator/runtime overhead. This is a source-level allocation trace, not an instrumented Emscripten allocation stack trace.

## Change in archive 338

- If the entry has no `.use` modules and no header `.fmod` uses, parse it directly into the caller-provided output `FgirProgram` and prepare its native-unit declarations there. This removes both unnecessary scratch `FgirProgram` allocations for the no-import case.
- If imports exist, retain one reusable `mod` scratch object for modules and then reuse that same object to parse the entry. This removes the separate `entry_prog` allocation and reduces the peak from three full `FgirProgram` objects to two.
- Preserve the existing fixed language limits; no WASM-specific block/instruction limits are introduced.
- Extend `wasm_api_limits_memory_smoke.mjs` with a generous 600 MiB high-water ceiling for a simple no-import compile. This catches accidental reintroduction of multiple ~418 MB FGIR objects while allowing allocator/page-growth overhead.

This is an intermediate reduction, not the final memory design. A single `FgirProgram` is still very large because `blocks[1024]` and each block's `instr[128]` are embedded fixed-capacity arrays. A follow-up should evaluate capacity-managed block/instruction storage and ownership/copy semantics before changing the representation.

## Validation

- `make all -j2`: PASS.
- `bin/run_tests fmod native native_use -v -e`: PASS (46/46), covering the import/link paths most directly affected.
- Native `/usr/bin/time -v` measurement for a no-import compile: maximum RSS 416,612 KB.
- Native `/usr/bin/time -v` measurement for `examples/use_demo/program.fl` (one `.use` module): maximum RSS 823,192 KB.
- A full `make test` invocation was started but stopped during the unrelated FLVM harness portion; it is not counted as a pass. The user had independently passed 1,151/1,151 on the preceding snapshot, before this linker-only change.
- The current environment has Node.js but no `emcc`; the modified C source cannot be rebuilt into a fresh WASM artifact here. The new 600 MiB assertion must therefore be confirmed by the user after `make rebuild` on Linux/Termux. The archived WASM binary is not treated as evidence for the changed linker code.

## Scope

Only the linker allocation lifecycle, the Node.js memory regression assertion, and planning/documentation change. No VM, scheduler, task/channel, or bytecode semantics are changed.
