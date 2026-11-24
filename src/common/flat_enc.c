#include "flat_enc.h"

#include "flat_lit.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char B64URL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
static const char B32HEX[] = "0123456789ABCDEFGHIJKLMNOPQRSTUV";

static void set_why(char *why, size_t why_sz, const char *msg) {
    if (why && why_sz > 0) {
        snprintf(why, why_sz, "%s", msg ? msg : "enc");
    }
}

char *flat_enc_base64_encode(const unsigned char *data, size_t len, size_t *out_len) {
    size_t out_cap, i, o = 0;
    char *out;
    if (out_len) {
        *out_len = 0;
    }
    if (!data && len) {
        return NULL;
    }
    out_cap = 4 * ((len + 2) / 3) + 1;
    out = (char *)malloc(out_cap);
    if (!out) {
        return NULL;
    }
    for (i = 0; i < len; i += 3) {
        unsigned int v = (unsigned int)data[i] << 16;
        int rem = (int)(len - i);
        if (rem > 1) {
            v |= (unsigned int)data[i + 1] << 8;
        }
        if (rem > 2) {
            v |= (unsigned int)data[i + 2];
        }
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = rem > 1 ? B64[(v >> 6) & 63] : '=';
        out[o++] = rem > 2 ? B64[v & 63] : '=';
    }
    out[o] = '\0';
    if (out_len) {
        *out_len = o;
    }
    return out;
}

char *flat_enc_base64url_encode(const unsigned char *data, size_t len, size_t *out_len) {
    size_t out_cap, i, o = 0;
    char *out;
    if (out_len) {
        *out_len = 0;
    }
    if (!data && len) {
        return NULL;
    }
    out_cap = 4 * ((len + 2) / 3) + 1;
    out = (char *)malloc(out_cap);
    if (!out) {
        return NULL;
    }
    for (i = 0; i < len; i += 3) {
        unsigned int v = (unsigned int)data[i] << 16;
        int rem = (int)(len - i);
        if (rem > 1) {
            v |= (unsigned int)data[i + 1] << 8;
        }
        if (rem > 2) {
            v |= (unsigned int)data[i + 2];
        }
        out[o++] = B64URL[(v >> 18) & 63];
        out[o++] = B64URL[(v >> 12) & 63];
        if (rem > 1) {
            out[o++] = B64URL[(v >> 6) & 63];
        }
        if (rem > 2) {
            out[o++] = B64URL[v & 63];
        }
    }
    out[o] = '\0';
    if (out_len) {
        *out_len = o;
    }
    return out;
}

static int ensure_cap(char **buf, size_t *len, size_t *cap, size_t need) {
    char *nbuf;
    size_t ncap;
    if (*len + need + 1 <= *cap) {
        return 0;
    }
    ncap = *cap ? *cap : 64;
    while (*len + need + 1 > ncap) {
        ncap *= 2;
    }
    nbuf = (char *)realloc(*buf, ncap);
    if (!nbuf) {
        return -1;
    }
    *buf = nbuf;
    *cap = ncap;
    return 0;
}

int flat_enc_url_form_append(char **buf, size_t *len, size_t *cap, const char *s, size_t n) {
    size_t i;
    if (!buf || !len || !cap) {
        return -1;
    }
    if (!s && n) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)(s ? s[i] : 0);
        char tmp[4];
        size_t tn;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.' || c == '*') {
            tmp[0] = (char)c;
            tn = 1;
        } else if (c == ' ') {
            tmp[0] = '+';
            tn = 1;
        } else {
            snprintf(tmp, sizeof tmp, "%%%02X", c);
            tn = 3;
        }
        if (ensure_cap(buf, len, cap, tn) != 0) {
            return -1;
        }
        memcpy(*buf + *len, tmp, tn);
        *len += tn;
        (*buf)[*len] = '\0';
    }
    return 0;
}

char *flat_enc_url_form_encode(const char *s, size_t n, size_t *out_len) {
    char *buf = NULL;
    size_t len = 0, cap = 0;
    if (out_len) {
        *out_len = 0;
    }
    if (flat_enc_url_form_append(&buf, &len, &cap, s, n) != 0) {
        free(buf);
        return NULL;
    }
    if (!buf) {
        buf = (char *)malloc(1);
        if (!buf) {
            return NULL;
        }
        buf[0] = '\0';
        len = 0;
    }
    if (out_len) {
        *out_len = len;
    }
    return buf;
}

static int url_comp_append(char **buf, size_t *len, size_t *cap, const char *s, size_t n) {
    size_t i;
    if (!buf || !len || !cap) {
        return -1;
    }
    if (!s && n) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)(s ? s[i] : 0);
        char tmp[4];
        size_t tn;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.' || c == '~') {
            tmp[0] = (char)c;
            tn = 1;
        } else {
            snprintf(tmp, sizeof tmp, "%%%02X", c);
            tn = 3;
        }
        if (ensure_cap(buf, len, cap, tn) != 0) {
            return -1;
        }
        memcpy(*buf + *len, tmp, tn);
        *len += tn;
        (*buf)[*len] = '\0';
    }
    return 0;
}

