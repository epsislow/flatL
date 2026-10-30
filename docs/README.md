# flatL documentation

**Language:** all files under **`docs/`** (and the root [README.md](../README.md), [test/README.md](../test/README.md)) must be written in **English**. Source code and comments are **English** too. Cursor **plans** under [`.cursor/plans/`](../.cursor/plans/) may use Romanian.

**Offline browser docs:** from the repo root, run **`make docs`** (or **`bin/gen_doc_site`**), then open **`doc_site/index.html`**. The table of contents is driven by **[index.json](./index.json)** — add a section there when you add a new normative doc page.

| Document | Contents |
|----------|----------|
| [SPECIFICATION.md](./SPECIFICATION.md) | Language model, keywords vs natives, `.fb` format, VM overview |
| [LANGUAGE_TYPES.md](./LANGUAGE_TYPES.md) | BOOL / NUMBER / STRING (INLINE + REF), pool, limits |
| [TOOLCHAIN.md](./TOOLCHAIN.md) | `make`, `make test-flatl`, `flc`, `flvm`, **`flatl`**, **`[flatl]`** in `case.toml`, `run_tests`, **portability** |
| [PORTABILITY.md](./PORTABILITY.md) | **Normative:** `flat_str`, `flat_port`, MSVC/GCC/Clang rules |
| [IO.md](./IO.md) | **`io.*` / `str.to_*`**, stdin/stdout, cancel, PANIC, tests |
| [natives/README.md](./natives/README.md) | **Optional** `use native … with …`; core vs optional; index |
| [natives/math.md](./natives/math.md) | **`math`** — libm, aggregations, constants |
| [natives/math.trig.md](./natives/math.trig.md) | **`math.trig`** — sin, cos, tan |
| [natives/list.md](./natives/list.md) | **`list`** — pack, push/prepend/insert/set, slice, splice, range, repeat, search |
| [natives/str.md](./natives/str.md) | **`str`** — len, slice, concat, split/join, search, trim, case, replace |
| [natives/obj.md](./natives/obj.md) | **`obj`** — empty, get/set, has, len, remove, keys, values |
| [natives/math.stats.md](./natives/math.stats.md) | **`math.stats`** — mean, median, var, stdev |
| [IO_COLLECTIONS.md](./IO_COLLECTIONS.md) | Normative: `json.*`, collection stdin/stdout rules |
| [LIST_OBJECT.md](./LIST_OBJECT.md) | **Guide:** LIST/OBJECT + JSON I/O for authors |

Design notes and Cursor **plans** under [`.cursor/`](../.cursor/) may be bilingual; **normative specs** for the toolchain live here in English.
