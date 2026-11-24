# Native module `enc`

Encode / decode **binary STRING** payloads with a named scheme. **Optional** — declare before use:

```text
use native enc
```

Input and output are **STRING** (binary-safe, same contract as `file.read` / `writeBytes`). Typed slots (`tN`) work as usual. Compile-time blob literals (`^…^hex|bin|oct|b32|b64`) stay in core and do **not** require this module; use `enc.decode` / `enc.encode` for runtime data.

`http.basic` / form encoding continue to use the shared C helper (`flat_enc`) and do **not** need `use native enc`.

Recoverable failures → **PANIC** with reason **`enc.<fn>…`** unless the line has **`?`** / **`~`** ([SPECIFICATION.md](../SPECIFICATION.md) §1.2.2).

## Functions

| Call | Returns | Panic (no modifier) |
|------|---------|---------------------|
| `enc.encode(scheme, data)` | STRING (text for the scheme) | **`enc.encode.args`**, **`enc.encode.scheme`**, **`enc.encode.memory`**, or scheme-specific |
| `enc.decode(scheme, data)` | STRING (raw bytes) | **`enc.decode.args`**, **`enc.decode.scheme`**, **`enc.decode.memory`**, **`enc.decode.percent`**, or lit decode reasons |

`scheme` must be a non-empty STRING without embedded NUL.

### Schemes

| Scheme | Encode | Decode notes |
|--------|--------|--------------|
| `hex` | lowercase hex digits | same alphabet as `^…^hex` |
| `bin` | 8 bits per byte, MSB first | same as `^…^bin` |
| `oct` | 3 octal digits per byte | same as `^…^oct` |
| `b32` | base32hex + `=` padding | same as `^…^b32` |
| `b64` / `base64` | standard Base64 + `=` padding | same as `^…^b64` |
| `base64url` | Base64url (`-`/`_`), no `=` in output | accepts unpadded / `-_` input |
| `urlform` | `application/x-www-form-urlencoded` (`+` for space) | `+` → space; `%HH` |
| `urlcomp` | RFC 3986 component (`%20` for space) | `%HH` only (no `+`) |

### Round-trip

```text
use native enc

#main
  enc.encode('hex', 'ABC') -> h
  h == '414243'
  enc.decode('hex', h) -> raw
  raw == 'ABC'
  enc.encode('base64', 'ABC') -> b
  b == 'QUJD'
  enc.decode('b64', b) == 'ABC'
  io.outln('ok')
  OK
```

### With blob literals

```text
use native enc

#main
  buf = ^414243^hex
  enc.encode('hex', buf) == '414243'
  enc.decode('hex', '4869') == 'Hi'
  OK
```

## Related

- Build toggle: `make … enc=0` → `-DFLAT_WITH_ENC=0` (helper `flat_enc` remains for `http`)
- Blob literals: [LANGUAGE_TYPES.md](../LANGUAGE_TYPES.md)
- Implementation: `src/lib/mod_enc.c`, `src/common/flat_enc.c`
