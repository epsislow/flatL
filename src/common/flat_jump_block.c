#include "flat_jump_block.h"
#include "flat_number.h"
#include "flat_str.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *skip_ws(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static int parse_uint_literal(const char *p, uint32_t *out, char *err, size_t err_sz) {
    char *end = NULL;
    unsigned long v;

    if (!p || !*p) {
        snprintf(err, err_sz, "jump:blockMax: expected integer");
        return -1;
    }
    if (flat_number_is_negative_literal_token(p)) {
        snprintf(err, err_sz, "jump.blockMax.invalid");
        return -1;
    }
    v = strtoul(p, &end, 10);
    if (!end || end == p || *skip_ws(end) != '\0') {
        snprintf(err, err_sz, "jump:blockMax: expected integer literal");
        return -1;
    }
    if (v > UINT32_MAX) {
        snprintf(err, err_sz, "jump:blockMax: value out of range");
        return -1;
    }
    *out = (uint32_t)v;
    return 0;
}

int flat_parse_jump_block_max_directive(const char *line, uint32_t *out_n, char *err, size_t err_sz) {
    const char *p;
    const char *rest;
    size_t kw = strlen(FLAT_JUMP_BLOCK_MAX_KW);

    if (!line || !out_n) {
        return 0;
    }
    p = skip_ws(line);
    if (strncmp(p, FLAT_JUMP_BLOCK_MAX_KW, kw) != 0 ||
        (p[kw] != '\0' && !isspace((unsigned char)p[kw]))) {
        return 0;
    }
    rest = skip_ws(p + kw);
    if (*rest == '\0') {
        snprintf(err, err_sz, "jump:blockMax: expected integer");
        return -1;
    }
    if (parse_uint_literal(rest, out_n, err, err_sz) != 0) {
        snprintf(err, err_sz, "jump.blockMax.invalid");
        return -1;
    }
    return 1;
}

int flat_block_header_strip_jump_block_max(char *line, int *out_override_set, uint32_t *out_override,
                                           char *err, size_t err_sz) {
    char *p;
    char *q;
    size_t kw = strlen(FLAT_JUMP_BLOCK_MAX_KW);

    if (out_override_set) {
        *out_override_set = 0;
    }
    if (!line) {
        return 0;
    }
    p = line;
    while ((q = strstr(p, FLAT_JUMP_BLOCK_MAX_KW)) != NULL) {
        const char *rest;
        uint32_t n;
        char ebuf[128];

        if (q != p && !isspace((unsigned char)q[-1])) {
            p = q + kw;
            continue;
        }
        rest = skip_ws(q + kw);
        ebuf[0] = '\0';
        if (parse_uint_literal(rest, &n, ebuf, sizeof ebuf) != 0) {
            snprintf(err, err_sz, "jump.blockMax.invalid");
            return -1;
        }
        if (out_override_set) {
            *out_override_set = 1;
        }
        if (out_override) {
            *out_override = n;
        }
        while (q > line && isspace((unsigned char)q[-1])) {
            q--;
        }
        *q = '\0';
        return 0;
    }
    return 0;
}
