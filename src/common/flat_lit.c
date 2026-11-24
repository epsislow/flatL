#include "flat_lit.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define FLAT_LIT_T_MAX 8192u

static void set_why(char *why, size_t why_sz, const char *msg) {
    if (why && why_sz > 0) {
        snprintf(why, why_sz, "%s", msg ? msg : "lit.hex");
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

/* Parse optional :t or :tN after codec. Returns 0 and sets *cap (0 = none). */
static int parse_optional_t_cap(const char **p, unsigned *cap, char *why, size_t why_sz) {
    const char *s = *p;
    unsigned long n;
    char *end = NULL;

    *cap = 0;
    if (*s != ':') {
        return 0;
    }
    s++;
    if (*s != 't') {
        set_why(why, why_sz, "lit.hex.type");
        return -1;
    }
    s++;
    if (*s == '\0' || (!(isalnum((unsigned char)*s) || *s == '_'))) {
        /* bare :t → t127 */
        *cap = 127u;
        *p = s;
        return 0;
    }
    if (!isdigit((unsigned char)*s)) {
        set_why(why, why_sz, "lit.hex.type");
        return -1;
    }
    n = strtoul(s, &end, 10);
    if (end == s || n == 0 || n > FLAT_LIT_T_MAX) {
        set_why(why, why_sz, "lit.hex.type");
        return -1;
    }
    *cap = (unsigned)n;
    *p = end;
    return 0;
}

int flat_lit_parse_blob_at(const char **p, unsigned char **out, size_t *out_len, char *why,
                           size_t why_sz) {
    const char *s;
    const char *payload;
    const char *close;
    const char *codec;
    size_t dig_count = 0;
    size_t i;
    size_t o = 0;
    unsigned char *buf = NULL;
    unsigned cap = 0;
    int hi = -1;

    if (why && why_sz > 0) {
        why[0] = '\0';
    }
    if (!p || !*p || !out || !out_len) {
        set_why(why, why_sz, "lit.hex");
        return -1;
    }
    *out = NULL;
    *out_len = 0;
    s = *p;
    if (*s != '^') {
        set_why(why, why_sz, "lit.hex");
        return -1;
    }
    s++;
    payload = s;
    close = strchr(s, '^');
    if (!close) {
        set_why(why, why_sz, "lit.hex.unclosed");
        return -1;
    }
    /* Count hex digits (space/tab ignored; other chars invalid) */
    for (i = 0; payload + i < close; i++) {
        char c = payload[i];
        if (c == ' ' || c == '\t') {
            continue;
        }
        if (c == '\n' || c == '\r') {
            set_why(why, why_sz, "lit.hex.newline");
            return -1;
        }
        if (hex_nibble(c) < 0) {
            set_why(why, why_sz, "lit.hex.digit");
            return -1;
        }
        dig_count++;
    }
    if (dig_count % 2u != 0u) {
        set_why(why, why_sz, "lit.hex.odd");
        return -1;
    }
    codec = close + 1;
    if (strncmp(codec, "hex", 3) == 0 &&
        (codec[3] == '\0' || codec[3] == ':' || codec[3] == ' ' || codec[3] == '\t')) {
        s = codec + 3;
    } else if (strncmp(codec, "bin", 3) == 0 || strncmp(codec, "oct", 3) == 0) {
        set_why(why, why_sz, "lit.codec.later");
        return -1;
    } else {
        set_why(why, why_sz, "lit.codec.unknown");
        return -1;
    }
    if (parse_optional_t_cap(&s, &cap, why, why_sz) != 0) {
        return -1;
    }
    s = skip_ws_space_tab(s);

    if (dig_count / 2u > 0) {
        buf = (unsigned char *)malloc(dig_count / 2u);
        if (!buf) {
            set_why(why, why_sz, "lit.hex.memory");
            return -1;
        }
        for (i = 0; payload + i < close; i++) {
            char c = payload[i];
            int nib;
            if (c == ' ' || c == '\t') {
                continue;
            }
            nib = hex_nibble(c);
            if (hi < 0) {
                hi = nib;
            } else {
                buf[o++] = (unsigned char)((hi << 4) | nib);
                hi = -1;
            }
        }
    }
    if (cap > 0 && o > (size_t)cap) {
        free(buf);
        set_why(why, why_sz, "lit.hex.len");
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
        set_why(why, why_sz, "lit.hex");
        return -1;
    }
    n = strlen(rhs);
    buf = (char *)malloc(n + 1);
    if (!buf) {
        set_why(why, why_sz, "lit.hex.memory");
        return -1;
    }
    memcpy(buf, rhs, n + 1);
    /* trim */
    {
        char *s = buf;
        char *e;
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (*s == '\0') {
            free(buf);
            set_why(why, why_sz, "lit.hex");
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
        set_why(why, why_sz, "lit.hex");
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
        set_why(why, why_sz, "lit.hex.trailing");
        free(buf);
        return -1;
    }
    free(buf);
    if (why && why_sz > 0) {
        why[0] = '\0';
    }
    return 0;
}
