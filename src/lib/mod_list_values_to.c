#include "flat_list_shape.h"
#include "flat_list_typed.h"
#include "flat_native_mod.h"
#include "flat_native_tag.h"
#include "flat_native_util.h"
#include "flat_number.h"
#include "flat_slot_type.h"
#include "flat_value.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FlatNativeCtx *ctx;
    FlatSlotType target;
    int mode_any;
    int changed;
} ValuesToState;

static int list_debug_enabled(void) {
    const char *env = getenv("FLAT_LIST_DEBUG");
    return env && env[0] == '1' && env[1] == '\0';
}

static void list_debug(const char *fmt, ...) {
    va_list ap;
    if (!list_debug_enabled()) {
        return;
    }
    fputs("[list] ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static int str_bytes(const FlatValue *v, FlatStringPool *pool, const uint8_t **bp, size_t *len) {
    if (!v || !bp || !len) {
        return -1;
    }
    if (v->tag == VAL_STR_INLINE) {
        *bp = (const uint8_t *)v->u.ins.bytes;
        *len = v->u.ins.len;
        return 0;
    }
    if (v->tag == VAL_STR_REF) {
        *bp = flat_pool_read(pool, v->u.ref_id, len);
        return *bp ? 0 : -1;
    }
    return -1;
}

static int str_token_buf(FlatStringPool *pool, const FlatValue *v, char *buf, size_t buf_sz) {
    const uint8_t *bp;
    size_t len;
    if (str_bytes(v, pool, &bp, &len) != 0 || len + 1 > buf_sz) {
        return -1;
    }
    memcpy(buf, bp, len);
    buf[len] = '\0';
    return 0;
}

static int emit_pool_str(FlatStringPool *pool, FlatValue *out, const void *data, size_t len) {
    uint32_t id;
    if (flat_pool_alloc_copy(pool, data, len, &id) != 0) {
        return -1;
    }
    return flat_value_set_str_ref(out, id);
}

static int values_leaf_equal(const FlatValue *a, const FlatValue *b, FlatStringPool *pool) {
    if (a->tag != b->tag) {
        return 0;
    }
    switch (a->tag) {
    case VAL_BOOL:
        return a->u.boolean == b->u.boolean;
    case VAL_NUMBER:
        return flat_number_equals(a, b);
    case VAL_STR_INLINE:
        if (a->u.ins.len != b->u.ins.len) {
            return 0;
        }
        return memcmp(a->u.ins.bytes, b->u.ins.bytes, a->u.ins.len) == 0;
    case VAL_STR_REF: {
        const uint8_t *pa;
        const uint8_t *pb;
        size_t la;
        size_t lb;
        if (a->u.ref_id != b->u.ref_id) {
            if (str_bytes(a, pool, &pa, &la) != 0 || str_bytes(b, pool, &pb, &lb) != 0) {
                return 0;
            }
            return la == lb && memcmp(pa, pb, la) == 0;
        }
        return 1;
    }
    default:
        return 0;
    }
}

static int values_to_fail(ValuesToState *st, const char *callee_detail) {
    if (st->mode_any) {
        return 1; /* skip leaf, copy unchanged */
    }
    if (callee_detail && callee_detail[0] != '\0') {
        snprintf(st->ctx->panic_detail, sizeof st->ctx->panic_detail, "%s", callee_detail);
    } else if (st->ctx->panic_detail[0] == '\0') {
        snprintf(st->ctx->panic_detail, sizeof st->ctx->panic_detail, "%s",
                 "type.list.values_to.fail");
    }
    return -1;
}

static int try_already_ok(ValuesToState *st, const FlatValue *src, FlatValue *out, int *changed) {
    FlatValue staged;
    char panic[80];
    const FlatSchemaCatalog *cat = st->ctx->bc ? &st->ctx->bc->schemas : NULL;

    *changed = 0;
    memcpy(&staged, src, sizeof staged);
    if (flat_slot_type_check_value(&st->target, src, st->ctx->pool, st->ctx->coll, cat, &staged,
                                   panic, sizeof panic) != 0) {
        return 0;
    }
    if (values_leaf_equal(src, &staged, st->ctx->pool)) {
        *out = *src;
        return 1;
    }
    *out = staged;
    *changed = 1;
    return 1;
}

static int convert_leaf(ValuesToState *st, const FlatValue *src, FlatValue *out) {
    int changed = 0;
    const FlatSchemaCatalog *cat;
    char panic[96];
    char tok[512];
    int rc;

    if (!src || !out) {
        return -1;
    }
    cat = st->ctx->bc ? &st->ctx->bc->schemas : NULL;

    if (src->tag == VAL_OBJECT || src->tag == VAL_EMPTY || src->tag == VAL_NULL) {
        return values_to_fail(st, NULL);
    }

    rc = try_already_ok(st, src, out, &changed);
    if (rc > 0) {
        if (changed) {
            st->changed = 1;
        }
        return 0;
    }

    memset(out, 0, sizeof *out);

    if (st->target.kind == FLAT_SLOT_TYPE_B) {
        if (src->tag == VAL_BOOL) {
            *out = *src;
            return 0;
        }
        if (src->tag == VAL_NUMBER) {
            double d = flat_number_as_f64(src);
            if (d == 0.0) {
                return flat_value_set_bool(out, 0) == 0 ? (st->changed = 1, 0) : -1;
            }
            if (d == 1.0) {
                return flat_value_set_bool(out, 1) == 0 ? (st->changed = 1, 0) : -1;
            }
            return values_to_fail(st, NULL);
        }
        if (src->tag == VAL_STR_INLINE || src->tag == VAL_STR_REF) {
            int b;
            if (str_token_buf(st->ctx->pool, src, tok, sizeof tok) != 0) {
                return values_to_fail(st, NULL);
            }
            if (flat_parse_bool_token(flat_native_trim(tok), &b) != 0) {
                return values_to_fail(st, NULL);
            }
            st->changed = 1;
            return flat_value_set_bool(out, b);
        }
        return values_to_fail(st, NULL);
    }

    if (flat_slot_type_is_numeric(&st->target)) {
        if (src->tag == VAL_NUMBER) {
            if (flat_number_has_nan(src) || !isfinite(flat_number_as_f64(src))) {
                return values_to_fail(st, NULL);
            }
            if (flat_number_coerce_for_slot(src, st->target.kind, out, panic, sizeof panic) != 0) {
                return values_to_fail(st, panic[0] ? panic : NULL);
            }
            st->changed = 1;
            return 0;
        }
        if (src->tag == VAL_BOOL) {
            if (flat_number_set_for_slot_kind(out, st->target.kind, src->u.boolean ? 1u : 0u,
                                              src->u.boolean ? 1 : 0, 0.0) != 0) {
                return -1;
            }
            if (flat_slot_type_check_value(&st->target, out, st->ctx->pool, st->ctx->coll, cat, out,
                                           panic, sizeof panic) != 0) {
                return values_to_fail(st, panic[0] ? panic : NULL);
            }
            st->changed = 1;
            return 0;
        }
        if (src->tag == VAL_STR_INLINE || src->tag == VAL_STR_REF) {
            if (str_token_buf(st->ctx->pool, src, tok, sizeof tok) != 0) {
                return values_to_fail(st, NULL);
            }
            if (flat_number_parse_for_slot_kind(flat_native_trim(tok), st->target.kind, out, panic,
                                                sizeof panic) != 0) {
                return values_to_fail(st, panic[0] ? panic : NULL);
            }
            if (flat_slot_type_is_numeric(&st->target) &&
                !isfinite(flat_number_as_f64(out))) {
                return values_to_fail(st, NULL);
            }
            st->changed = 1;
            return 0;
        }
        return values_to_fail(st, NULL);
    }

    if (st->target.kind == FLAT_SLOT_TYPE_T || st->target.kind == FLAT_SLOT_TYPE_TN) {
        if (src->tag == VAL_STR_INLINE || src->tag == VAL_STR_REF) {
            rc = try_already_ok(st, src, out, &changed);
            if (rc > 0) {
                if (changed) {
                    st->changed = 1;
                }
                return 0;
            }
            return values_to_fail(st, NULL);
        }
        if (src->tag == VAL_BOOL) {
            const char *lit = src->u.boolean ? "true" : "false";
            size_t lit_len = src->u.boolean ? 4u : 5u;
            if (lit_len <= FLAT_INLINE_STR_MAX) {
                if (flat_value_set_str_inline(out, lit, lit_len) != 0) {
                    return -1;
                }
            } else if (emit_pool_str(st->ctx->pool, out, lit, lit_len) != 0) {
                return -1;
            }
            if (flat_slot_type_check_value(&st->target, out, st->ctx->pool, st->ctx->coll, cat, out,
                                           panic, sizeof panic) != 0) {
                return values_to_fail(st, panic[0] ? panic : NULL);
            }
            st->changed = 1;
            return 0;
        }
        if (src->tag == VAL_NUMBER) {
            char dec[128];
            if (flat_number_format_decimal_string(src, dec, sizeof dec) != 0) {
                return values_to_fail(st, NULL);
            }
            if (strlen(dec) <= FLAT_INLINE_STR_MAX) {
                if (flat_value_set_str_inline(out, dec, strlen(dec)) != 0) {
                    return -1;
                }
            } else if (emit_pool_str(st->ctx->pool, out, dec, strlen(dec)) != 0) {
                return -1;
            }
            if (flat_slot_type_check_value(&st->target, out, st->ctx->pool, st->ctx->coll, cat, out,
                                           panic, sizeof panic) != 0) {
                return values_to_fail(st, panic[0] ? panic : NULL);
            }
            st->changed = 1;
            return 0;
        }
        return values_to_fail(st, NULL);
    }

    return values_to_fail(st, NULL);
}

static int convert_list(ValuesToState *st, uint32_t list_id, int depth, uint32_t *out_id);

/* Borrowed STR_REF/OBJECT from source tree need an extra retain before flat_list_build. */
static void values_to_pin_build_item(ValuesToState *st, const FlatValue *v) {
    if (!st || !v) {
        return;
    }
    if (v->tag == VAL_STR_REF || v->tag == VAL_OBJECT) {
        flat_value_retain(st->ctx->coll, st->ctx->pool, v);
    }
}

static int convert_list(ValuesToState *st, uint32_t list_id, int depth, uint32_t *out_id) {
    FlatValue items[FLAT_LIST_MAX_ELEMS];
    int len;
    int i;

    if (!st || !out_id) {
        return -1;
    }
    if (depth > FLAT_LIST_TYPED_MAX_RANK) {
        snprintf(st->ctx->panic_detail, sizeof st->ctx->panic_detail, "%s", "type.list.rank.max");
        return -1;
    }
    if (flat_list_len(st->ctx->coll, list_id, &len) != 0) {
        return -1;
    }
    if (len > FLAT_LIST_MAX_ELEMS) {
        snprintf(st->ctx->panic_detail, sizeof st->ctx->panic_detail, "%s", "list.max");
        return -1;
    }
    memset(items, 0, sizeof items);
    list_debug("values_to depth=%d list_id=%u len=%d", depth, (unsigned)list_id, len);
    for (i = 0; i < len; i++) {
        FlatValue el;
        if (flat_list_get(st->ctx->coll, list_id, i, &el) != 0) {
            return -1;
        }
        if (el.tag == VAL_LIST) {
            uint32_t child_id;
            if (convert_list(st, el.u.coll_id, depth + 1, &child_id) != 0) {
                int j;
                for (j = 0; j < i; j++) {
                    flat_value_release(st->ctx->coll, st->ctx->pool, &items[j]);
                }
                return -1;
            }
            if (flat_value_set_list(&items[i], child_id) != 0) {
                return -1;
            }
            flat_value_retain(st->ctx->coll, st->ctx->pool, &items[i]);
            continue;
        }
        {
            int rc = convert_leaf(st, &el, &items[i]);
            if (rc < 0) {
                int j;
                for (j = 0; j < i; j++) {
                    flat_value_release(st->ctx->coll, st->ctx->pool, &items[j]);
                }
                return -1;
            }
            if (rc > 0) {
                items[i] = el;
            }
            values_to_pin_build_item(st, &items[i]);
        }
    }
    if (flat_list_build(st->ctx->coll, st->ctx->pool, items, len, out_id) != 0) {
        int j;
        for (j = 0; j < len; j++) {
            flat_value_release(st->ctx->coll, st->ctx->pool, &items[j]);
        }
        return -1;
    }
    for (i = 0; i < len; i++) {
        flat_value_release(st->ctx->coll, st->ctx->pool, &items[i]);
    }
    return 0;
}

int flat_native_list_values_to(FlatNativeCtx *ctx) {
    FlatValue list_v;
    FlatSlotType target;
    int has_ty = 0;
    int mode_any = 0;
    ValuesToState st;
    uint32_t out_id;
    FlatListShapeInfo shape;
    int rank;
    int rc = -1;

    if (!ctx || !ctx->out || !ctx->io_status || !ctx->coll) {
        return -1;
    }
    memset(&list_v, 0, sizeof list_v);
    *ctx->io_status = FLAT_IO_OK;
    ctx->panic_detail[0] = '\0';

    if (flat_native_tag_reject_untagged_native(ctx, FLAT_NATIVE_LIST_VALUES_TO) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (!ctx->tag_expr || ctx->tag_expr[0] == '\0') {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.tag.arg");
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (flat_native_tag_resolve_values_to(ctx, FLAT_NATIVE_LIST_VALUES_TO, &target, &has_ty,
                                          &mode_any) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (!has_ty) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.tag.not_accepted");
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }

    if (flat_native_parse_expr(ctx, ctx->inner, &list_v) != 0 || list_v.tag != VAL_LIST) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.list.values_to.not_list");
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }

    rank = flat_list_value_rank(ctx->coll, list_v.u.coll_id);
    if (rank < 0 || rank > FLAT_LIST_TYPED_MAX_RANK) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.list.rank.max");
        *ctx->io_status = FLAT_IO_ERR;
        goto out;
    }

    memset(&st, 0, sizeof st);
    st.ctx = ctx;
    st.target = target;
    st.mode_any = mode_any;

    if (convert_list(&st, list_v.u.coll_id, 1, &out_id) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        goto out;
    }
    if (mode_any && st.changed == 0) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.list.values_to.none");
        *ctx->io_status = FLAT_IO_ERR;
        goto out;
    }

    memset(&shape, 0, sizeof shape);
    if (flat_list_shape_walk(ctx->coll, out_id, &shape) != 0) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "list.shape");
        *ctx->io_status = FLAT_IO_ERR;
        goto out;
    }
    flat_coll_shape_slot_write(ctx->coll, out_id, &shape);
    if (flat_value_set_list(ctx->out, out_id) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        goto out;
    }
    rc = 0;
out:
    return rc;
}