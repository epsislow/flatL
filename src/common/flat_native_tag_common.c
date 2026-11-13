#include "flat_native_tag.h"

#include "flat_native_util.h"
#include "flat_slot_type.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLAT_TAG_MAX_TOKENS 16
#define FLAT_TAG_TOKEN_LEN 64

static int tag_scan_semicolon(char *s, char **operands, char **tag_expr) {
    int depth = 0;
    char quote = 0;
    char *p;

    for (p = s; *p; p++) {
        if (!quote && (*p == '\'' || *p == '"')) {
            quote = *p;
        } else if (quote && *p == quote) {
            quote = 0;
        } else if (!quote && (*p == '[' || *p == '(')) {
            depth++;
        } else if (!quote && (*p == ']' || *p == ')')) {
            if (depth > 0) {
                depth--;
            }
        } else if (!quote && depth == 0 && *p == ';') {
            *p = '\0';
            *operands = flat_native_trim(s);
            *tag_expr = flat_native_trim(p + 1);
            return 1;
        }
    }
    *operands = flat_native_trim(s);
    *tag_expr = NULL;
    return 0;
}

void flat_native_split_operands_tag(char *buf, char **operands, char **tag_expr, int *has_tag_section) {
    if (!buf || !operands || !tag_expr || !has_tag_section) {
        return;
    }
  *has_tag_section = tag_scan_semicolon(buf, operands, tag_expr);
    if (!*has_tag_section) {
        *tag_expr = NULL;
    }
}

static int is_empty_string_literal(const char *expr) {
    char scratch[64];
    const char *p;

    if (!expr) {
        return 0;
    }
    snprintf(scratch, sizeof scratch, "%s", expr);
    p = flat_native_trim(scratch);
    if ((p[0] == '"' && p[1] == '"' && p[2] == '\0') ||
        (p[0] == '\'' && p[1] == '\'' && p[2] == '\0')) {
        return 1;
    }
    return 0;
}

static int compare_tag_token_rows(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

static int tokenize_sort(char tokens[][FLAT_TAG_TOKEN_LEN], int *count, const char *spec) {
    const char *p;
    int n = 0;

    if (!spec || !count) {
        return -1;
    }
    *count = 0;
    p = spec;

    while (*p) {
        size_t i = 0;

        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        if (n >= FLAT_TAG_MAX_TOKENS) {
            return -1;
        }
        while (*p && !isspace((unsigned char)*p)) {
            if (i + 1 >= FLAT_TAG_TOKEN_LEN) {
                return -1;
            }
            tokens[n][i++] = *p++;
        }
        tokens[n][i] = '\0';
        n++;
    }

    if (n > 1) {
        qsort(tokens, (size_t)n, sizeof tokens[0], compare_tag_token_rows);
    }
    *count = n;
    return 0;
}

static int spec_from_flat_tag_tokens(const char tokens[][FLAT_TAG_TOKEN_LEN], int n,
                                     char *spec_out, size_t spec_sz) {
    int i;
    size_t pos = 0;

    if (!spec_out || spec_sz == 0) {
        return -1;
    }
    spec_out[0] = '\0';
    if (n <= 0) {
        return 0;
    }
    if (n > 1) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        size_t len = strlen(tokens[i]);
        if (pos + len + 2 > spec_sz) {
            return -1;
        }
        if (i > 0) {
            spec_out[pos++] = ' ';
        }
        memcpy(spec_out + pos, tokens[i], len);
        pos += len;
    }
    spec_out[pos] = '\0';
    return 0;
}

static int numeric_type_token_count(const char tokens[][FLAT_TAG_TOKEN_LEN], int n) {
    int i;
    int c = 0;
    FlatSlotType ty;
    char spec[FLAT_TAG_TOKEN_LEN + 4];

    for (i = 0; i < n; i++) {
        snprintf(spec, sizeof spec, "%s", tokens[i]);
        if (flat_slot_type_parse_spec(spec, &ty, NULL, NULL, 0) == 0 &&
            flat_slot_type_is_numeric(&ty)) {
            c++;
        }
    }
    return c;
}

int flat_native_native_accepts_tags(uint16_t native_id) {
    switch ((FlatNativeId)native_id) {
    case FLAT_NATIVE_MATH_CALC:
    case FLAT_NATIVE_MATH_SUM2:
    case FLAT_NATIVE_MATH_MIN2:
    case FLAT_NATIVE_MATH_MAX2:
    case FLAT_NATIVE_IO_INLN:
    case FLAT_NATIVE_IO_IN_NUM:
    case FLAT_NATIVE_IO_IN_BOOL:
    case FLAT_NATIVE_STR_TO_NUM:
        return 1;
    default:
        return 0;
    }
}

