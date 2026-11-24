#include "flat_lit.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLAT_LIT_T_MAX 8192u

typedef enum { LIT_HEX = 0, LIT_BIN = 1, LIT_OCT = 2, LIT_B32 = 3, LIT_B64 = 4 } LitCodec;

static void set_why(char *why, size_t why_sz, const char *msg) {
    if (why && why_sz > 0) {
        snprintf(why, why_sz, "%s", msg ? msg : "lit");
    }
}

static const char *codec_prefix(LitCodec c) {
    switch (c) {
    case LIT_HEX:
        return "lit.hex";
    case LIT_BIN:
        return "lit.bin";
    case LIT_OCT:
        return "lit.oct";
    case LIT_B32:
        return "lit.b32";
    case LIT_B64:
        return "lit.b64";
    default:
        return "lit";
    }
}

static void why_codec(char *why, size_t why_sz, LitCodec c, const char *suffix) {
    if (why && why_sz > 0) {
        if (suffix && suffix[0]) {
            snprintf(why, why_sz, "%s.%s", codec_prefix(c), suffix);
        } else {
            snprintf(why, why_sz, "%s", codec_prefix(c));
        }
    }
}

static const char *skip_ws_space_tab(const char *p) {
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
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

/* RFC 4648 base32hex alphabet: 0-9 A-V (case-insensitive). */
static int b32_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'V') {
        return 10 + (c - 'A');
    }
    if (c >= 'a' && c <= 'v') {
        return 10 + (c - 'a');
    }
    return -1;
}

