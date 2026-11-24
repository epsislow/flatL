#ifndef FLAT_LIT_H
#define FLAT_LIT_H

#include <stddef.h>

/*
 * Binary blob literals: ^payload^(hex|bin|oct|b32|b64)[:tN]
 * Spaces/tabs in payload ignored. No escapes. No newlines.
 *  - hex: even nibble count
 *  - bin: bit count multiple of 8
 *  - oct: 3 digits per byte (000–377)
 *  - b32: RFC 4648 base32hex (0-9A-V → 5 bits); optional trailing '=' pad
 *  - b64: RFC 4648 base64 (A-Za-z0-9+/ → 6 bits); optional trailing '=' pad
 */

/* Decode one blob literal starting at *p (must point at '^').
 * Advances *p past the literal (including optional :tN).
 * On success: *out is malloc'd bytes (caller frees); may be NULL if *out_len==0.
 * On failure: sets why (e.g. lit.hex.odd / lit.bin.align) and returns -1.
 */
int flat_lit_parse_blob_at(const char **p, unsigned char **out, size_t *out_len, char *why,
                           size_t why_sz);

/* Validate that `rhs` (trimmed) is exactly one blob literal. */
int flat_lit_validate_blob_rhs(const char *rhs, char *why, size_t why_sz);

#endif
