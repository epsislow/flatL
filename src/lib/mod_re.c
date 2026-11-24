#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#ifdef FLAT_RE_BUNDLED
#include "../../external/pcre2/pcre2.h"
#else
#include <pcre2.h>
#endif

#define FLAT_RE_PATTERN_MAX 8192u
#define FLAT_RE_MATCHES_MAX 1024u

static int re_fail(FlatNativeCtx *ctx, const char *why) {
    if (ctx && ctx->io_status) {
        *ctx->io_status = FLAT_IO_ERR;
    }
    if (ctx && why) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", why);
    }
    return -1;
}

static int re_ok(FlatNativeCtx *ctx) {
    if (ctx && ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return 0;
}

static void release_args(FlatNativeCtx *ctx, FlatValue *v, int n) {
    int i;
    for (i = 0; i < n; i++) {
        flat_native_parse_rhs_release(ctx, &v[i]);
    }
}

static int split_two(char *s, char **a, char **b) {
    int depth = 0;
    char q = 0;
    char *p;
    for (p = s; *p; p++) {
        if (q) {
            if (*p == q) {
                q = 0;
            }
            continue;
        }
        if (*p == '\'' || *p == '"') {
            q = *p;
            continue;
        }
        if (*p == '[' || *p == '{' || *p == '(') {
            depth++;
        } else if ((*p == ']' || *p == '}' || *p == ')') && depth) {
            depth--;
        } else if (*p == ',' && depth == 0) {
            *p = '\0';
            *a = flat_native_trim(s);
            *b = flat_native_trim(p + 1);
            return 0;
        }
    }
    return -1;
}

static int split_three(char *s, char **a, char **b, char **c) {
    char *rest;
    if (split_two(s, a, &rest) != 0) {
        return -1;
    }
    return split_two(rest, b, c);
}

static int value_bytes(FlatNativeCtx *ctx, const FlatValue *v, unsigned char **out, size_t *len) {
    const uint8_t *p;
    size_t n;
    if (!ctx || !v || !out || !len) {
        return -1;
    }
    if (v->tag == VAL_STR_INLINE) {
        n = v->u.ins.len;
        p = (const uint8_t *)v->u.ins.bytes;
    } else if (v->tag == VAL_STR_REF) {
        p = flat_pool_read(ctx->pool, v->u.ref_id, &n);
        if (!p) {
            return -1;
        }
    } else {
        return -1;
    }
    *out = (unsigned char *)malloc(n ? n : 1);
    if (!*out) {
        return -1;
    }
    if (n) {
        memcpy(*out, p, n);
    }
    *len = n;
    return 0;
}

static int out_bytes(FlatNativeCtx *ctx, const void *data, size_t len) {
    uint32_t ref;
    if (!ctx || !ctx->out) {
        return -1;
    }
    if (len <= FLAT_INLINE_STR_MAX) {
        re_ok(ctx);
        return flat_value_set_str_inline(ctx->out, data ? (const char *)data : "", len);
    }
    if (flat_pool_alloc_copy(ctx->pool, data ? data : "", len, &ref) != 0) {
        return re_fail(ctx, "re.memory");
    }
    re_ok(ctx);
    return flat_value_set_str_ref(ctx->out, ref);
}

static int set_str_item(FlatNativeCtx *ctx, FlatValue *dst, const unsigned char *data, size_t len) {
    uint32_t ref;
    if (len <= FLAT_INLINE_STR_MAX) {
        return flat_value_set_str_inline(dst, data ? (const char *)data : "", len);
    }
    if (flat_pool_alloc_copy(ctx->pool, data ? (const void *)data : "", len, &ref) != 0) {
        return -1;
    }
    return flat_value_set_str_ref(dst, ref);
}

static pcre2_code *compile_pattern(const unsigned char *pat, size_t pat_len, char *why, size_t why_sz,
                                   const char *fn) {
    int errcode = 0;
    PCRE2_SIZE erroffset = 0;
    pcre2_code *re;
    char buf[96];
    if (pat_len > FLAT_RE_PATTERN_MAX) {
        snprintf(why, why_sz, "%s.limit", fn);
        return NULL;
    }
    re = pcre2_compile((PCRE2_SPTR)pat, (PCRE2_SIZE)pat_len, 0, &errcode, &erroffset, NULL);
    if (!re) {
        snprintf(buf, sizeof buf, "%s.pattern", fn);
        snprintf(why, why_sz, "%s", buf);
        return NULL;
    }
    return re;
}

static int build_capture_list(FlatNativeCtx *ctx, pcre2_code *re, pcre2_match_data *md,
                              const unsigned char *subject, size_t subject_len, const char *fn) {
    uint32_t list_id;
    FlatValue *items = NULL;
    uint32_t count;
    uint32_t i;
    PCRE2_SIZE *ovector;
    int rc;

    (void)subject_len;
    count = pcre2_get_ovector_count(md);
    if (count == 0) {
        return re_fail(ctx, "re.memory");
    }
    items = (FlatValue *)calloc(count, sizeof *items);
    if (!items) {
        return re_fail(ctx, "re.memory");
    }
    ovector = pcre2_get_ovector_pointer(md);
    for (i = 0; i < count; i++) {
        PCRE2_SIZE start = ovector[2 * i];
        PCRE2_SIZE end = ovector[2 * i + 1];
        if (start == PCRE2_UNSET || end == PCRE2_UNSET || end < start) {
            if (set_str_item(ctx, &items[i], (const unsigned char *)"", 0) != 0) {
                free(items);
                return re_fail(ctx, "re.memory");
            }
        } else {
            if (set_str_item(ctx, &items[i], subject + start, (size_t)(end - start)) != 0) {
                free(items);
                return re_fail(ctx, "re.memory");
            }
        }
    }
    (void)re;
    if (flat_list_build(ctx->coll, ctx->pool, items, (int)count, &list_id) != 0) {
        free(items);
        return re_fail(ctx, "re.memory");
    }
    free(items);
    re_ok(ctx);
    rc = flat_value_set_list(ctx->out, list_id);
    if (rc != 0) {
        return re_fail(ctx, fn);
    }
    return 0;
}

typedef enum {
    RE_OP_TEST = 0,
    RE_OP_MATCH,
    RE_OP_FIND
} ReMatchOp;

static int re_match_op(FlatNativeCtx *ctx, ReMatchOp op) {
    char buf[4096];
    char *a;
    char *b;
    FlatValue vs[2];
    unsigned char *pat = NULL;
    unsigned char *subj = NULL;
    size_t pat_len = 0;
    size_t subj_len = 0;
    pcre2_code *re = NULL;
    pcre2_match_data *md = NULL;
    char why[64];
    const char *fn;
    int mrc;
    PCRE2_SIZE *ovector;
    int rc = -1;

    memset(vs, 0, sizeof vs);
    why[0] = '\0';
    if (op == RE_OP_TEST) {
        fn = "re.test";
    } else if (op == RE_OP_MATCH) {
        fn = "re.match";
    } else {
        fn = "re.find";
    }
    if (!ctx || !ctx->out || !ctx->inner) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_two(buf, &a, &b) != 0) {
        return re_fail(ctx, op == RE_OP_TEST ? "re.test.args" : (op == RE_OP_MATCH ? "re.match.args" : "re.find.args"));
    }
    if (flat_native_parse_rhs(ctx, a, &vs[0]) != 0 || flat_native_parse_rhs(ctx, b, &vs[1]) != 0) {
        release_args(ctx, vs, 2);
        return re_fail(ctx, op == RE_OP_TEST ? "re.test.args" : (op == RE_OP_MATCH ? "re.match.args" : "re.find.args"));
    }
    if (value_bytes(ctx, &vs[0], &pat, &pat_len) != 0 || value_bytes(ctx, &vs[1], &subj, &subj_len) != 0) {
        free(pat);
        free(subj);
        release_args(ctx, vs, 2);
        return re_fail(ctx, op == RE_OP_TEST ? "re.test.args" : (op == RE_OP_MATCH ? "re.match.args" : "re.find.args"));
    }
    re = compile_pattern(pat, pat_len, why, sizeof why, fn);
    if (!re) {
        free(pat);
        free(subj);
        release_args(ctx, vs, 2);
        return re_fail(ctx, why[0] ? why : "re.pattern");
    }
    md = pcre2_match_data_create_from_pattern(re, NULL);
    if (!md) {
        pcre2_code_free(re);
        free(pat);
        free(subj);
        release_args(ctx, vs, 2);
        return re_fail(ctx, "re.memory");
    }
    mrc = pcre2_match(re, (PCRE2_SPTR)subj, (PCRE2_SIZE)subj_len, 0, 0, md, NULL);
    if (mrc < 0) {
        if (mrc == PCRE2_ERROR_NOMATCH) {
            if (op == RE_OP_TEST) {
                re_ok(ctx);
                rc = flat_value_set_bool(ctx->out, 0);
            } else {
                snprintf(why, sizeof why, "%s.nomatch", fn);
                rc = re_fail(ctx, why);
            }
        } else {
            snprintf(why, sizeof why, "%s.match", fn);
            rc = re_fail(ctx, why);
        }
        goto done;
    }
    ovector = pcre2_get_ovector_pointer(md);
    if (op == RE_OP_MATCH && ovector[0] != 0) {
        snprintf(why, sizeof why, "%s.nomatch", fn);
        rc = re_fail(ctx, why);
        goto done;
    }
    if (op == RE_OP_TEST) {
        re_ok(ctx);
        rc = flat_value_set_bool(ctx->out, 1);
        goto done;
    }
    rc = build_capture_list(ctx, re, md, subj, subj_len, fn);

done:
    if (md) {
        pcre2_match_data_free(md);
    }
    if (re) {
        pcre2_code_free(re);
    }
    free(pat);
    free(subj);
    release_args(ctx, vs, 2);
    return rc;
}

