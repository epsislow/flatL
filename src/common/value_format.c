#include "flat_value.h"

#include <stdio.h>
#include <string.h>

#define FMT_DEPTH_MAX 6
#define FMT_STR_PREVIEW 80

static void fprint_quoted(FILE *out, const char *s, size_t len) {
    fputc('\'', out);
    while (len > 0) {
        unsigned char c = (unsigned char)*s++;
        len--;
        if (c == '\'' || c == '\\') {
            fputc('\\', out);
        }
        fputc((int)c, out);
    }
    fputc('\'', out);
}

static void flat_value_fprint_impl(FILE *out, const FlatValue *v, const FlatStringPool *pool,
                                   const FlatCollectionPool *coll, int depth, int raw_strings);

static void fprint_list(FILE *out, const FlatStringPool *pool, const FlatCollectionPool *coll,
                        uint32_t id, int depth) {
    int len = 0;
    int i;

    if (!coll || depth > FMT_DEPTH_MAX) {
        fputs("[…]", out);
        return;
    }
    if (flat_list_len(coll, id, &len) != 0) {
        fputs("[?]", out);
        return;
    }
    fputc('[', out);
    for (i = 0; i < len; i++) {
        FlatValue el;
        if (i > 0) {
            fputs(", ", out);
        }
        if (flat_list_get(coll, id, i, &el) != 0) {
            fputs("?", out);
            continue;
        }
        flat_value_fprint_impl(out, &el, pool, coll, depth + 1, 0);
    }
    fputc(']', out);
}

static void fprint_object(FILE *out, const FlatStringPool *pool, const FlatCollectionPool *coll,
                          uint32_t id, int depth) {
    int len = 0;
    int i;

    if (!coll || depth > FMT_DEPTH_MAX) {
        fputs("{…}", out);
        return;
    }
    if (flat_obj_len(coll, id, &len) != 0) {
        fputs("{?}", out);
        return;
    }
    fputc('{', out);
    for (i = 0; i < len; i++) {
        const char *key = NULL;
        FlatValue val;
        if (i > 0) {
            fputs(", ", out);
        }
        if (flat_obj_entry_at(coll, id, i, &key, &val) != 0 || !key) {
            fputs("?", out);
            continue;
        }
        fprint_quoted(out, key, strlen(key));
        fputs(": ", out);
        flat_value_fprint_impl(out, &val, pool, coll, depth + 1, 0);
    }
    fputc('}', out);
}

static void flat_value_fprint_impl(FILE *out, const FlatValue *v, const FlatStringPool *pool,
                                   const FlatCollectionPool *coll, int depth, int raw_strings) {
    size_t len = 0;
    const uint8_t *p;

    if (!v || !out) {
        fputs("<null>", out);
        return;
    }
    switch (v->tag) {
    case VAL_EMPTY:
        fputs("<empty>", out);
        break;
    case VAL_BOOL:
        fputs(v->u.boolean ? "true" : "false", out);
        break;
    case VAL_NUMBER:
        fprintf(out, "%.17g", v->u.number);
        break;
    case VAL_STR_INLINE:
        if (raw_strings && depth == 0) {
            fwrite(v->u.ins.bytes, 1, v->u.ins.len, out);
        } else {
            fprint_quoted(out, v->u.ins.bytes, v->u.ins.len);
        }
        break;
    case VAL_STR_REF:
        if (!pool) {
            fputs("<str-ref>", out);
            break;
        }
        p = flat_pool_read(pool, v->u.ref_id, &len);
        if (!p) {
            fputs("<str-ref?>", out);
            break;
        }
        if (raw_strings && depth == 0) {
            size_t n = len;
            if (n > FMT_STR_PREVIEW) {
                n = FMT_STR_PREVIEW;
            }
            fwrite(p, 1, n, out);
            if (len > FMT_STR_PREVIEW) {
                fprintf(out, "…+%zu", len - FMT_STR_PREVIEW);
            }
        } else if (len > FMT_STR_PREVIEW) {
            fprint_quoted(out, (const char *)p, FMT_STR_PREVIEW);
            fprintf(out, "…+%zu", len - FMT_STR_PREVIEW);
        } else {
            fprint_quoted(out, (const char *)p, len);
        }
        break;
    case VAL_LIST:
        fprint_list(out, pool, coll, v->u.coll_id, depth);
        break;
    case VAL_OBJECT:
        fprint_object(out, pool, coll, v->u.coll_id, depth);
        break;
    default:
        fputs("<?>", out);
        break;
    }
}

void flat_value_fprint_ex(FILE *out, const FlatValue *v, const FlatStringPool *pool,
                          const FlatCollectionPool *coll, int raw_strings) {
    flat_value_fprint_impl(out, v, pool, coll, 0, raw_strings ? 1 : 0);
}

void flat_value_fprint(FILE *out, const FlatValue *v, const FlatStringPool *pool,
                       const FlatCollectionPool *coll) {
    flat_value_fprint_ex(out, v, pool, coll, 0);
}

void flat_value_fprint_ln(FILE *out, const FlatValue *v, const FlatStringPool *pool,
                          const FlatCollectionPool *coll) {
    flat_value_fprint(out, v, pool, coll);
    fputc('\n', out);
}

void flat_value_fprint_io(FILE *out, const FlatValue *v, const FlatStringPool *pool,
                          const FlatCollectionPool *coll) {
    flat_value_fprint_ex(out, v, pool, coll, 1);
}

void flat_value_fprint_io_ln(FILE *out, const FlatValue *v, const FlatStringPool *pool,
                             const FlatCollectionPool *coll) {
    flat_value_fprint_io(out, v, pool, coll);
    fputc('\n', out);
}
