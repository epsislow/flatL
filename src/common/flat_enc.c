#include "flat_enc.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *flat_enc_base64_encode(const unsigned char *data, size_t len, size_t *out_len) {
    size_t out_cap, i, o = 0;
    char *out;
    if (out_len) *out_len = 0;
    if (!data && len) return NULL;
    out_cap = 4 * ((len + 2) / 3) + 1;
    out = (char *)malloc(out_cap);
    if (!out) return NULL;
    for (i = 0; i < len; i += 3) {
        unsigned int v = (unsigned int)data[i] << 16;
        int rem = (int)(len - i);
        if (rem > 1) v |= (unsigned int)data[i + 1] << 8;
        if (rem > 2) v |= (unsigned int)data[i + 2];
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = rem > 1 ? B64[(v >> 6) & 63] : '=';
        out[o++] = rem > 2 ? B64[v & 63] : '=';
    }
    out[o] = '\0';
    if (out_len) *out_len = o;
    return out;
}

static int ensure_cap(char **buf, size_t *len, size_t *cap, size_t need) {
    char *nbuf;
    size_t ncap;
    if (*len + need + 1 <= *cap) return 0;
    ncap = *cap ? *cap : 64;
    while (*len + need + 1 > ncap) ncap *= 2;
    nbuf = (char *)realloc(*buf, ncap);
    if (!nbuf) return -1;
    *buf = nbuf;
    *cap = ncap;
    return 0;
}

int flat_enc_url_form_append(char **buf, size_t *len, size_t *cap, const char *s, size_t n) {
    size_t i;
    if (!buf || !len || !cap) return -1;
    if (!s && n) return -1;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)(s ? s[i] : 0);
        char tmp[4];
        size_t tn;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '*') {
            tmp[0] = (char)c;
            tn = 1;
        } else if (c == ' ') {
            tmp[0] = '+';
            tn = 1;
        } else {
            snprintf(tmp, sizeof tmp, "%%%02X", c);
            tn = 3;
        }
        if (ensure_cap(buf, len, cap, tn) != 0) return -1;
        memcpy(*buf + *len, tmp, tn);
        *len += tn;
        (*buf)[*len] = '\0';
    }
    return 0;
}
