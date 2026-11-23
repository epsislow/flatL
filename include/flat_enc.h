#ifndef FLAT_ENC_H
#define FLAT_ENC_H

#include <stddef.h>

/* Shared encode helpers (used by http.basic / http.form; future enc module). */

/* Allocates NUL-terminated Base64; caller frees. Returns NULL on OOM. */
char *flat_enc_base64_encode(const unsigned char *data, size_t len, size_t *out_len);

/*
 * Percent-encode for application/x-www-form-urlencoded.
 * Appends into *buf / *len / *cap (realloc). Returns 0 on success, -1 on OOM.
 */
int flat_enc_url_form_append(char **buf, size_t *len, size_t *cap, const char *s, size_t n);

#endif
