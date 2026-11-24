#ifndef FLAT_LIT_H
#define FLAT_LIT_H

#include <stddef.h>

/*
 * Binary blob literals: ^payload^hex[:tN]
 * Phase 1: hex only. Spaces/tabs in payload ignored. No escapes. No newlines.
 */

/* Decode one blob literal starting at *p (must point at '^').
 * Advances *p past the literal (including optional :tN).
 * On success: *out is malloc'd bytes (caller frees); may be NULL if *out_len==0.
 * On failure: sets why (e.g. lit.hex.odd) and returns -1; *out unchanged.
 */
int flat_lit_parse_blob_at(const char **p, unsigned char **out, size_t *out_len, char *why,
                           size_t why_sz);

/* Validate that `rhs` (trimmed) is exactly one blob literal. */
int flat_lit_validate_blob_rhs(const char *rhs, char *why, size_t why_sz);

#endif
