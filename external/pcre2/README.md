# Bundled PCRE2 (8-bit, no JIT)

Upstream **PCRE2 10.49** sources used by FlatL's optional `re` native module.

- Built with `SUPPORT_PCRE2_8`, **without** `SUPPORT_JIT` (dummy `pcre2_jit_compile.c`).
- Headers: `pcre2.h` / `config.h` from upstream `*.generic`.
- Chartables: `pcre2_chartables.c.dist`.

Default FlatL build: `re_compile=1` compiles these files into `flvm`/`flatl`.
Pass `re_compile=0` with `re=1` to link system `libpcre2-8` instead (`pkg-config`).
Pass `re=0` to omit the module.