static int tag_policy_numeric_only(uint16_t native_id, const char tokens[][FLAT_TAG_TOKEN_LEN], int n) {
    FlatSlotType ty;
    char spec[FLAT_TAG_TOKEN_LEN + 4];
    int num_n;

    (void)native_id;
    if (n == 0) {
        return 0;
    }
    num_n = numeric_type_token_count(tokens, n);
    if (num_n > 1) {
        return -1;
    }
    if (spec_from_flat_tag_tokens(tokens, n, spec, sizeof spec) != 0) {
        return -1;
    }
    if (flat_slot_type_parse_spec(spec, &ty, NULL, NULL, 0) != 0) {
        return -1;
    }
    if (!flat_slot_type_is_numeric(&ty) && ty.kind != FLAT_SLOT_TYPE_T &&
        ty.kind != FLAT_SLOT_TYPE_TN) {
        return -1;
    }
    return 0;
}

static int validate_tokens_for_native(uint16_t native_id, const char tokens[][FLAT_TAG_TOKEN_LEN],
                                      int n) {
    switch ((FlatNativeId)native_id) {
    case FLAT_NATIVE_MATH_CALC:
    case FLAT_NATIVE_MATH_SUM2:
    case FLAT_NATIVE_MATH_MIN2:
    case FLAT_NATIVE_MATH_MAX2:
    case FLAT_NATIVE_IO_IN_NUM:
    case FLAT_NATIVE_IO_IN_BOOL:
    case FLAT_NATIVE_STR_TO_NUM:
        return tag_policy_numeric_only(native_id, tokens, n);
    case FLAT_NATIVE_IO_INLN:
        return tag_policy_numeric_only(native_id, tokens, n);
    default:
        if (n > 0) {
            return -1;
        }
        return 0;
    }
}

static int string_from_tag_expr_compile(const char *tag_expr, char *out, size_t out_sz) {
    const char *p;
    char quote;

    if (!tag_expr || !out || out_sz == 0) {
        return -1;
    }
    p = tag_expr;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '"' && *p != '\'') {
        return -1;
    }
    quote = *p++;
    {
        size_t i = 0;
        while (*p && *p != quote) {
            if (i + 1 >= out_sz) {
                return -1;
            }
            out[i++] = *p++;
        }
        if (*p != quote) {
            return -1;
        }
        out[i] = '\0';
    }
    while (*++p) {
        if (!isspace((unsigned char)*p)) {
            return -1;
        }
    }
    return 0;
}

int flat_native_tag_compile_check(uint16_t native_id, const char *inner, char *err, size_t err_sz) {
    char buf[4096];
    char *operands;
    char *tag_expr;
    int has_tag;
    char tokens[FLAT_TAG_MAX_TOKENS][FLAT_TAG_TOKEN_LEN];
    int ntok;
    char spec[256];

    if (!inner) {
        return 0;
    }
    snprintf(buf, sizeof buf, "%s", inner);
    flat_native_split_operands_tag(buf, &operands, &tag_expr, &has_tag);

    if (!has_tag) {
        if (flat_native_native_accepts_tags(native_id)) {
            return 0;
        }
        return 0;
    }

    if (is_empty_string_literal(tag_expr)) {
        snprintf(err, err_sz, "type.tag.arg");
        return -1;
    }

    if (!flat_native_native_accepts_tags(native_id)) {
        if (tag_expr && tag_expr[0] != '\0') {
            snprintf(err, err_sz, "type.tag.not_accepted");
            return -1;
        }
        return 0;
    }

    if (!tag_expr || tag_expr[0] == '\0') {
        return 0;
    }

    if (string_from_tag_expr_compile(tag_expr, spec, sizeof spec) == 0) {
        if (spec[0] == '\0') {
            snprintf(err, err_sz, "type.tag.arg");
            return -1;
        }
        if (tokenize_sort(tokens, &ntok, spec) != 0 || validate_tokens_for_native(native_id, tokens, ntok) != 0) {
            snprintf(err, err_sz, "type.tag.not_accepted");
            return -1;
        }
        return 0;
    }

    /* Non-const tag expression: allowed at compile; validated at runtime. */
    return 0;
}
