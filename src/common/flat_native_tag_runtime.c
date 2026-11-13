#include "flat_native_tag.h"

#include "flat_native_util.h"
#include "flat_value.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLAT_TAG_MAX_TOKENS 16
#define FLAT_TAG_TOKEN_LEN 64

static int tokenize_sort(char tokens[][FLAT_TAG_TOKEN_LEN], int *count, const char *spec) {
    const char *p;
    char buf[512];
    int n = 0;

    if (!spec || !count) {
        return -1;
    }
    *count = 0;
    snprintf(buf, sizeof buf, "%s", spec);
    p = buf;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        {
            size_t i = 0;
            while (*p && !isspace((unsigned char)*p)) {
                if (i + 1 >= FLAT_TAG_TOKEN_LEN) {
                    return -1;
                }
                tokens[n][i++] = *p++;
            }
            tokens[n][i] = '\0';
            n++;
            if (n >= FLAT_TAG_MAX_TOKENS) {
                return -1;
            }
        }
    }
    if (n == 0) {
        *count = 0;
        return 0;
    }
    qsort(tokens, (size_t)n, sizeof tokens[0], (int (*)(const void *, const void *))strcmp);
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
    case FLAT_NATIVE_IO_INLN:
        return tag_policy_numeric_only(native_id, tokens, n);
    default:
        if (n > 0) {
            return -1;
        }
        return 0;
    }
}

static int eval_tag_string(FlatNativeCtx *ctx, const char *tag_expr, char *spec, size_t spec_sz) {
    FlatValue v;
    char buf[512];

    if (!ctx || !tag_expr) {
        return -1;
    }
    if (tag_expr[0] == '\0') {
        spec[0] = '\0';
        return 0;
    }
    snprintf(buf, sizeof buf, "%s", tag_expr);
    if (flat_value_parse_assign_rhs(flat_native_trim(buf), &v, ctx->pool, ctx->coll, ctx->vars,
                                    ctx->var_count, ctx->var_names) != 0) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.tag.arg");
        return -1;
    }
    if (v.tag != VAL_STR_INLINE && v.tag != VAL_STR_REF) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.tag.arg");
        return -1;
    }
    if (v.tag == VAL_STR_INLINE) {
        if (v.u.ins.len + 1 > spec_sz) {
            return -1;
        }
        memcpy(spec, v.u.ins.bytes, v.u.ins.len);
        spec[v.u.ins.len] = '\0';
        return 0;
    }
    {
        size_t len = 0;
        const uint8_t *p = flat_pool_read(ctx->pool, v.u.ref_id, &len);
        if (!p || len + 1 > spec_sz) {
            return -1;
        }
        memcpy(spec, p, len);
        spec[len] = '\0';
    }
    return 0;
}

int flat_native_tag_reject_untagged_native(FlatNativeCtx *ctx, uint16_t native_id) {
    if (!ctx) {
        return -1;
    }
    if (flat_native_native_accepts_tags(native_id)) {
        return 0;
    }
    if (!ctx->tag_expr || ctx->tag_expr[0] == '\0') {
        return 0;
    }
    {
        char spec[256];
        char tokens[FLAT_TAG_MAX_TOKENS][FLAT_TAG_TOKEN_LEN];
        int ntok;

        if (eval_tag_string(ctx, ctx->tag_expr, spec, sizeof spec) != 0) {
            return -1;
        }
        if (tokenize_sort(tokens, &ntok, spec) != 0) {
            snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.tag.not_accepted");
            return -1;
        }
        if (ntok > 0) {
            snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.tag.not_accepted");
            return -1;
        }
    }
    return 0;
}

int flat_native_tag_resolve_type(FlatNativeCtx *ctx, uint16_t native_id, FlatSlotType *out_ty,
                                  int *has_tag) {
    char spec[256];
    char tokens[FLAT_TAG_MAX_TOKENS][FLAT_TAG_TOKEN_LEN];
    int ntok;
    const FlatSchemaCatalog *cat;

    if (has_tag) {
        *has_tag = 0;
    }
    if (!ctx || !out_ty) {
        return -1;
    }

    if (!ctx->tag_expr) {
        return 0;
    }

    if (eval_tag_string(ctx, ctx->tag_expr, spec, sizeof spec) != 0) {
        return -1;
    }
    if (tokenize_sort(tokens, &ntok, spec) != 0) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.tag.not_accepted");
        return -1;
    }
    if (ntok == 0) {
        return 0;
    }

    if (!flat_native_native_accepts_tags(native_id)) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.tag.not_accepted");
        return -1;
    }
    if (validate_tokens_for_native(native_id, tokens, ntok) != 0) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.tag.not_accepted");
        return -1;
    }
    {
        char one[128];
        if (spec_from_flat_tag_tokens(tokens, ntok, one, sizeof one) != 0) {
            snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.tag.not_accepted");
            return -1;
        }
        cat = ctx->bc ? &ctx->bc->schemas : NULL;
        if (flat_slot_type_parse_spec(one, out_ty, cat, NULL, 0) != 0) {
            snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.tag.not_accepted");
            return -1;
        }
    }
    if (has_tag) {
        *has_tag = 1;
    }
    return 0;
}
