# flatL documentation

**Language:** all files in this directory (and the root [README.md](../README.md), [test/README.md](../test/README.md)) are written in **English**.

| Document | Contents |
|----------|----------|
| [SPECIFICATION.md](./SPECIFICATION.md) | Language model, keywords vs natives, `.fb` format, VM overview |
| [LANGUAGE_TYPES.md](./LANGUAGE_TYPES.md) | BOOL / NUMBER / STRING (INLINE + REF), pool, limits |
| [TOOLCHAIN.md](./TOOLCHAIN.md) | `make`, `flc`, `flvm`, `run_tests`, test cases, **portability (Windows/WSL)** |
| [IO.md](./IO.md) | **`io.*` / `str.to_*`**, stdin/stdout, cancel, PANIC, tests |
| [IO_COLLECTIONS.md](./IO_COLLECTIONS.md) | Normative: `json.*`, collection stdin/stdout rules |
| [LIST_OBJECT.md](./LIST_OBJECT.md) | **Guide:** LIST/OBJECT + JSON I/O for authors |

Design notes and Cursor **plans** under [`.cursor/`](../.cursor/) may be bilingual; **normative specs** for the toolchain live here in English.
