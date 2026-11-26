# Native module `hash`

Digests and non-cryptographic random bytes. **Optional** — declare before use:

```text
use native hash
```

**Not** a full crypto suite: no TLS, AES, HMAC API, or password KDF in H1. MD5 / SHA-1 are for **legacy / checksum** compatibility only.

Input data is **STRING** (binary-safe, same contract as `enc` / `file.read`). Digest output is a **lowercase hex STRING**.

Build toggle: `make … hash=0` → `-DFLAT_WITH_HASH=0`. Primitives live in `external/hash/` (WjCryptLib public-domain subset); SHA-384 via FlatL wrapper on SHA-512.

**Availability:** native ✅ · WASM ✅ (same C in `external/hash/`). Matrix: [README.md § WASM](./README.md#wasm-availability).

Recoverable failures → **PANIC** **`hash.<fn>…`** unless **`?`** / **`~`**.

## Functions

| Call | Returns | Panic (no modifier) |
|------|---------|---------------------|
| `hash.digest(algo, data)` | STRING (hex lowercase) | **`hash.digest.args`**, **`hash.digest.algo`**, **`hash.memory`** |
| `hash.randomBytes(n)` | STRING (raw `n` bytes) | **`hash.randomBytes.args`** if `n` not integer in **1…8192**; **`hash.memory`** |

### Algorithms (`algo`)

| Name | Digest hex length |
|------|-------------------|
| `md5` | 32 |
| `sha1` | 40 |
| `sha256` | 64 |
| `sha384` | 96 |
| `sha512` | 128 |

No per-algo aliases (`hash.md5` etc.) in H1 — use `digest`.

### Examples

```text
use native hash

#main
  hash.digest('sha256', 'abc') -> h
  h == 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'
  hash.randomBytes(16) -> b
  str.len(b) == 16
  io.outln('ok')
  OK
```

`hash.randomBytes` uses the VM-shared **`flat_rng`** (same per-VM RNG as `math.random` and `math.seed`). It does **not** require `use native math`. Not suitable as a CSPRNG.

## Related

- Implementation: `src/lib/mod_hash.c`, `src/common/flat_hash.c`, `external/hash/`
- RNG: `include/flat_rng.h`