char *flat_enc_url_comp_encode(const char *s, size_t n, size_t *out_len) {
    char *buf = NULL;
    size_t len = 0, cap = 0;
    if (out_len) {
        *out_len = 0;
    }
    if (url_comp_append(&buf, &len, &cap, s, n) != 0) {
        free(buf);
        return NULL;
    }
    if (!buf) {
        buf = (char *)malloc(1);
        if (!buf) {
            return NULL;
        }
        buf[0] = '\0';
        len = 0;
    }
    if (out_len) {
        *out_len = len;
    }
    return buf;
}

char *flat_enc_hex_encode(const unsigned char *data, size_t len, size_t *out_len) {
    static const char H[] = "0123456789abcdef";
    char *out;
    size_t i;
    if (out_len) {
        *out_len = 0;
    }
    if (!data && len) {
        return NULL;
    }
    out = (char *)malloc(len * 2 + 1);
    if (!out) {
        return NULL;
    }
    for (i = 0; i < len; i++) {
        out[i * 2] = H[data[i] >> 4];
        out[i * 2 + 1] = H[data[i] & 0xf];
    }
    out[len * 2] = '\0';
    if (out_len) {
        *out_len = len * 2;
    }
    return out;
}

char *flat_enc_bin_encode(const unsigned char *data, size_t len, size_t *out_len) {
    char *out;
    size_t i, o = 0;
    int b;
    if (out_len) {
        *out_len = 0;
    }
    if (!data && len) {
        return NULL;
    }
    out = (char *)malloc(len * 8 + 1);
    if (!out) {
        return NULL;
    }
    for (i = 0; i < len; i++) {
        for (b = 7; b >= 0; b--) {
            out[o++] = (data[i] & (1u << b)) ? '1' : '0';
        }
    }
    out[o] = '\0';
    if (out_len) {
        *out_len = o;
    }
    return out;
}

char *flat_enc_oct_encode(const unsigned char *data, size_t len, size_t *out_len) {
    char *out;
    size_t i, o = 0;
    if (out_len) {
        *out_len = 0;
    }
    if (!data && len) {
        return NULL;
    }
    out = (char *)malloc(len * 3 + 1);
    if (!out) {
        return NULL;
    }
    for (i = 0; i < len; i++) {
        unsigned v = data[i];
        out[o++] = (char)('0' + ((v >> 6) & 7));
        out[o++] = (char)('0' + ((v >> 3) & 7));
        out[o++] = (char)('0' + (v & 7));
    }
    out[o] = '\0';
    if (out_len) {
        *out_len = o;
    }
    return out;
}

char *flat_enc_b32_encode(const unsigned char *data, size_t len, size_t *out_len) {
    char *out;
    size_t o = 0;
    size_t i = 0;
    unsigned bitbuf = 0;
    int nbits = 0;
    size_t cap;
    if (out_len) {
        *out_len = 0;
    }
    if (!data && len) {
        return NULL;
    }
    cap = ((len * 8 + 4) / 5) + 8;
    out = (char *)malloc(cap + 1);
    if (!out) {
        return NULL;
    }
    while (i < len) {
        bitbuf = (bitbuf << 8) | data[i++];
        nbits += 8;
        while (nbits >= 5) {
            nbits -= 5;
            out[o++] = B32HEX[(bitbuf >> nbits) & 31u];
        }
    }
    if (nbits > 0) {
        out[o++] = B32HEX[(bitbuf << (5 - nbits)) & 31u];
    }
    while (o % 8u != 0u) {
        out[o++] = '=';
    }
    out[o] = '\0';
    if (out_len) {
        *out_len = o;
    }
    return out;
}

