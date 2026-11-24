# Bundled hash primitives (WjCryptLib subset)

Public-domain (**Unlicense**) implementations from
[WaterJuice/WjCryptLib](https://github.com/WaterJuice/WjCryptLib):

- `WjCryptLib_Md5.{c,h}`
- `WjCryptLib_Sha1.{c,h}`
- `WjCryptLib_Sha256.{c,h}`
- `WjCryptLib_Sha512.{c,h}`

Used by FlatL optional native `hash` (`hash=1`). SHA-384 is provided by FlatL’s
`flat_hash` wrapper (SHA-512 compress + FIPS SHA-384 IVs, truncated digest).

AES/RC4 modules from upstream are **not** vendored.