/* RFC 4648 base64 alphabet: A-Za-z0-9+/ (case-sensitive). */
static int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return 26 + (c - 'a');
    }
    if (c >= '0' && c <= '9') {
        return 52 + (c - '0');
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

static int parse_optional_t_cap(const char **p, unsigned *cap, LitCodec codec, char *why,
                                size_t why_sz) {
    const char *s = *p;
    unsigned long n;
    char *end = NULL;

    *cap = 0;
    if (*s != ':') {
        return 0;
    }
    s++;
    if (*s != 't') {
        why_codec(why, why_sz, codec, "type");
        return -1;
    }
    s++;
    if (*s == '\0' || (!(isalnum((unsigned char)*s) || *s == '_'))) {
        *cap = 127u;
        *p = s;
        return 0;
    }
    if (!isdigit((unsigned char)*s)) {
        why_codec(why, why_sz, codec, "type");
        return -1;
    }
    n = strtoul(s, &end, 10);
    if (end == s || n == 0 || n > FLAT_LIT_T_MAX) {
        why_codec(why, why_sz, codec, "type");
        return -1;
    }
    *cap = (unsigned)n;
    *p = end;
    return 0;
}

static int codec_word(const char *codec, const char *name, LitCodec id, LitCodec *out,
                      const char **after) {
    size_t n = strlen(name);
    if (strncmp(codec, name, n) != 0) {
        return -1;
    }
    if (codec[n] != '\0' && codec[n] != ':' && codec[n] != ' ' && codec[n] != '\t') {
        return -1;
    }
    *out = id;
    *after = codec + n;
    return 0;
}

static int codec_at(const char *codec, LitCodec *out, const char **after) {
    if (codec_word(codec, "hex", LIT_HEX, out, after) == 0) {
        return 0;
    }
    if (codec_word(codec, "bin", LIT_BIN, out, after) == 0) {
        return 0;
    }
    if (codec_word(codec, "oct", LIT_OCT, out, after) == 0) {
        return 0;
    }
    if (codec_word(codec, "b32", LIT_B32, out, after) == 0) {
        return 0;
    }
    if (codec_word(codec, "b64", LIT_B64, out, after) == 0) {
        return 0;
    }
    return -1;
}

static int collect_digits(const char *payload, const char *close, LitCodec codec, char *digits,
                          size_t dig_max, size_t *dig_count, char *why, size_t why_sz) {
    size_t i;
    size_t n = 0;
    int seen_pad = 0;

    for (i = 0; payload + i < close; i++) {
        char c = payload[i];
        if (c == ' ' || c == '\t') {
            continue;
        }
        if (c == '\n' || c == '\r') {
            why_codec(why, why_sz, codec, "newline");
            return -1;
        }
        if ((codec == LIT_B32 || codec == LIT_B64) && c == '=') {
            seen_pad = 1;
            continue; /* RFC padding; ignored for bit decode */
        }
        if (seen_pad) {
            why_codec(why, why_sz, codec, "pad");
            return -1;
        }
        if (codec == LIT_HEX) {
            if (hex_nibble(c) < 0) {
                why_codec(why, why_sz, codec, "digit");
                return -1;
            }
        } else if (codec == LIT_BIN) {
            if (c != '0' && c != '1') {
                why_codec(why, why_sz, codec, "digit");
                return -1;
            }
        } else if (codec == LIT_OCT) {
            if (c < '0' || c > '7') {
                why_codec(why, why_sz, codec, "digit");
                return -1;
            }
        } else if (codec == LIT_B32) {
            if (b32_value(c) < 0) {
                why_codec(why, why_sz, codec, "digit");
                return -1;
            }
        } else { /* B64 */
            if (b64_value(c) < 0) {
                why_codec(why, why_sz, codec, "digit");
                return -1;
            }
        }
        if (n + 1 >= dig_max) {
            why_codec(why, why_sz, codec, "memory");
            return -1;
        }
        digits[n++] = c;
    }
    *dig_count = n;
    return 0;
}

static int decode_hex(const char *digits, size_t dig_count, unsigned char *buf, size_t *out_len,
                      char *why, size_t why_sz) {
    size_t i;
    size_t o = 0;
    if (dig_count % 2u != 0u) {
        why_codec(why, why_sz, LIT_HEX, "odd");
        return -1;
    }
    for (i = 0; i + 1 < dig_count; i += 2) {
        buf[o++] = (unsigned char)((hex_nibble(digits[i]) << 4) | hex_nibble(digits[i + 1]));
    }
    *out_len = o;
    return 0;
}

static int decode_bin(const char *digits, size_t dig_count, unsigned char *buf, size_t *out_len,
                      char *why, size_t why_sz) {
    size_t i;
    size_t o = 0;
    if (dig_count % 8u != 0u) {
        why_codec(why, why_sz, LIT_BIN, "align");
        return -1;
    }
    for (i = 0; i < dig_count; i += 8) {
        unsigned char b = 0;
        int k;
        for (k = 0; k < 8; k++) {
            b = (unsigned char)((b << 1) | (digits[i + (size_t)k] - '0'));
        }
        buf[o++] = b;
    }
    *out_len = o;
    return 0;
}

static int decode_oct(const char *digits, size_t dig_count, unsigned char *buf, size_t *out_len,
                      char *why, size_t why_sz) {
    size_t i;
    size_t o = 0;
    if (dig_count % 3u != 0u) {
        why_codec(why, why_sz, LIT_OCT, "align");
        return -1;
    }
    for (i = 0; i < dig_count; i += 3) {
        unsigned v = (unsigned)(digits[i] - '0') * 64u + (unsigned)(digits[i + 1] - '0') * 8u +
                     (unsigned)(digits[i + 2] - '0');
        if (v > 255u) {
            why_codec(why, why_sz, LIT_OCT, "range");
            return -1;
        }
        buf[o++] = (unsigned char)v;
    }
    *out_len = o;
    return 0;
}

static int decode_b32(const char *digits, size_t dig_count, unsigned char *buf, size_t *out_len,
                      char *why, size_t why_sz) {
    size_t i;
    size_t o = 0;
    unsigned bitbuf = 0;
    int nbits = 0;

    for (i = 0; i < dig_count; i++) {
        int v = b32_value(digits[i]);
        if (v < 0) {
            why_codec(why, why_sz, LIT_B32, "digit");
            return -1;
        }
        bitbuf = (bitbuf << 5) | (unsigned)v;
        nbits += 5;
        while (nbits >= 8) {
            nbits -= 8;
            buf[o++] = (unsigned char)((bitbuf >> nbits) & 0xffu);
        }
    }
    if (nbits > 0) {
        unsigned mask = (1u << (unsigned)nbits) - 1u;
        if ((bitbuf & mask) != 0u) {
            why_codec(why, why_sz, LIT_B32, "align");
            return -1;
        }
    }
    *out_len = o;
    return 0;
}

static int decode_b64(const char *digits, size_t dig_count, unsigned char *buf, size_t *out_len,
                      char *why, size_t why_sz) {
    size_t i;
    size_t o = 0;
    unsigned bitbuf = 0;
    int nbits = 0;

    for (i = 0; i < dig_count; i++) {
        int v = b64_value(digits[i]);
        if (v < 0) {
            why_codec(why, why_sz, LIT_B64, "digit");
            return -1;
        }
        bitbuf = (bitbuf << 6) | (unsigned)v;
        nbits += 6;
        while (nbits >= 8) {
            nbits -= 8;
            buf[o++] = (unsigned char)((bitbuf >> nbits) & 0xffu);
        }
    }
    if (nbits > 0) {
        unsigned mask = (1u << (unsigned)nbits) - 1u;
        if ((bitbuf & mask) != 0u) {
            why_codec(why, why_sz, LIT_B64, "align");
            return -1;
        }
    }
    *out_len = o;
    return 0;
}

int flat_lit_parse_blob_at(const char **p, unsigned char **out, size_t *out_len, char *why,
                           size_t why_sz) {
    const char *s;
    const char *payload;
    const char *close;
    const char *after_codec;
    LitCodec codec;
    size_t dig_count = 0;
    size_t byte_cap = 0;
    size_t o = 0;
    unsigned char *buf = NULL;
    char *digits = NULL;
    size_t payload_len;
    unsigned cap = 0;

    if (why && why_sz > 0) {
        why[0] = '\0';
    }
    if (!p || !*p || !out || !out_len) {
        set_why(why, why_sz, "lit");
        return -1;
    }
    *out = NULL;
    *out_len = 0;
    s = *p;
    if (*s != '^') {
        set_why(why, why_sz, "lit");
        return -1;
    }
    s++;
    payload = s;
    close = strchr(s, '^');
    if (!close) {
        set_why(why, why_sz, "lit.unclosed");
        return -1;
    }
    if (codec_at(close + 1, &codec, &after_codec) != 0) {
        set_why(why, why_sz, "lit.codec.unknown");
        return -1;
    }
    s = after_codec;
    if (parse_optional_t_cap(&s, &cap, codec, why, why_sz) != 0) {
        return -1;
    }
    s = skip_ws_space_tab(s);

    payload_len = (size_t)(close - payload);
    digits = (char *)malloc(payload_len + 1);
    if (!digits) {
        why_codec(why, why_sz, codec, "memory");
        return -1;
    }
    if (collect_digits(payload, close, codec, digits, payload_len + 1, &dig_count, why, why_sz) !=
        0) {
        free(digits);
        return -1;
    }

    if (codec == LIT_HEX) {
        byte_cap = dig_count / 2u;
    } else if (codec == LIT_BIN) {
        byte_cap = dig_count / 8u;
    } else if (codec == LIT_OCT) {
        byte_cap = dig_count / 3u;
    } else if (codec == LIT_B32) {
        byte_cap = (dig_count * 5u) / 8u;
    } else {
        byte_cap = (dig_count * 6u) / 8u;
    }

    if (byte_cap > 0) {
        buf = (unsigned char *)malloc(byte_cap);
        if (!buf) {
            free(digits);
            why_codec(why, why_sz, codec, "memory");
            return -1;
        }
    }

    if (codec == LIT_HEX) {
        if (decode_hex(digits, dig_count, buf, &o, why, why_sz) != 0) {
            free(digits);
            free(buf);
            return -1;
        }
    } else if (codec == LIT_BIN) {
        if (decode_bin(digits, dig_count, buf, &o, why, why_sz) != 0) {
            free(digits);
            free(buf);
            return -1;
        }
    } else if (codec == LIT_OCT) {
        if (decode_oct(digits, dig_count, buf, &o, why, why_sz) != 0) {
            free(digits);
            free(buf);
            return -1;
        }
    } else if (codec == LIT_B32) {
        if (decode_b32(digits, dig_count, buf, &o, why, why_sz) != 0) {
            free(digits);
            free(buf);
            return -1;
        }
    } else {
        if (decode_b64(digits, dig_count, buf, &o, why, why_sz) != 0) {
            free(digits);
            free(buf);
            return -1;
        }
    }
    free(digits);

    if (cap > 0 && o > (size_t)cap) {
        free(buf);
        why_codec(why, why_sz, codec, "len");
        return -1;
    }

    *out = buf;
    *out_len = o;
    *p = s;
    return 0;
}

int flat_lit_validate_blob_rhs(const char *rhs, char *why, size_t why_sz) {
    const char *p;
    unsigned char *bytes = NULL;
    size_t len = 0;
    char local[64];
    char *buf = NULL;
    size_t n;
    int rc;

    if (!rhs) {
        set_why(why, why_sz, "lit");
        return -1;
    }
    n = strlen(rhs);
    buf = (char *)malloc(n + 1);
    if (!buf) {
        set_why(why, why_sz, "lit.memory");
        return -1;
    }
    memcpy(buf, rhs, n + 1);
    {
        char *s = buf;
        char *e;
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (*s == '\0') {
            free(buf);
            set_why(why, why_sz, "lit");
            return -1;
        }
        e = s + strlen(s) - 1;
        while (e > s && (*e == ' ' || *e == '\t')) {
            *e-- = '\0';
        }
        p = s;
    }
    if (*p != '^') {
        free(buf);
        set_why(why, why_sz, "lit");
        return -1;
    }
    rc = flat_lit_parse_blob_at(&p, &bytes, &len, local, sizeof local);
    free(bytes);
    if (rc != 0) {
        set_why(why, why_sz, local);
        free(buf);
        return -1;
    }
    p = skip_ws_space_tab(p);
    if (*p != '\0') {
        set_why(why, why_sz, "lit.trailing");
        free(buf);
        return -1;
    }
    free(buf);
    if (why && why_sz > 0) {
        why[0] = '\0';
    }
    return 0;
}
