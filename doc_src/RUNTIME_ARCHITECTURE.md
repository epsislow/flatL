# FlatL runtime architecture

This document maps **what lives where** in the flatL repository today. It is normative only for layers and boundaries described here; language features not yet in the toolchain are out of scope.

## Three layers

| Layer | Responsibility | In this repo |
|-------|----------------|--------------|
| **OS / portability** | Files, I/O, env, processes — **C only** for the toolchain (`flat_port_*` in `src/port/`). User programs use **native modules** (`file`, `io`, …), not raw OS calls from FlatL. | [PORTABILITY.md](./PORTABILITY.md) |
| **VM (C)** | Bytecode, values, memory pools, **VCX**, call stack, control flow, native dispatch. No user filesystem logic inside `vm.c`. | `src/common/vm.c`, `compile.c`, `flat_vcx.c`, `value_*.c` |
| **FlatL language** | Source, blocks, `use`, natives, stdlib `.fl` blocks. Compilers and tools are separate binaries (`flc`, `flvm`, `flatl`). | `src/flc/`, `blocks/`, `doc_src/` |

```text
FlatL sources (.fl) — blocks, use, natives
        │
        ▼
flc — parse, FGIR, link → .fb
        │
        ▼
flvm — bytecode, VCX, natives (C + optional plugins)
        │
        ▼
flat_port / OS — toolchain & native backends
```

## VM primitives (today)

- **Bytecode** opcodes: control flow, assign, natives, `call #block`, **`call block` *expr*** (`OP_CALL_BLOCK`), `return`, `for` / `foreach`, etc. — see [CALL_BLOCK.md](./CALL_BLOCK.md).
- **VCX** (`use native vcx`): multiple variable storages; **`create`**, **`set`**, **`destroy`**, copy helpers including **`varsCopyByNames`** — see [natives/vcx.md](./natives/vcx.md).
- **Natives**: registered in `src/lib/native_registry.c`; core implementations in `src/lib/mod_*.c` and `src/common/flat_vcx.c`.
- **`var_slots`**: compile-time count of distinct bind names for the linked program; global VCX capacity follows this value.

## What stays implementable in FlatL

- Policy and orchestration in **`.fl` blocks** (executors, helpers) calling **documented natives** only.
- No direct access to FGIR/C structs from FlatL sources.

## OS / sandbox (hosts)

| Host | Notes |
|------|--------|
| **Desktop (`flvm`)** | Real cwd and paths via `file` / `io` natives. |
| **fleditor (WASM)** | Same bytecode; I/O and paths constrained by the editor sandbox — see [FLEDITOR.md](./FLEDITOR.md). |

Native modules available in WASM are those **linked into** the build; user `.so` plugins are not loaded in the browser.

## Toolchain binaries

| Binary | Role |
|--------|------|
| `flc` | Compile `.fl` → `.fb` |
| `flvm` | Run `.fb` |
| `flatl` | Compile + run convenience |
| `run_tests` | Regression suites under `test/cases/` |

## Related docs

- [SPECIFICATION.md](./SPECIFICATION.md) — language surface
- [natives/README.md](./natives/README.md) — optional native modules
- [PORTABILITY.md](./PORTABILITY.md) — C portability boundary
