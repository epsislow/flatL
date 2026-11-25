# Native module `bytes`

Binary-safe byte-buffer operations for reading, writing, combining and slicing binary data. The module uses FlatL STRING values as byte buffers; strings may contain `0x00` and are not treated as NUL-terminated text.

Declare before use:

```flat
use native bytes
```

The module is embedded by default. Disable it with `make rebuild bytes=0` (and `bytes=0` for WASM builds).

**Availability:** native ✅ · WASM ✅. Matrix: [README.md § WASM](./README.md#wasm-availability).

## Functions overview

| Function | Signature | Returns | Description |
|---|---|---|---|
| `bytes.read` | `bytes.read(data, offset ; format)` | NUMBER or STRING | Decode a typed value from a byte offset |
| `bytes.write` | `bytes.write(value ; format)` | STRING | Encode a number or fixed-width string into bytes |
| `bytes.concat` | `bytes.concat(a, b)` | STRING | Concatenate two byte buffers |
| `bytes.slice` | `bytes.slice(data, offset, length)` | STRING | Extract a byte range |
| `bytes.len` | `bytes.len(data)` | NUMBER | Return buffer length in bytes |

## Binary formats

The format is supplied in the native tag section after `;`:

```flat
bytes.read(data, 4; 'u16 be') -> version
bytes.write(12345; 'u32') -> encoded
```

Supported numeric formats:

| Format | Size | Meaning |
|---|---:|---|
| `u8`, `u16`, `u32`, `u64` | 1, 2, 4, 8 bytes | Unsigned integer |
| `s8`, `s16`, `s32`, `s64` | 1, 2, 4, 8 bytes | Signed two's-complement integer |
| `f32`, `f64` | 4, 8 bytes | IEEE-754 floating point |
| `tX` | X bytes | Fixed-width UTF-8 string |

For numeric formats, endianness may be `le` or `be`. It defaults to `le`. Endianness is not applicable to `u8`, `s8`, or `tX`.

`tX` means exactly X bytes, not X Unicode characters. No NUL terminator or padding is added. `bytes.write` requires the input string to contain exactly X bytes; `bytes.read` requires X bytes to be available. The string bytes are preserved as-is; UTF-8 validity is not checked.

## `bytes.read(data, offset ; format)`

Reads one value starting at a zero-based byte offset. Numeric values are returned using the corresponding FlatL numeric storage type, preserving full-width `u64` and `s64` values. `tX` returns a STRING containing exactly X bytes.

```flat
use native bytes

#main
  raw = bytes.write(4660; 'u16 be')
  value = bytes.read(raw, 0; 'u16 be')
  value == 4660
  OK
```

The function panics with `bytes.read.args` for invalid arguments, `bytes.read.format` for an invalid format, `bytes.read.data` for a non-string buffer, `bytes.read.offset` for an invalid offset, and `bytes.read.range` when the requested bytes are not available.

## `bytes.write(value ; format)`

Encodes one value into a new byte buffer. Integer values are range-checked; values are never silently truncated. Signed integers use two's-complement representation. Floating-point values use IEEE-754 binary32/binary64 representation. Finite decimal values are rounded to the selected representation; NaN and infinity are rejected.

```flat
use native bytes

#main
  header = bytes.write(12345; 'u32 be')
  temperature = bytes.write(21.5; 'f32 le')
  name = bytes.write('FlatL'; 't5')
  OK
```

The function panics with `bytes.write.args` for invalid arguments/formats, `bytes.write.range` for an out-of-range or incompatible numeric value, and `bytes.write.length` when a `tX` string does not contain exactly X bytes.

## `bytes.concat(a, b)`

Returns a new buffer containing all bytes from `a`, followed by all bytes from `b`.

```flat
use native bytes

#main
  magic = bytes.write('FLAT'; 't4')
  version = bytes.write(1; 'u16 le')
  header = bytes.concat(magic, version)
  bytes.len(header) == 6
  OK
```

Invalid arguments or a result larger than the supported 16 MiB buffer limit panic with `bytes.concat.args` or `bytes.size`.

## `bytes.slice(data, offset, length)`

Returns a new buffer containing `length` bytes starting at the zero-based `offset`.

```flat
use native bytes

#main
  data = bytes.write('FLATDATA'; 't8')
  magic = bytes.slice(data, 0, 4)
  magic == 'FLAT'
  OK
```

Negative, non-integer, or out-of-range offsets/lengths panic with `bytes.slice.range`.

## `bytes.len(data)`

Returns the number of bytes, not the number of UTF-8 characters.

```flat
use native bytes

#main
  data = bytes.write('FlatL'; 't5')
  bytes.len(data) == 5
  OK
```

## Complete example: write a binary file and read it back

The following example writes a small binary header and payload, then opens the file again and decodes its fields. `file.writeBytes` is used so the buffer is written without text formatting.

```flat
use native bytes

#main
  magic = bytes.write('FLAT'; 't4')
  version = bytes.write(1; 'u16 le')
  payload_size = bytes.write(3; 'u32 be')
  payload = bytes.write('ABC'; 't3')
  first = bytes.concat(magic, version)
  second = bytes.concat(payload_size, payload)
  file_data = bytes.concat(first, second)

  fp = file.createCtx('sample.bin')
  file.writeBytes(fp, file_data)
  file.close(fp)

  fp = file.readCtx('sample.bin')
  loaded = file.read(fp)
  file.close(fp)

  read_magic = bytes.read(loaded, 0; 't4')
  read_version = bytes.read(loaded, 4; 'u16 le')
  read_size = bytes.read(loaded, 6; 'u32 be')
  read_payload = bytes.slice(loaded, 10, read_size)

  read_magic == 'FLAT'
  read_version == 1
  read_payload == 'ABC'
  io.outln('binary file roundtrip ok')
  OK
```

## Safety and limits

- All offsets and lengths are zero-based byte counts and must be non-negative integers.
- Reads never return partial values: insufficient input is an error.
- Integer writes reject overflow and incompatible numeric values.
- Fixed-width strings must match the requested byte length exactly.
- The maximum output buffer size for a single operation is 16 MiB.
- `bytes.read` / `bytes.write` accept one format tag. For numeric types, at most one endianness token is allowed.