int flat_native_re_test(FlatNativeCtx *ctx) {
    return re_match_op(ctx, RE_OP_TEST);
}

int flat_native_re_match(FlatNativeCtx *ctx) {
    return re_match_op(ctx, RE_OP_MATCH);
}

int flat_native_re_find(FlatNativeCtx *ctx) {
    return re_match_op(ctx, RE_OP_FIND);
}

int flat_native_re_replace(FlatNativeCtx *ctx) {
    char buf[4096];
    char *a;
    char *b;
    char *c;
    FlatValue vs[3];
    unsigned char *pat = NULL;
    unsigned char *subj = NULL;
    unsigned char *repl = NULL;
    size_t pat_len = 0;
    size_t subj_len = 0;
    size_t repl_len = 0;
    pcre2_code *re = NULL;
    PCRE2_UCHAR *out = NULL;
    PCRE2_SIZE out_len = 0;
    char why[64];
    int src;
    int rc = -1;

    memset(vs, 0, sizeof vs);
    why[0] = '\0';
    if (!ctx || !ctx->out || !ctx->inner) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_three(buf, &a, &b, &c) != 0) {
        return re_fail(ctx, "re.replace.args");
    }
    if (flat_native_parse_rhs(ctx, a, &vs[0]) != 0 || flat_native_parse_rhs(ctx, b, &vs[1]) != 0 ||
        flat_native_parse_rhs(ctx, c, &vs[2]) != 0) {
        release_args(ctx, vs, 3);
        return re_fail(ctx, "re.replace.args");
    }
    if (value_bytes(ctx, &vs[0], &pat, &pat_len) != 0 || value_bytes(ctx, &vs[1], &subj, &subj_len) != 0 ||
        value_bytes(ctx, &vs[2], &repl, &repl_len) != 0) {
        free(pat);
        free(subj);
        free(repl);
        release_args(ctx, vs, 3);
        return re_fail(ctx, "re.replace.args");
    }
    re = compile_pattern(pat, pat_len, why, sizeof why, "re.replace");
    if (!re) {
        free(pat);
        free(subj);
        free(repl);
        release_args(ctx, vs, 3);
        return re_fail(ctx, why[0] ? why : "re.replace.pattern");
    }
    out_len = subj_len + 64;
    for (;;) {
        out = (PCRE2_UCHAR *)malloc(out_len ? out_len : 1);
        if (!out) {
            pcre2_code_free(re);
            free(pat);
            free(subj);
            free(repl);
            release_args(ctx, vs, 3);
            return re_fail(ctx, "re.memory");
        }
        src = pcre2_substitute(re, (PCRE2_SPTR)subj, (PCRE2_SIZE)subj_len, 0,
                               PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_OVERFLOW_LENGTH, NULL, NULL,
                               (PCRE2_SPTR)repl, (PCRE2_SIZE)repl_len, out, &out_len);
        if (src != PCRE2_ERROR_NOMEMORY) {
            break;
        }
        free(out);
        out = NULL;
        /* out_len updated to required size */
    }
    if (src < 0) {
        free(out);
        pcre2_code_free(re);
        free(pat);
        free(subj);
        free(repl);
        release_args(ctx, vs, 3);
        if (src == PCRE2_ERROR_NOSUBSTRING || src == PCRE2_ERROR_BADREPLACEMENT) {
            return re_fail(ctx, "re.replace.repl");
        }
        return re_fail(ctx, "re.replace");
    }
    if ((uint32_t)src > FLAT_RE_MATCHES_MAX) {
        free(out);
        pcre2_code_free(re);
        free(pat);
        free(subj);
        free(repl);
        release_args(ctx, vs, 3);
        return re_fail(ctx, "re.replace.limit");
    }
    rc = out_bytes(ctx, out, (size_t)out_len);
    free(out);
    pcre2_code_free(re);
    free(pat);
    free(subj);
    free(repl);
    release_args(ctx, vs, 3);
    return rc;
}

