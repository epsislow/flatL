#ifndef FLAT_HASH_H
#define FLAT_HASH_H

#include <stddef.h>

#define FLAT_HASH_DIGEST_MAX 64u /* sha512 */
#define FLAT_HASH_HEX_MAX (FLAT_HASH_DIGEST_MAX * 2u)

/*
 * Digest by algo name: md5|sha1|sha256|sha384|sha512.
 * On success: *out_len = digest bytes; out holds raw digest (caller provides
 * FLAT_HASH_DIGEST_MAX). Returns 0.
 * On failure: sets why (hash.digest.*) and returns -1.
 */
int flat_hash_digest(const char *algo, const unsigned char *data, size_t len,
                     unsigned char *out, size_t *out_len, char *why, size_t why_sz);

/* Hex-encode digest (lowercase). out needs 2*digest_len+1. */
void flat_hash_hex(const unsigned char *digest, size_t digest_len, char *out);

#endif