static int decode_via_lit(const char *codec, const char *text, size_t text_len, unsigned char **out,
                          size_t *out_len, char *why, size_t why_sz) {
    char *lit = NULL;
    size_t lit_len;
    const char *p;
    char local[64];
    int rc;

    lit_len = 1 + text_len + 1 + strlen(codec);
    lit = (char *)malloc(lit_len + 1);
    if (!lit) {
        set_why(why, why_sz, "enc.decode.memory");
        return -1;
    }
    lit[0] = '^';
    memcpy(lit + 1, text ? text : "", text_len);
    lit[1 + text_len] = '^';
    memcpy(lit + 2 + text_len, codec, strlen(codec) + 1);
    p = lit;
    rc = flat_lit_parse_blob_at(&p, out, out_len, local, sizeof local);
    free(lit);
    if (rc != 0) {
        set_why(why, why_sz, local[0] ? local : "enc.decode");
        return -1;
    }
    return 0;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static int decode_percent(const char *text, size_t text_len, int form_plus, unsigned char **out,
                          size_t *out_len, char *why, size_t why_sz) {
    unsigned char *buf;
    size_t i = 0, o = 0;
    buf = (unsigned char *)malloc(text_len + 1);
    if (!buf) {
        set_why(why, why_sz, "enc.decode.memory");
        return -1;
    }
    while (i < text_len) {
        char c = text[i];
        if (form_plus && c == '+') {
            buf[o++] = ' ';
            i++;
        } else if (c == '%' && i + 2 < text_len) {
            int hi = hex_nibble(text[i + 1]);
            int lo = hex_nibble(text[i + 2]);
            if (hi < 0 || lo < 0) {
                free(buf);
                set_why(why, why_sz, "enc.decode.percent");
                return -1;
            }
            buf[o++] = (unsigned char)((hi << 4) | lo);
            i += 3;
        } else if (c == '%') {
            free(buf);
            set_why(why, why_sz, "enc.decode.percent");
            return -1;
        } else {
            buf[o++] = (unsigned char)c;
            i++;
        }
    }
    *out = buf;
    *out_len = o;
    return 0;
}

static int decode_base64url(const char *text, size_t text_len, unsigned char **out, size_t *out_len,
                            char *why, size_t why_sz) {
    char *norm;
    size_t i, n = 0;
    int rc;
    norm = (char *)malloc(text_len + 4 + 1);
    if (!norm) {
        set_why(why, why_sz, "enc.decode.memory");
        return -1;
    }
    for (i = 0; i < text_len; i++) {
        char c = text[i];
        if (c == ' ' || c == '\t') {
            continue;
        }
        if (c == '-') {
            c = '+';
        } else if (c == '_') {
            c = '/';
        }
        norm[n++] = c;
    }
    while (n % 4u != 0u) {
        norm[n++] = '=';
    }
    norm[n] = '\0';
    rc = decode_via_lit("b64", norm, n, out, out_len, why, why_sz);
    free(norm);
    return rc;
}

int flat_enc_decode(const char *scheme, const char *text, size_t text_len, unsigned char **out,
                    size_t *out_len, char *why, size_t why_sz) {
    if (why && why_sz > 0) {
        why[0] = '\0';
    }
    if (!scheme || !out || !out_len) {
        set_why(why, why_sz, "enc.decode.scheme");
        return -1;
    }
    *out = NULL;
    *out_len = 0;
    if (!text) {
        text = "";
        text_len = 0;
    }
    if (strcmp(scheme, "hex") == 0) {
        return decode_via_lit("hex", text, text_len, out, out_len, why, why_sz);
    }
    if (strcmp(scheme, "bin") == 0) {
        return decode_via_lit("bin", text, text_len, out, out_len, why, why_sz);
    }
    if (strcmp(scheme, "oct") == 0) {
        return decode_via_lit("oct", text, text_len, out, out_len, why, why_sz);
    }
    if (strcmp(scheme, "b32") == 0) {
        return decode_via_lit("b32", text, text_len, out, out_len, why, why_sz);
    }
    if (strcmp(scheme, "b64") == 0 || strcmp(scheme, "base64") == 0) {
        return decode_via_lit("b64", text, text_len, out, out_len, why, why_sz);
    }
    if (strcmp(scheme, "base64url") == 0) {
        return decode_base64url(text, text_len, out, out_len, why, why_sz);
    }
    if (strcmp(scheme, "urlform") == 0) {
        return decode_percent(text, text_len, 1, out, out_len, why, why_sz);
    }
    if (strcmp(scheme, "urlcomp") == 0) {
        return decode_percent(text, text_len, 0, out, out_len, why, why_sz);
    }
    set_why(why, why_sz, "enc.decode.scheme");
    return -1;
}

int flat_enc_encode(const char *scheme, const unsigned char *data, size_t len, char **out,
                    size_t *out_len, char *why, size_t why_sz) {
    char *s = NULL;
    size_t n = 0;
    if (why && why_sz > 0) {
        why[0] = '\0';
    }
    if (!scheme || !out || !out_len) {
        set_why(why, why_sz, "enc.encode.scheme");
        return -1;
    }
    *out = NULL;
    *out_len = 0;
    if (!data && len) {
        set_why(why, why_sz, "enc.encode.args");
        return -1;
    }
    if (strcmp(scheme, "hex") == 0) {
        s = flat_enc_hex_encode(data, len, &n);
    } else if (strcmp(scheme, "bin") == 0) {
        s = flat_enc_bin_encode(data, len, &n);
    } else if (strcmp(scheme, "oct") == 0) {
        s = flat_enc_oct_encode(data, len, &n);
    } else if (strcmp(scheme, "b32") == 0) {
        s = flat_enc_b32_encode(data, len, &n);
    } else if (strcmp(scheme, "b64") == 0 || strcmp(scheme, "base64") == 0) {
        s = flat_enc_base64_encode(data, len, &n);
    } else if (strcmp(scheme, "base64url") == 0) {
        s = flat_enc_base64url_encode(data, len, &n);
    } else if (strcmp(scheme, "urlform") == 0) {
        s = flat_enc_url_form_encode((const char *)data, len, &n);
    } else if (strcmp(scheme, "urlcomp") == 0) {
        s = flat_enc_url_comp_encode((const char *)data, len, &n);
    } else {
        set_why(why, why_sz, "enc.encode.scheme");
        return -1;
    }
    if (!s) {
        set_why(why, why_sz, "enc.encode.memory");
        return -1;
    }
    *out = s;
    *out_len = n;
    return 0;
}