int flat_native_re_split(FlatNativeCtx *ctx) {
    char buf[4096];
    char *a;
    char *b;
    FlatValue vs[2];
    unsigned char *pat = NULL;
    unsigned char *subj = NULL;
    size_t pat_len = 0;
    size_t subj_len = 0;
    pcre2_code *re = NULL;
    pcre2_match_data *md = NULL;
    FlatValue *items = NULL;
    uint32_t list_id;
    char why[64];
    PCRE2_SIZE offset = 0;
    int part_count = 0;
    int match_count = 0;

    memset(vs, 0, sizeof vs);
    why[0] = '\0';
    if (!ctx || !ctx->out || !ctx->inner) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    if (split_two(buf, &a, &b) != 0) {
        return re_fail(ctx, "re.split.args");
    }
    if (flat_native_parse_rhs(ctx, a, &vs[0]) != 0 || flat_native_parse_rhs(ctx, b, &vs[1]) != 0) {
        release_args(ctx, vs, 2);
        return re_fail(ctx, "re.split.args");
    }
    if (value_bytes(ctx, &vs[0], &pat, &pat_len) != 0 || value_bytes(ctx, &vs[1], &subj, &subj_len) != 0) {
        free(pat);
        free(subj);
        release_args(ctx, vs, 2);
        return re_fail(ctx, "re.split.args");
    }
    re = compile_pattern(pat, pat_len, why, sizeof why, "re.split");
    if (!re) {
        free(pat);
        free(subj);
        release_args(ctx, vs, 2);
        return re_fail(ctx, why[0] ? why : "re.split.pattern");
    }
    md = pcre2_match_data_create_from_pattern(re, NULL);
    if (!md) {
        pcre2_code_free(re);
        free(pat);
        free(subj);
        release_args(ctx, vs, 2);
        return re_fail(ctx, "re.memory");
    }
    items = (FlatValue *)calloc(FLAT_RE_MATCHES_MAX, sizeof *items);
    if (!items) {
        pcre2_match_data_free(md);
        pcre2_code_free(re);
        free(pat);
        free(subj);
        release_args(ctx, vs, 2);
        return re_fail(ctx, "re.memory");
    }

    while (part_count < (int)FLAT_RE_MATCHES_MAX) {
        int mrc;
        PCRE2_SIZE *ovector;
        PCRE2_SIZE mstart;
        PCRE2_SIZE mend;
        uint32_t options = 0;

        if (offset > (PCRE2_SIZE)subj_len) {
            break;
        }
        if (offset == (PCRE2_SIZE)subj_len) {
            if (set_str_item(ctx, &items[part_count], (const unsigned char *)"", 0) != 0) {
                goto mem_fail;
            }
            part_count++;
            break;
        }
        mrc = pcre2_match(re, (PCRE2_SPTR)subj, (PCRE2_SIZE)subj_len, offset, options, md, NULL);
        if (mrc == PCRE2_ERROR_NOMATCH) {
            if (set_str_item(ctx, &items[part_count], subj + offset, subj_len - (size_t)offset) != 0) {
                goto mem_fail;
            }
            part_count++;
            break;
        }
        if (mrc < 0) {
            free(items);
            pcre2_match_data_free(md);
            pcre2_code_free(re);
            free(pat);
            free(subj);
            release_args(ctx, vs, 2);
            return re_fail(ctx, "re.split.match");
        }
        ovector = pcre2_get_ovector_pointer(md);
        mstart = ovector[0];
        mend = ovector[1];
        if (mstart < offset) {
            free(items);
            pcre2_match_data_free(md);
            pcre2_code_free(re);
            free(pat);
            free(subj);
            release_args(ctx, vs, 2);
            return re_fail(ctx, "re.split.match");
        }
        if (set_str_item(ctx, &items[part_count], subj + offset, (size_t)(mstart - offset)) != 0) {
            goto mem_fail;
        }
        part_count++;
        match_count++;
        if (match_count > (int)FLAT_RE_MATCHES_MAX) {
            free(items);
            pcre2_match_data_free(md);
            pcre2_code_free(re);
            free(pat);
            free(subj);
            release_args(ctx, vs, 2);
            return re_fail(ctx, "re.split.limit");
        }
        if (mend == mstart) {
            /* empty match: emit empty part advance one code unit if possible */
            if (offset < (PCRE2_SIZE)subj_len && mend == offset) {
                /* advance to avoid infinite loop */
                offset = mend + 1;
            } else {
                offset = mend;
            }
        } else {
            offset = mend;
        }
        if (part_count >= (int)FLAT_RE_MATCHES_MAX && offset < (PCRE2_SIZE)subj_len) {
            free(items);
            pcre2_match_data_free(md);
            pcre2_code_free(re);
            free(pat);
            free(subj);
            release_args(ctx, vs, 2);
            return re_fail(ctx, "re.split.limit");
        }
    }

    if (flat_list_build(ctx->coll, ctx->pool, items, part_count, &list_id) != 0) {
        goto mem_fail;
    }
    free(items);
    pcre2_match_data_free(md);
    pcre2_code_free(re);
    free(pat);
    free(subj);
    release_args(ctx, vs, 2);
    re_ok(ctx);
    return flat_value_set_list(ctx->out, list_id);

mem_fail:
    free(items);
    pcre2_match_data_free(md);
    pcre2_code_free(re);
    free(pat);
    free(subj);
    release_args(ctx, vs, 2);
    return re_fail(ctx, "re.memory");
}
