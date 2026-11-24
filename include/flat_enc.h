#ifndef FLAT_ENC_H
#define FLAT_ENC_H

#include <stddef.h>

/* Shared encode/decode helpers (http.basic / http.form / native enc). */

/* Allocates NUL-terminated Base64; caller frees. Returns NULL on OOM. */
char *flat_enc_base64_encode(const unsigned char *data, size_t len, size_t *out_len);

/* Base64url (RFC 4648 §5): -_ alphabet; no '=' padding in output. */
char *flat_enc_base64url_encode(const unsigned char *data, size_t len, size_t *out_len);

/*
 * Percent-encode for application/x-www-form-urlencoded.
 * Appends into *buf / *len / *cap (realloc). Returns 0 on success, -1 on OOM.
 */
int flat_enc_url_form_append(char **buf, size_t *len, size_t *cap, const char *s, size_t n);

/* Allocates percent-encoded form body; caller frees. */
char *flat_enc_url_form_encode(const char *s, size_t n, size_t *out_len);

/* RFC 3986 component encode (no '+' for space — uses %20). */
char *flat_enc_url_comp_encode(const char *s, size_t n, size_t *out_len);

char *flat_enc_hex_encode(const unsigned char *data, size_t len, size_t *out_len);
char *flat_enc_bin_encode(const unsigned char *data, size_t len, size_t *out_len);
char *flat_enc_oct_encode(const unsigned char *data, size_t len, size_t *out_len);
char *flat_enc_b32_encode(const unsigned char *data, size_t len, size_t *out_len);

/*
 * Decode by scheme name: hex|bin|oct|b32|b64|base64|base64url|urlform|urlcomp.
 * On success: *out malloc'd (may be NULL if *out_len==0); caller frees.
 * On failure: sets why (enc.decode.*) and returns -1.
 */
int flat_enc_decode(const char *scheme, const char *text, size_t text_len, unsigned char **out,
                    size_t *out_len, char *why, size_t why_sz);

/*
 * Encode by scheme name. On success: *out malloc'd C string; caller frees.
 * binary schemes take raw bytes; url* take text bytes.
 */
int flat_enc_encode(const char *scheme, const unsigned char *data, size_t len, char **out,
                    size_t *out_len, char *why, size_t why_sz);

#endif
