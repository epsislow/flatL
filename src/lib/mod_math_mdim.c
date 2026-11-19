#include "flat_list_shape.h"
#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_tag.h"
#include "flat_native_util.h"
#include "flat_number.h"
#include "flat_slot_type.h"
#include "flat_value.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int mdim_gather_subtree_ids(FlatCollectionPool *coll, uint32_t list_id, uint32_t *buf,
                                   int *n, int cap);

typedef int (*MdimBinOpFn)(double a, double b, double *out);

typedef struct MdimBcastLayout MdimBcastLayout;

static int mdim_debug_enabled(void) {
    static int inited;
    static int on;

    if (!inited) {
        const char *env = getenv("FLAT_MDIM_DEBUG");
        on = (env != NULL && env[0] != '\0' && strcmp(env, "0") != 0);
        inited = 1;
    }
    return on;
}

#define MDIM_DBG(...)                                                                              \
    do {                                                                                           \
        if (mdim_debug_enabled()) {                                                                \
            fprintf(stderr, "[mdim] " __VA_ARGS__);                                                \
            fflush(stderr);                                                                        \
        }                                                                                          \
    } while (0)

static void mdim_dump_shape(const char *label, const FlatListShapeInfo *info) {
    int i;

    if (!mdim_debug_enabled() || !info) {
        return;
    }
    MDIM_DBG("shape %s valid=%u rect=%u rank=%u dims_n=%u", label, info->valid, info->rect,
             info->rank, info->dims_n);
    for (i = 0; i < (int)info->dims_n && i < FLAT_LIST_TYPED_MAX_RANK; i++) {
        MDIM_DBG("  dims[%d]=%u", i, info->dims[i]);
    }
}

static void mdim_dump_list(FlatCollectionPool *coll, uint32_t id, int depth, int max_depth) {
    int len;
    int i;
    FlatValue el;

    if (!mdim_debug_enabled() || !coll) {
        return;
    }
    if (depth > max_depth) {
        fprintf(stderr, "[mdim] %*slist#%u ...\n", depth * 2, "", id);
        fflush(stderr);
        return;
    }
    if (flat_list_len(coll, id, &len) != 0) {
        fprintf(stderr, "[mdim] %*slist#%u (invalid)\n", depth * 2, "", id);
        fflush(stderr);
        return;
    }
    fprintf(stderr, "[mdim] %*slist#%u len=%d", depth * 2, "", id, len);
    for (i = 0; i < len && i < 8; i++) {
        if (flat_list_get(coll, id, i, &el) != 0) {
            fprintf(stderr, " ?");
            continue;
        }
        if (el.tag == VAL_NUMBER) {
            fprintf(stderr, " %.15g", flat_number_as_f64(&el));
        } else if (el.tag == VAL_LIST) {
            fprintf(stderr, "\n");
            mdim_dump_list(coll, el.u.coll_id, depth + 1, max_depth);
        } else {
            fprintf(stderr, " tag=%d", (int)el.tag);
        }
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}

static void mdim_dump_ix(int depth, const int *ix, int rank) {
    int i;

    if (!mdim_debug_enabled()) {
        return;
    }
    MDIM_DBG("  ix[%d..%d):", 0, depth + 1);
    for (i = 0; i < rank && i <= depth; i++) {
        MDIM_DBG(" %d", ix[i]);
    }
    MDIM_DBG("\n");
}

static int mdim_at2(FlatCollectionPool *coll, uint32_t mat_id, int row, int col, double *out);

static int split_two_args(char *inner, char **left, char **right) {
    int depth = 0;
    char *p;

    for (p = inner; *p; p++) {
        if (*p == '[' || *p == '(') {
            depth++;
        } else if (*p == ']' || *p == ')') {
            if (depth > 0) {
                depth--;
            }
        } else if (*p == ',' && depth == 0) {
            *p = '\0';
            *left = flat_native_trim(inner);
            *right = flat_native_trim(p + 1);
            return 0;
        }
    }
    return -1;
}

static void mdim_panic(FlatNativeCtx *ctx, const char *code) {
    if (!ctx) {
        return;
    }
    snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", code);
    *ctx->io_status = FLAT_IO_ERR;
}

typedef struct {
    FlatSlotType result_ty;
    int has_ty;
    int inferred_ty;
    FlatMdimTagRole role;
    int has_role;
} MdimCallTags;

static int mdim_native_allows_role(uint16_t native_id, FlatMdimTagRole role) {
    if (role == FLAT_MDIM_TAG_ROLE_NONE) {
        return 1;
    }
    switch ((FlatNativeId)native_id) {
    case FLAT_NATIVE_MATH_MDIM_SUM:
    case FLAT_NATIVE_MATH_MDIM_MIN:
    case FLAT_NATIVE_MATH_MDIM_MAX:
    case FLAT_NATIVE_MATH_MDIM_AVG:
        return 0;
    case FLAT_NATIVE_MATH_MDIM_DOT:
        return role == FLAT_MDIM_TAG_ROLE_VECTOR;
    case FLAT_NATIVE_MATH_MDIM_TRANSPOSE:
    case FLAT_NATIVE_MATH_MDIM_MATMUL:
    case FLAT_NATIVE_MATH_MDIM_DETERMINANT:
    case FLAT_NATIVE_MATH_MDIM_INVERSE:
        return role == FLAT_MDIM_TAG_ROLE_MATRIX;
    case FLAT_NATIVE_MATH_MDIM_ADD:
    case FLAT_NATIVE_MATH_MDIM_SUBTRACT:
    case FLAT_NATIVE_MATH_MDIM_MULTIPLY:
    case FLAT_NATIVE_MATH_MDIM_DIVIDE:
    case FLAT_NATIVE_MATH_MDIM_RESHAPE:
        return 1;
    default:
        return 0;
    }
}

static int mdim_resolve_call_tags(FlatNativeCtx *ctx, uint16_t native_id, MdimCallTags *tags) {
    if (!ctx || !tags) {
        return -1;
    }
    memset(tags, 0, sizeof *tags);
    tags->inferred_ty = 0;
    if (flat_native_tag_reject_untagged_native(ctx, native_id) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (flat_native_tag_resolve_mdim(ctx, native_id, &tags->result_ty, &tags->has_ty, &tags->role,
                                     &tags->has_role) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (tags->has_role && !mdim_native_allows_role(native_id, tags->role)) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.tag.not_accepted");
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    return 0;
}

static int mdim_kind_width(FlatSlotTypeKind k) {
    switch (k) {
    case FLAT_SLOT_TYPE_U8:
    case FLAT_SLOT_TYPE_S8:
        return 8;
    case FLAT_SLOT_TYPE_U16:
    case FLAT_SLOT_TYPE_S16:
        return 16;
    case FLAT_SLOT_TYPE_U32:
    case FLAT_SLOT_TYPE_S32:
        return 32;
    case FLAT_SLOT_TYPE_U64:
    case FLAT_SLOT_TYPE_S64:
        return 64;
    default:
        return 0;
    }
}

static int mdim_kind_signed(FlatSlotTypeKind k) {
    return k >= FLAT_SLOT_TYPE_S8 && k <= FLAT_SLOT_TYPE_S64;
}

static int mdim_merge_elem_kind(const FlatListShapeInfo *a, const FlatListShapeInfo *b,
                                FlatSlotTypeKind *kind_out, int *known_out) {
    FlatSlotTypeKind ka;
    FlatSlotTypeKind kb;

    if (!kind_out || !known_out || !a || !b) {
        return -1;
    }
    *known_out = 0;
    if (!a->elem_known && !b->elem_known) {
        return 0;
    }
    if (a->elem_known && !b->elem_known) {
        if (!flat_list_shape_kind_numeric(a->elem_kind)) {
            return -1;
        }
        *kind_out = a->elem_kind;
        *known_out = 1;
        return 0;
    }
    if (!a->elem_known && b->elem_known) {
        if (!flat_list_shape_kind_numeric(b->elem_kind)) {
            return -1;
        }
        *kind_out = b->elem_kind;
        *known_out = 1;
        return 0;
    }
    ka = a->elem_kind;
    kb = b->elem_kind;
    if (ka == kb) {
        *kind_out = ka;
        *known_out = 1;
        return 0;
    }
    if (!flat_list_shape_kind_numeric(ka) || !flat_list_shape_kind_numeric(kb)) {
        return -1;
    }
    if (ka == FLAT_SLOT_TYPE_F64 || kb == FLAT_SLOT_TYPE_F64 || ka == FLAT_SLOT_TYPE_F32 ||
        kb == FLAT_SLOT_TYPE_F32) {
        *kind_out = FLAT_SLOT_TYPE_F64;
        *known_out = 1;
        return 0;
    }
    if (mdim_kind_signed(ka) != mdim_kind_signed(kb)) {
        *kind_out = FLAT_SLOT_TYPE_F64;
        *known_out = 1;
        return 0;
    }
    {
        int wa = mdim_kind_width(ka);
        int wb = mdim_kind_width(kb);

        *kind_out = wa >= wb ? ka : kb;
        *known_out = 1;
        return 0;
    }
}

static void mdim_tags_apply_inferred(MdimCallTags *tags, FlatSlotTypeKind kind, int kind_known) {
    if (!tags || tags->has_ty || !kind_known) {
        return;
    }
    memset(&tags->result_ty, 0, sizeof tags->result_ty);
    tags->result_ty.kind = kind;
    tags->has_ty = 1;
    tags->inferred_ty = 1;
}

static void mdim_shape_apply_elem(FlatListShapeInfo *shape, FlatSlotTypeKind kind, int kind_known) {
    if (!shape || !kind_known) {
        return;
    }
    shape->elem_kind = kind;
    shape->elem_known = 1;
}

static int mdim_role_ok(const FlatListShapeInfo *info, const MdimCallTags *tags) {
    if (!info || !tags || !tags->has_role) {
        return 0;
    }
    if (tags->role == FLAT_MDIM_TAG_ROLE_VECTOR && info->rank != 1) {
        return -1;
    }
    if (tags->role == FLAT_MDIM_TAG_ROLE_MATRIX && info->rank != 2) {
        return -1;
    }
    return 0;
}

static int mdim_stage_scalar(FlatNativeCtx *ctx, double result, const MdimCallTags *tags) {
    FlatValue nv;
    FlatValue staged;
    char chk[64];
    const FlatSchemaCatalog *cat = ctx->bc ? &ctx->bc->schemas : NULL;

    if (!tags || !tags->has_ty) {
        return flat_value_set_number(ctx->out, result);
    }
    flat_value_set_number(&nv, result);
    staged = nv;
    if (flat_slot_type_check_value(&tags->result_ty, &nv, ctx->pool, ctx->coll, cat, &staged, chk,
                                   sizeof chk) != 0) {
        if (strncmp(chk, "type.assign.incompatible", 24) == 0) {
            snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.mdim.incompatible");
        } else {
            snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.calc.range");
        }
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    if (staged.tag == VAL_NUMBER) {
        return flat_value_set_number(ctx->out, flat_number_as_f64(&staged));
    }
    *ctx->out = staged;
    return 0;
}

static int mdim_rebuild_coerced_level(FlatNativeCtx *ctx, uint32_t src_id, const FlatSlotType *ty,
                                      uint32_t *out_id) {
    int len;
    int i;
    FlatValue el;
    FlatValue items[FLAT_LIST_MAX_ELEMS];
    const FlatSchemaCatalog *cat = ctx->bc ? &ctx->bc->schemas : NULL;

    if (!ctx || !out_id || flat_list_len(ctx->coll, src_id, &len) != 0 || len > FLAT_LIST_MAX_ELEMS) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (flat_list_get(ctx->coll, src_id, i, &el) != 0) {
            return -1;
        }
        if (el.tag == VAL_LIST) {
            uint32_t sub;
            if (mdim_rebuild_coerced_level(ctx, el.u.coll_id, ty, &sub) != 0) {
                return -1;
            }
            flat_value_set_list(&items[i], sub);
        } else if (el.tag == VAL_NUMBER) {
            FlatValue staged = el;
            char chk[64];
            if (flat_slot_type_check_value(ty, &el, ctx->pool, ctx->coll, cat, &staged, chk,
                                           sizeof chk) != 0) {
                if (strncmp(chk, "type.assign.incompatible", 24) == 0) {
                    snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s",
                             "type.mdim.incompatible");
                } else {
                    snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", "type.calc.range");
                }
                *ctx->io_status = FLAT_IO_ERR;
                return -1;
            }
            items[i] = staged;
        } else {
            return -1;
        }
    }
    {
        uint32_t avoid[4];
        int avoid_n = 0;

        avoid[avoid_n++] = src_id;
        return flat_list_build_disjoint(ctx->coll, ctx->pool, items, len, avoid, avoid_n, out_id);
    }
}

static int mdim_apply_result_list(FlatNativeCtx *ctx, uint32_t src_id, const MdimCallTags *tags,
                                  FlatSlotTypeKind src_elem, int src_elem_known, uint32_t *out_id) {
    if (!tags || !tags->has_ty) {
        *out_id = src_id;
        return 0;
    }
    if (tags->result_ty.kind == FLAT_SLOT_TYPE_F64) {
        if (tags->inferred_ty) {
            *out_id = src_id;
            return 0;
        }
        if (src_elem_known && src_elem == FLAT_SLOT_TYPE_F64) {
            *out_id = src_id;
            return 0;
        }
        return mdim_rebuild_coerced_level(ctx, src_id, &tags->result_ty, out_id);
    }
    if (flat_list_shape_kind_numeric(tags->result_ty.kind)) {
        return mdim_rebuild_coerced_level(ctx, src_id, &tags->result_ty, out_id);
    }
    *out_id = src_id;
    return 0;
}

static int mdim_flatten(FlatCollectionPool *coll, uint32_t list_id, double *buf, int cap,
                        int *count) {
    int len;
    int i;
    FlatValue el;

    if (!coll || !buf || !count || flat_list_len(coll, list_id, &len) != 0) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (flat_list_get(coll, list_id, i, &el) != 0) {
            return -1;
        }
        if (el.tag == VAL_LIST) {
            if (mdim_flatten(coll, el.u.coll_id, buf, cap, count) != 0) {
                return -1;
            }
        } else if (el.tag == VAL_NUMBER) {
            if (*count >= cap) {
                return -1;
            }
            buf[(*count)++] = flat_number_as_f64(&el);
        } else {
            return -1;
        }
    }
    return 0;
}

static int mdim_build_from_shape(FlatNativeCtx *ctx, const double *vals, int *pos,
                                 const uint16_t *dims, int depth, int rank, uint32_t *out_id) {
    int len;
    int i;
    FlatValue items[FLAT_LIST_MAX_ELEMS];
    uint32_t avoid[FLAT_LIST_MAX_ELEMS * 2];
    int avoid_n = 0;

    if (depth < 0 || depth >= rank) {
        return -1;
    }
    len = (int)dims[depth];
    if (len > FLAT_LIST_MAX_ELEMS) {
        return -1;
    }
    if (depth == rank - 1) {
        for (i = 0; i < len; i++) {
            flat_number_init_legacy_double(&items[i], vals[(*pos)++]);
        }
        if (rank > 2) {
            return flat_list_build_disjoint(ctx->coll, ctx->pool, items, len, avoid, avoid_n,
                                            out_id);
        }
        return flat_list_build(ctx->coll, ctx->pool, items, len, out_id);
    }
    for (i = 0; i < len; i++) {
        uint32_t child;
        if (mdim_build_from_shape(ctx, vals, pos, dims, depth + 1, rank, &child) != 0) {
            return -1;
        }
        flat_value_set_list(&items[i], child);
        if (rank > 2 &&
            mdim_gather_subtree_ids(ctx->coll, child, avoid, &avoid_n,
                                    (int)(sizeof avoid / sizeof avoid[0])) != 0) {
            return -1;
        }
        flat_value_retain(ctx->coll, ctx->pool, &items[i]);
    }
    if (rank > 2) {
        int rc = flat_list_build_disjoint(ctx->coll, ctx->pool, items, len, avoid, avoid_n, out_id);
        for (i = 0; i < len; i++) {
            flat_value_release(ctx->coll, ctx->pool, &items[i]);
        }
        return rc;
    }
    if (flat_list_build(ctx->coll, ctx->pool, items, len, out_id) != 0) {
        for (i = 0; i < len; i++) {
            flat_value_release(ctx->coll, ctx->pool, &items[i]);
        }
        return -1;
    }
    for (i = 0; i < len; i++) {
        flat_value_release(ctx->coll, ctx->pool, &items[i]);
    }
    return 0;
}

struct MdimBcastLayout {
    int rank;
    int a_rank;
    int b_rank;
    uint16_t out_dims[FLAT_LIST_TYPED_MAX_RANK];
    uint16_t a_dims[FLAT_LIST_TYPED_MAX_RANK];
    uint16_t b_dims[FLAT_LIST_TYPED_MAX_RANK];
};

static void mdim_dump_layout(const MdimBcastLayout *lo) {
    int i;

    if (!mdim_debug_enabled() || !lo) {
        return;
    }
    MDIM_DBG("bcast rank=%d a_rank=%d b_rank=%d", lo->rank, lo->a_rank, lo->b_rank);
    for (i = 0; i < lo->rank; i++) {
        MDIM_DBG("  axis %d: out=%u a=%u b=%u", i, lo->out_dims[i], lo->a_dims[i], lo->b_dims[i]);
    }
}

static int walk_leaves_numeric(FlatCollectionPool *coll, uint32_t list_id) {
    int len;
    int i;
    FlatValue el;

    if (!coll || flat_list_len(coll, list_id, &len) != 0) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (flat_list_get(coll, list_id, i, &el) != 0) {
            return -1;
        }
        if (el.tag == VAL_LIST) {
            int rc = walk_leaves_numeric(coll, el.u.coll_id);
            if (rc != 0) {
                return rc;
            }
        } else if (el.tag != VAL_NUMBER) {
            return -2;
        }
    }
    return 0;
}

static void mdim_stamp_shape(FlatCollectionPool *coll, uint32_t list_id,
                             const FlatListShapeInfo *info) {
    FlatListShapeInfo st;

    if (!coll || !info) {
        return;
    }
    st = *info;
    st.valid = 1;
    st.rect = 1;
    st.source = FLAT_LIST_SHAPE_SRC_WALK;
    flat_coll_shape_slot_write(coll, list_id, &st);
}

static int mdim_operand_info(FlatNativeCtx *ctx, const FlatValue *list_v, int require_rect,
                             FlatListShapeInfo *info_out) {
    int rc;

    if (!ctx || !list_v || list_v->tag != VAL_LIST || !info_out) {
        return -1;
    }
    if (flat_list_shape_query(ctx->coll, list_v->u.coll_id, info_out) != 0) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        return -1;
    }
    if (require_rect && !info_out->rect) {
        mdim_panic(ctx, "type.mdim.not_rect");
        return -1;
    }
    rc = walk_leaves_numeric(ctx->coll, list_v->u.coll_id);
    if (rc == -2) {
        mdim_panic(ctx, "type.mdim.not_numeric");
        return -1;
    }
    if (rc != 0) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        return -1;
    }
    return 0;
}

static int mdim_parse_list(FlatNativeCtx *ctx, const char *expr, FlatValue *out) {
    if (flat_native_parse_rhs(ctx, expr, out) != 0 || out->tag != VAL_LIST) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        return -1;
    }
    return 0;
}

static int mdim_gather_subtree_ids(FlatCollectionPool *coll, uint32_t list_id, uint32_t *buf,
                                   int *n, int cap) {
    int len;
    int i;
    FlatValue el;

    if (!coll || !buf || !n || *n >= cap) {
        return -1;
    }
    for (i = 0; i < *n; i++) {
        if (buf[i] == list_id) {
            return 0;
        }
    }
    buf[(*n)++] = list_id;
    if (flat_list_len(coll, list_id, &len) != 0) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (flat_list_get(coll, list_id, i, &el) != 0) {
            continue;
        }
        if (el.tag == VAL_LIST &&
            mdim_gather_subtree_ids(coll, el.u.coll_id, buf, n, cap) != 0) {
            return -1;
        }
    }
    return 0;
}

static int mdim_gather_from_built(FlatNativeCtx *ctx, const FlatValue *built, int len,
                                  uint32_t *buf, int *n, int cap) {
    int i;

    if (!ctx || !buf || !n) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (built[i].tag != VAL_LIST) {
            continue;
        }
        if (mdim_gather_subtree_ids(ctx->coll, built[i].u.coll_id, buf, n, cap) != 0) {
            return -1;
        }
    }
    return 0;
}

static int mdim_rehome_list(FlatNativeCtx *ctx, const FlatValue *items, int len, uint32_t *out_id) {
    int i;
    int blen;

    if (!ctx || !out_id) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (items[i].tag == VAL_LIST && items[i].u.coll_id == *out_id) {
            uint32_t tmp;
            if (flat_list_len(ctx->coll, *out_id, &blen) != 0 || blen <= 0) {
                return -1;
            }
            if (flat_list_slice_copy(ctx->coll, ctx->pool, *out_id, 0, blen, &tmp) != 0) {
                return -1;
            }
            if (tmp == *out_id) {
                return -1;
            }
            *out_id = tmp;
            return 0;
        }
    }
    return 0;
}

static int mdim_dup_list(FlatNativeCtx *ctx, uint32_t src_id, uint32_t *out_id) {
    int len;
    int i;
    FlatValue el;
    FlatValue items[FLAT_LIST_MAX_ELEMS];

    if (!ctx || !out_id || flat_list_len(ctx->coll, src_id, &len) != 0) {
        return -1;
    }
    if (len > FLAT_LIST_MAX_ELEMS) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (flat_list_get(ctx->coll, src_id, i, &el) != 0) {
            return -1;
        }
        if (el.tag == VAL_LIST) {
            uint32_t sub;
            if (mdim_dup_list(ctx, el.u.coll_id, &sub) != 0) {
                return -1;
            }
            flat_value_set_list(&items[i], sub);
        } else if (el.tag == VAL_NUMBER) {
            items[i] = el;
        } else {
            return -1;
        }
    }
    return flat_list_build(ctx->coll, ctx->pool, items, len, out_id);
}

static int mdim_dup_list_disjoint(FlatNativeCtx *ctx, uint32_t src_id, uint32_t *avoid, int *avoid_n,
                                  int avoid_cap, uint32_t *out_id) {
    int len;
    int i;
    FlatValue el;
    FlatValue items[FLAT_LIST_MAX_ELEMS];

    if (!ctx || !out_id || !avoid || !avoid_n || flat_list_len(ctx->coll, src_id, &len) != 0) {
        return -1;
    }
    if (len > FLAT_LIST_MAX_ELEMS) {
        return -1;
    }
    if (mdim_gather_subtree_ids(ctx->coll, src_id, avoid, avoid_n, avoid_cap) != 0) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (flat_list_get(ctx->coll, src_id, i, &el) != 0) {
            return -1;
        }
        if (el.tag == VAL_LIST) {
            uint32_t sub;

            if (mdim_dup_list_disjoint(ctx, el.u.coll_id, avoid, avoid_n, avoid_cap, &sub) != 0) {
                return -1;
            }
            flat_value_set_list(&items[i], sub);
            if (mdim_gather_subtree_ids(ctx->coll, sub, avoid, avoid_n, avoid_cap) != 0) {
                return -1;
            }
        } else if (el.tag == VAL_NUMBER) {
            items[i] = el;
        } else {
            return -1;
        }
    }
    if (flat_list_build_disjoint(ctx->coll, ctx->pool, items, len, avoid, *avoid_n, out_id) != 0) {
        return -1;
    }
    return mdim_rehome_list(ctx, items, len, out_id);
}

static int mdim_at1(FlatCollectionPool *coll, uint32_t vec_id, int i, double *out) {
    FlatValue el;

    if (flat_list_get(coll, vec_id, i, &el) != 0 || el.tag != VAL_NUMBER) {
        return -1;
    }
    *out = flat_number_as_f64(&el);
    return 0;
}

static int mdim_bcast_pick(uint16_t dim, int out_ix) {
    if (dim == 1) {
        return 0;
    }
    return out_ix;
}

static int mdim_bcast_layout(FlatNativeCtx *ctx, const FlatListShapeInfo *a,
                             const FlatListShapeInfo *b, MdimBcastLayout *lo) {
    int ra;
    int rb;
    int r;
    int pa;
    int pb;
    int i;

    if (!a || !b || !lo) {
        return -1;
    }
    ra = a->rank;
    rb = b->rank;
    if (ra <= 0 || rb <= 0) {
        return -1;
    }
    r = ra > rb ? ra : rb;
    if (r > FLAT_LIST_TYPED_MAX_RANK) {
        return -1;
    }
    pa = r - ra;
    pb = r - rb;
    lo->rank = r;
    lo->a_rank = ra;
    lo->b_rank = rb;
    for (i = 0; i < r; i++) {
        uint16_t da = i < pa ? (uint16_t)1 : a->dims[i - pa];
        uint16_t db = i < pb ? (uint16_t)1 : b->dims[i - pb];

        if (da == db || da == 1 || db == 1) {
            lo->a_dims[i] = da;
            lo->b_dims[i] = db;
            lo->out_dims[i] = da == 1 ? db : da;
        } else {
            mdim_panic(ctx, "type.mdim.broadcast");
            return -1;
        }
    }
    return 0;
}

static int mdim_sublist_bcast(FlatCollectionPool *coll, uint32_t list_id, const MdimBcastLayout *lo,
                              int depth, int out_ix, int is_b, uint32_t *sub_out) {
    int op_rank = is_b ? lo->b_rank : lo->a_rank;
    int ax = depth - (lo->rank - op_rank);
    const uint16_t *dims = is_b ? lo->b_dims : lo->a_dims;
    int pi;
    FlatValue el;

    if (!sub_out || !lo) {
        return -1;
    }
    if (ax < 0) {
        *sub_out = list_id;
        return 0;
    }
    pi = mdim_bcast_pick(dims[depth], out_ix);
    if (flat_list_get(coll, list_id, pi, &el) != 0 || el.tag != VAL_LIST) {
        MDIM_DBG("sublist fail depth=%d out_ix=%d id=%u dim=%u pi=%d tag=%d\n", depth, out_ix,
                 list_id, dims[depth], pi, (int)el.tag);
        return -1;
    }
    *sub_out = el.u.coll_id;
    return 0;
}

static int mdim_read_num_at(FlatNativeCtx *ctx, uint32_t list_id, const MdimBcastLayout *lo,
                            const int *ix, int depth, int is_b, double *out) {
    const uint16_t *dims = is_b ? lo->b_dims : lo->a_dims;
    int r = lo->rank;
    int d;
    uint32_t cur = list_id;

    for (d = depth; d < r - 1; d++) {
        int pi = mdim_bcast_pick(dims[d], ix[d]);
        FlatValue el;

        if (flat_list_get(ctx->coll, cur, pi, &el) != 0 || el.tag != VAL_LIST) {
            MDIM_DBG("read navigate fail d=%d pi=%d cur=%u tag=%d\n", d, pi, cur, (int)el.tag);
            return -1;
        }
        cur = el.u.coll_id;
    }
    {
        int pi = mdim_bcast_pick(dims[r - 1], ix[r - 1]);
        if (mdim_at1(ctx->coll, cur, pi, out) != 0) {
            FlatValue el;
            int tag = -1;
            if (flat_list_get(ctx->coll, cur, pi, &el) == 0) {
                tag = (int)el.tag;
            }
            MDIM_DBG("read leaf fail depth=%d cur=%u pi=%d leaf_tag=%d", depth, cur, pi, tag);
            mdim_dump_ix(depth, ix, r);
            fprintf(stderr, "\n");
            fflush(stderr);
            return -1;
        }
        return 0;
    }
}

static int mdim_build_bcast_level(FlatNativeCtx *ctx, const MdimBcastLayout *lo, int depth,
                                  uint32_t a_id, uint32_t b_id, int *ix, MdimBinOpFn op,
                                  uint32_t *out_id) {
    int r = lo->rank;

    if (depth == r - 1) {
        int len = lo->out_dims[depth];
        FlatValue items[FLAT_LIST_MAX_ELEMS];
        int i;

        if (len > FLAT_LIST_MAX_ELEMS) {
            return -1;
        }
        for (i = 0; i < len; i++) {
            double da;
            double db;
            double dr;
            int orc;

            ix[depth] = i;
            if (mdim_read_num_at(ctx, a_id, lo, ix, depth, 0, &da) != 0 ||
                mdim_read_num_at(ctx, b_id, lo, ix, depth, 1, &db) != 0) {
                mdim_panic(ctx, "type.mdim.not_numeric");
                return -1;
            }
            orc = op(da, db, &dr);
            if (orc == -2) {
                mdim_panic(ctx, "math.mdim.divzero");
                return -1;
            }
            if (orc != 0) {
                return -1;
            }
            flat_number_init_legacy_double(&items[i], dr);
        }
        if (lo->rank > 2) {
            uint32_t avoid[FLAT_LIST_MAX_ELEMS + 8];
            int avoid_n = 0;
            uint32_t leaf_id;

            if (mdim_gather_subtree_ids(ctx->coll, a_id, avoid, &avoid_n,
                                        (int)(sizeof avoid / sizeof avoid[0])) != 0 ||
                mdim_gather_subtree_ids(ctx->coll, b_id, avoid, &avoid_n,
                                        (int)(sizeof avoid / sizeof avoid[0])) != 0 ||
                flat_list_build_disjoint(ctx->coll, ctx->pool, items, len, avoid, avoid_n,
                                         &leaf_id) != 0) {
                return -1;
            }
            return mdim_dup_list_disjoint(ctx, leaf_id, avoid, &avoid_n,
                                          (int)(sizeof avoid / sizeof avoid[0]), out_id);
        }
        return flat_list_build(ctx->coll, ctx->pool, items, len, out_id);
    }
    {
        int len = lo->out_dims[depth];
        FlatValue built[FLAT_LIST_MAX_ELEMS];
        int i;

        if (len > FLAT_LIST_MAX_ELEMS) {
            return -1;
        }
        memset(built, 0, sizeof built);
        {
            uint32_t level_avoid[FLAT_LIST_MAX_ELEMS * 4];
            int level_avoid_n = 0;

            if (lo->rank > 2) {
                if (mdim_gather_subtree_ids(ctx->coll, a_id, level_avoid, &level_avoid_n,
                                            (int)(sizeof level_avoid / sizeof level_avoid[0])) != 0 ||
                    mdim_gather_subtree_ids(ctx->coll, b_id, level_avoid, &level_avoid_n,
                                            (int)(sizeof level_avoid / sizeof level_avoid[0])) !=
                        0) {
                    return -1;
                }
            }
            for (i = 0; i < len; i++) {
            uint32_t child;
            uint32_t child_dup;
            uint32_t sub_a;
            uint32_t sub_b;

            int d;
            ix[depth] = i;
            for (d = depth + 1; d < lo->rank; d++) {
                ix[d] = 0;
            }
            if (mdim_sublist_bcast(ctx->coll, a_id, lo, depth, i, 0, &sub_a) != 0 ||
                mdim_sublist_bcast(ctx->coll, b_id, lo, depth, i, 1, &sub_b) != 0) {
                MDIM_DBG("sublist bcast depth=%d i=%d a_id=%u b_id=%u\n", depth, i, a_id, b_id);
                return -1;
            }
            MDIM_DBG("mid depth=%d i=%d sub_a=%u sub_b=%u", depth, i, sub_a, sub_b);
            mdim_dump_ix(depth, ix, lo->rank);
            if (mdim_build_bcast_level(ctx, lo, depth + 1, sub_a, sub_b, ix, op, &child) != 0) {
                MDIM_DBG("child build fail depth=%d i=%d\n", depth, i);
                return -1;
            }
            MDIM_DBG(" child=%u", child);
            mdim_dump_list(ctx->coll, child, 0, 3);
            if (depth + 1 == lo->rank - 1) {
                if (lo->rank > 2) {
                    if (mdim_dup_list_disjoint(ctx, child, level_avoid, &level_avoid_n,
                                               (int)(sizeof level_avoid / sizeof level_avoid[0]),
                                               &child_dup) != 0) {
                        return -1;
                    }
                    MDIM_DBG(" row-dup child=%u dup=%u\n", child, child_dup);
                } else {
                    int child_len;
                    if (flat_list_len(ctx->coll, child, &child_len) != 0 || child_len <= 0 ||
                        child_len > FLAT_LIST_MAX_ELEMS) {
                        return -1;
                    }
                    if (flat_list_slice_copy(ctx->coll, ctx->pool, child, 0, child_len,
                                            &child_dup) != 0) {
                        return -1;
                    }
                    MDIM_DBG(" slice-row child=%u dup=%u len=%d\n", child, child_dup, child_len);
                }
            } else if (lo->rank > 2) {
                if (mdim_gather_subtree_ids(ctx->coll, child, level_avoid, &level_avoid_n,
                                            (int)(sizeof level_avoid / sizeof level_avoid[0])) != 0) {
                    return -1;
                }
                child_dup = child;
                MDIM_DBG(" block-use child=%u\n", child);
            } else if (mdim_dup_list(ctx, child, &child_dup) != 0) {
                MDIM_DBG(" dup fail child=%u\n", child);
                return -1;
            } else {
                MDIM_DBG(" dup-tree child=%u -> dup=%u\n", child, child_dup);
            }
            flat_value_set_list(&built[i], child_dup);
            flat_value_retain(ctx->coll, ctx->pool, &built[i]);
        }
            if (lo->rank > 2) {
                if (mdim_gather_from_built(ctx, built, len, level_avoid, &level_avoid_n,
                                           (int)(sizeof level_avoid / sizeof level_avoid[0])) !=
                    0) {
                    for (i = 0; i < len; i++) {
                        flat_value_release(ctx->coll, ctx->pool, &built[i]);
                    }
                    return -1;
                }
                if (flat_list_build_disjoint(ctx->coll, ctx->pool, built, len, level_avoid,
                                             level_avoid_n, out_id) != 0) {
                    MDIM_DBG("flat_list_build_disjoint fail depth=%d len=%d\n", depth, len);
                    for (i = 0; i < len; i++) {
                        flat_value_release(ctx->coll, ctx->pool, &built[i]);
                    }
                    return -1;
                }
                if (mdim_rehome_list(ctx, built, len, out_id) != 0) {
                    for (i = 0; i < len; i++) {
                        flat_value_release(ctx->coll, ctx->pool, &built[i]);
                    }
                    return -1;
                }
            }
        }
        if (lo->rank <= 2 && flat_list_build(ctx->coll, ctx->pool, built, len, out_id) != 0) {
            MDIM_DBG("flat_list_build fail depth=%d len=%d\n", depth, len);
            for (i = 0; i < len; i++) {
                flat_value_release(ctx->coll, ctx->pool, &built[i]);
            }
            return -1;
        }
        MDIM_DBG("built depth=%d out=%u\n", depth, *out_id);
        mdim_dump_list(ctx->coll, *out_id, 0, 3);
        for (i = 0; i < len; i++) {
            flat_value_release(ctx->coll, ctx->pool, &built[i]);
        }
        return 0;
    }
}

static int mdim_map_binary_bcast(FlatNativeCtx *ctx, uint32_t a_id, uint32_t b_id,
                                 const FlatListShapeInfo *ia, const FlatListShapeInfo *ib,
                                 MdimBinOpFn op, uint32_t *out_id, FlatListShapeInfo *out_info) {
    MdimBcastLayout lo;
    int ix[FLAT_LIST_TYPED_MAX_RANK];

    memset(ix, 0, sizeof ix);
    mdim_dump_shape("operand a", ia);
    mdim_dump_shape("operand b", ib);
    if (mdim_bcast_layout(ctx, ia, ib, &lo) != 0) {
        return -1;
    }
    mdim_dump_layout(&lo);
    MDIM_DBG("map_binary a_id=%u b_id=%u\n", a_id, b_id);
    mdim_dump_list(ctx->coll, a_id, 0, 3);
    if (mdim_build_bcast_level(ctx, &lo, 0, a_id, b_id, ix, op, out_id) != 0) {
        return -1;
    }
    memset(out_info, 0, sizeof *out_info);
    out_info->rank = (unsigned char)lo.rank;
    out_info->dims_n = (unsigned char)lo.rank;
    out_info->rect = 1;
    memcpy(out_info->dims, lo.out_dims, (size_t)lo.rank * sizeof out_info->dims[0]);
    return 0;
}

static int mdim_op_add(double a, double b, double *out) {
    *out = a + b;
    return 0;
}

static int mdim_op_sub(double a, double b, double *out) {
    *out = a - b;
    return 0;
}

static int mdim_op_mul(double a, double b, double *out) {
    *out = a * b;
    return 0;
}

static int mdim_op_div(double a, double b, double *out) {
    if (b == 0.0) {
        return -2;
    }
    *out = a / b;
    return 0;
}

static int mdim_binary_native(FlatNativeCtx *ctx, uint16_t native_id, MdimBinOpFn op) {
    char inner_buf[512];
    char left_buf[384];
    char right_buf[384];
    char *left;
    char *right;
    FlatValue a_v;
    FlatValue b_v;
    FlatListShapeInfo ia;
    FlatListShapeInfo ib;
    FlatListShapeInfo out_shape;
    uint32_t out_id;
    uint32_t final_id;
    MdimCallTags tags;
    int rc = -1;

    memset(&a_v, 0, sizeof a_v);
    memset(&b_v, 0, sizeof b_v);
    if (!ctx->out || !ctx->io_status || !ctx->inner) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    *ctx->io_status = FLAT_IO_OK;
    if (mdim_resolve_call_tags(ctx, native_id, &tags) != 0) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        return -1;
    }
    snprintf(left_buf, sizeof left_buf, "%s", left);
    snprintf(right_buf, sizeof right_buf, "%s", right);
    if (mdim_parse_list(ctx, left_buf, &a_v) != 0) {
        goto out;
    }
    if (mdim_parse_list(ctx, right_buf, &b_v) != 0) {
        goto out;
    }
    if (mdim_operand_info(ctx, &a_v, 1, &ia) != 0 || mdim_operand_info(ctx, &b_v, 1, &ib) != 0) {
        goto out;
    }
    if (mdim_role_ok(&ia, &tags) != 0 || mdim_role_ok(&ib, &tags) != 0) {
        mdim_panic(ctx, "type.mdim.tag_shape");
        goto out;
    }
    FlatSlotTypeKind out_elem;
    int out_elem_known;

    if (mdim_merge_elem_kind(&ia, &ib, &out_elem, &out_elem_known) != 0) {
        mdim_panic(ctx, "type.mdim.incompatible");
        goto out;
    }
    mdim_tags_apply_inferred(&tags, out_elem, out_elem_known);
    if (mdim_map_binary_bcast(ctx, a_v.u.coll_id, b_v.u.coll_id, &ia, &ib, op, &out_id,
                              &out_shape) != 0) {
        if (ctx->panic_detail[0] == '\0') {
            mdim_panic(ctx, "type.mdim.dim_mismatch");
        }
        goto out;
    }
    if (tags.has_ty) {
        mdim_shape_apply_elem(&out_shape, tags.result_ty.kind, 1);
    }
    mdim_stamp_shape(ctx->coll, out_id, &out_shape);
    if (mdim_apply_result_list(ctx, out_id, &tags, out_elem, out_elem_known, &final_id) != 0) {
        goto out;
    }
    if (final_id != out_id) {
        mdim_stamp_shape(ctx->coll, final_id, &out_shape);
    }
    if (flat_value_set_list(ctx->out, final_id) != 0) {
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &a_v);
    flat_native_parse_rhs_release(ctx, &b_v);
    return rc;
}

typedef struct {
    int have;
    double acc;
    int is_min;
    int is_max;
} MdimReduceAcc;

static int mdim_reduce_visit(FlatCollectionPool *coll, uint32_t list_id, MdimReduceAcc *acc) {
    int len;
    int i;
    FlatValue el;

    if (!coll || !acc || flat_list_len(coll, list_id, &len) != 0) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (flat_list_get(coll, list_id, i, &el) != 0) {
            return -1;
        }
        if (el.tag == VAL_LIST) {
            if (mdim_reduce_visit(coll, el.u.coll_id, acc) != 0) {
                return -1;
            }
        } else if (el.tag == VAL_NUMBER) {
            double d = flat_number_as_f64(&el);
            if (!acc->have) {
                acc->acc = d;
                acc->have = 1;
            } else if (acc->is_min) {
                if (d < acc->acc) {
                    acc->acc = d;
                }
            } else if (acc->is_max) {
                if (d > acc->acc) {
                    acc->acc = d;
                }
            } else {
                acc->acc += d;
            }
        } else {
            return -2;
        }
    }
    return 0;
}

static int mdim_count_numeric_leaves(FlatCollectionPool *coll, uint32_t list_id, int *count_out) {
    int len;
    int i;
    FlatValue el;

    if (!coll || !count_out || flat_list_len(coll, list_id, &len) != 0) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (flat_list_get(coll, list_id, i, &el) != 0) {
            return -1;
        }
        if (el.tag == VAL_LIST) {
            if (mdim_count_numeric_leaves(coll, el.u.coll_id, count_out) != 0) {
                return -1;
            }
        } else if (el.tag == VAL_NUMBER) {
            (*count_out)++;
        } else {
            return -2;
        }
    }
    return 0;
}

static int mdim_reduce_native(FlatNativeCtx *ctx, uint16_t native_id, int kind) {
    FlatValue list_v;
    FlatListShapeInfo info;
    MdimReduceAcc acc;
    MdimCallTags tags;
    int count = 0;
    int rc = -1;

    memset(&list_v, 0, sizeof list_v);
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    *ctx->io_status = FLAT_IO_OK;
    if (mdim_resolve_call_tags(ctx, native_id, &tags) != 0) {
        return -1;
    }
    if (mdim_parse_list(ctx, ctx->inner, &list_v) != 0) {
        goto out;
    }
    if (mdim_operand_info(ctx, &list_v, 1, &info) != 0) {
        goto out;
    }
    mdim_tags_apply_inferred(&tags, info.elem_kind, info.elem_known);
    memset(&acc, 0, sizeof acc);
    acc.is_min = (kind == 2);
    acc.is_max = (kind == 3);
    {
        int vrc = mdim_reduce_visit(ctx->coll, list_v.u.coll_id, &acc);
        if (vrc == -2) {
            mdim_panic(ctx, "type.mdim.not_numeric");
            goto out;
        }
        if (vrc != 0) {
            mdim_panic(ctx, "type.mdim.dim_mismatch");
            goto out;
        }
    }
    if (!acc.have) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        goto out;
    }
    if (kind == 0 || kind == 2 || kind == 3) {
        if (mdim_stage_scalar(ctx, acc.acc, &tags) != 0) {
            goto out;
        }
        rc = 0;
        goto out;
    }
    if (mdim_count_numeric_leaves(ctx->coll, list_v.u.coll_id, &count) != 0 || count <= 0) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        goto out;
    }
    if (mdim_stage_scalar(ctx, acc.acc / (double)count, &tags) != 0) {
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &list_v);
    return rc;
}

static int mdim_at2(FlatCollectionPool *coll, uint32_t mat_id, int row, int col, double *out) {
    FlatValue row_v;
    FlatValue cell;
    int got_row;

    if (flat_list_get(coll, mat_id, row, &row_v) != 0 || row_v.tag != VAL_LIST) {
        return -1;
    }
    if (flat_list_len(coll, row_v.u.coll_id, &got_row) != 0) {
        return -1;
    }
    if (col < 0 || col >= got_row) {
        return -1;
    }
    if (flat_list_get(coll, row_v.u.coll_id, col, &cell) != 0 || cell.tag != VAL_NUMBER) {
        return -1;
    }
    *out = flat_number_as_f64(&cell);
    return 0;
}

static int mdim_dot_native(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char left_buf[384];
    char right_buf[384];
    char *left;
    char *right;
    FlatValue a_v;
    FlatValue b_v;
    FlatListShapeInfo ia;
    FlatListShapeInfo ib;
    MdimCallTags tags;
    int i;
    int n;
    int n_b;
    double sum = 0.0;
    int rc = -1;

    memset(&a_v, 0, sizeof a_v);
    memset(&b_v, 0, sizeof b_v);
    if (!ctx->out || !ctx->io_status || !ctx->inner) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    *ctx->io_status = FLAT_IO_OK;
    if (mdim_resolve_call_tags(ctx, FLAT_NATIVE_MATH_MDIM_DOT, &tags) != 0) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        return -1;
    }
    snprintf(left_buf, sizeof left_buf, "%s", left);
    snprintf(right_buf, sizeof right_buf, "%s", right);
    if (mdim_parse_list(ctx, left_buf, &a_v) != 0) {
        goto out;
    }
    if (mdim_parse_list(ctx, right_buf, &b_v) != 0) {
        goto out;
    }
    if (mdim_operand_info(ctx, &a_v, 1, &ia) != 0 || mdim_operand_info(ctx, &b_v, 1, &ib) != 0) {
        goto out;
    }
    if (mdim_role_ok(&ia, &tags) != 0 || mdim_role_ok(&ib, &tags) != 0) {
        mdim_panic(ctx, "type.mdim.tag_shape");
        goto out;
    }
    {
        FlatSlotTypeKind out_elem;
        int out_elem_known;

        if (mdim_merge_elem_kind(&ia, &ib, &out_elem, &out_elem_known) != 0) {
            mdim_panic(ctx, "type.mdim.incompatible");
            goto out;
        }
        mdim_tags_apply_inferred(&tags, out_elem, out_elem_known);
    }
    if (ia.rank != 1 || ib.rank != 1) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        goto out;
    }
    if (flat_list_len(ctx->coll, a_v.u.coll_id, &n) != 0 ||
        flat_list_len(ctx->coll, b_v.u.coll_id, &n_b) != 0 || n != n_b || n <= 0) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        goto out;
    }
    for (i = 0; i < n; i++) {
        FlatValue ea;
        FlatValue eb;
        double da;
        double db;
        if (flat_list_get(ctx->coll, a_v.u.coll_id, i, &ea) != 0 ||
            flat_list_get(ctx->coll, b_v.u.coll_id, i, &eb) != 0) {
            goto out;
        }
        if (ea.tag != VAL_NUMBER || eb.tag != VAL_NUMBER) {
            mdim_panic(ctx, "type.mdim.not_numeric");
            goto out;
        }
        da = flat_number_as_f64(&ea);
        db = flat_number_as_f64(&eb);
        sum += da * db;
    }
    if (mdim_stage_scalar(ctx, sum, &tags) != 0) {
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &a_v);
    flat_native_parse_rhs_release(ctx, &b_v);
    return rc;
}

static int mdim_transpose_native(FlatNativeCtx *ctx) {
    FlatValue m_v;
    FlatListShapeInfo info;
    MdimCallTags tags;
    int rows;
    int cols;
    int r;
    int c;
    uint32_t out_id;
    uint32_t final_id;
    int rc = -1;

    memset(&m_v, 0, sizeof m_v);
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    *ctx->io_status = FLAT_IO_OK;
    if (mdim_resolve_call_tags(ctx, FLAT_NATIVE_MATH_MDIM_TRANSPOSE, &tags) != 0) {
        return -1;
    }
    if (mdim_parse_list(ctx, ctx->inner, &m_v) != 0) {
        goto out;
    }
    if (mdim_operand_info(ctx, &m_v, 1, &info) != 0) {
        goto out;
    }
    if (mdim_role_ok(&info, &tags) != 0) {
        mdim_panic(ctx, "type.mdim.tag_shape");
        goto out;
    }
    mdim_tags_apply_inferred(&tags, info.elem_kind, info.elem_known);
    if (info.rank != 2) {
        mdim_panic(ctx, "type.mdim.rank");
        goto out;
    }
    rows = (int)info.dims[0];
    cols = (int)info.dims[1];
    if (cols > FLAT_LIST_MAX_ELEMS || rows > FLAT_LIST_MAX_ELEMS) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        goto out;
    }
    {
        FlatValue built[FLAT_LIST_MAX_ELEMS];
        for (c = 0; c < cols; c++) {
            FlatValue col_cells[FLAT_LIST_MAX_ELEMS];
            uint32_t row_id;
            uint32_t row_dup;
            for (r = 0; r < rows; r++) {
                double v;
                if (mdim_at2(ctx->coll, m_v.u.coll_id, r, c, &v) != 0) {
                    goto out;
                }
                flat_number_init_legacy_double(&col_cells[r], v);
            }
            if (flat_list_build(ctx->coll, ctx->pool, col_cells, rows, &row_id) != 0) {
                goto out;
            }
            if (mdim_dup_list(ctx, row_id, &row_dup) != 0) {
                goto out;
            }
            flat_value_set_list(&built[c], row_dup);
            flat_value_retain(ctx->coll, ctx->pool, &built[c]);
        }
        if (flat_list_build(ctx->coll, ctx->pool, built, cols, &out_id) != 0) {
            while (c > 0) {
                c--;
                flat_value_release(ctx->coll, ctx->pool, &built[c]);
            }
            goto out;
        }
        for (c = 0; c < cols; c++) {
            flat_value_release(ctx->coll, ctx->pool, &built[c]);
        }
    }
    {
        FlatListShapeInfo out_info;
        memset(&out_info, 0, sizeof out_info);
        out_info.valid = 1;
        out_info.rect = 1;
        out_info.rank = 2;
        out_info.dims_n = 2;
        out_info.dims[0] = (uint16_t)cols;
        out_info.dims[1] = (uint16_t)rows;
        out_info.elem_known = info.elem_known;
        out_info.elem_kind = info.elem_kind;
        if (tags.has_ty) {
            mdim_shape_apply_elem(&out_info, tags.result_ty.kind, 1);
        }
        mdim_stamp_shape(ctx->coll, out_id, &out_info);
    }
    if (mdim_apply_result_list(ctx, out_id, &tags, info.elem_kind, info.elem_known, &final_id) != 0) {
        goto out;
    }
    if (final_id != out_id) {
        FlatListShapeInfo out_info;
        memset(&out_info, 0, sizeof out_info);
        out_info.valid = 1;
        out_info.rect = 1;
        out_info.rank = 2;
        out_info.dims_n = 2;
        out_info.dims[0] = (uint16_t)cols;
        out_info.dims[1] = (uint16_t)rows;
        if (tags.has_ty) {
            mdim_shape_apply_elem(&out_info, tags.result_ty.kind, 1);
        } else {
            out_info.elem_known = info.elem_known;
            out_info.elem_kind = info.elem_kind;
        }
        mdim_stamp_shape(ctx->coll, final_id, &out_info);
    }
    if (flat_value_set_list(ctx->out, final_id) != 0) {
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &m_v);
    return rc;
}

static int mdim_matmul_native(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char left_buf[384];
    char right_buf[384];
    char *left;
    char *right;
    FlatValue a_v;
    FlatValue b_v;
    FlatListShapeInfo ia;
    FlatListShapeInfo ib;
    MdimCallTags tags;
    int a_rows;
    int a_cols;
    int b_rows;
    int b_cols;
    int r;
    uint32_t out_id;
    uint32_t final_id;
    int rc = -1;

    memset(&a_v, 0, sizeof a_v);
    memset(&b_v, 0, sizeof b_v);
    if (!ctx->out || !ctx->io_status || !ctx->inner) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    *ctx->io_status = FLAT_IO_OK;
    if (mdim_resolve_call_tags(ctx, FLAT_NATIVE_MATH_MDIM_MATMUL, &tags) != 0) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        return -1;
    }
    snprintf(left_buf, sizeof left_buf, "%s", left);
    snprintf(right_buf, sizeof right_buf, "%s", right);
    if (mdim_parse_list(ctx, left_buf, &a_v) != 0) {
        goto out;
    }
    if (mdim_parse_list(ctx, right_buf, &b_v) != 0) {
        goto out;
    }
    if (mdim_operand_info(ctx, &a_v, 1, &ia) != 0 || mdim_operand_info(ctx, &b_v, 1, &ib) != 0) {
        goto out;
    }
    if (mdim_role_ok(&ia, &tags) != 0 || mdim_role_ok(&ib, &tags) != 0) {
        mdim_panic(ctx, "type.mdim.tag_shape");
        goto out;
    }
    FlatSlotTypeKind out_elem;
    int out_elem_known;

    if (mdim_merge_elem_kind(&ia, &ib, &out_elem, &out_elem_known) != 0) {
        mdim_panic(ctx, "type.mdim.incompatible");
        goto out;
    }
    mdim_tags_apply_inferred(&tags, out_elem, out_elem_known);
    if (ia.rank != 2 || ib.rank != 2) {
        mdim_panic(ctx, "type.mdim.rank");
        goto out;
    }
    a_rows = (int)ia.dims[0];
    a_cols = (int)ia.dims[1];
    b_rows = (int)ib.dims[0];
    b_cols = (int)ib.dims[1];
    if (a_cols != b_rows) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        goto out;
    }
    if (a_rows > FLAT_LIST_MAX_ELEMS || b_cols > FLAT_LIST_MAX_ELEMS) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        goto out;
    }
    {
        FlatValue built[FLAT_LIST_MAX_ELEMS];
        for (r = 0; r < a_rows; r++) {
            FlatValue cells[FLAT_LIST_MAX_ELEMS];
            uint32_t row_id;
            uint32_t row_dup;
            int c;
            for (c = 0; c < b_cols; c++) {
                double sum = 0.0;
                int k;
                for (k = 0; k < a_cols; k++) {
                    double av;
                    double bv;
                    if (mdim_at2(ctx->coll, a_v.u.coll_id, r, k, &av) != 0 ||
                        mdim_at2(ctx->coll, b_v.u.coll_id, k, c, &bv) != 0) {
                        goto out;
                    }
                    sum += av * bv;
                }
                flat_number_init_legacy_double(&cells[c], sum);
            }
            if (flat_list_build(ctx->coll, ctx->pool, cells, b_cols, &row_id) != 0) {
                goto out;
            }
            if (mdim_dup_list(ctx, row_id, &row_dup) != 0) {
                goto out;
            }
            flat_value_set_list(&built[r], row_dup);
            flat_value_retain(ctx->coll, ctx->pool, &built[r]);
        }
        if (flat_list_build(ctx->coll, ctx->pool, built, a_rows, &out_id) != 0) {
            for (r = 0; r < a_rows; r++) {
                flat_value_release(ctx->coll, ctx->pool, &built[r]);
            }
            goto out;
        }
        for (r = 0; r < a_rows; r++) {
            flat_value_release(ctx->coll, ctx->pool, &built[r]);
        }
    }
    {
        FlatListShapeInfo out_info;
        memset(&out_info, 0, sizeof out_info);
        out_info.valid = 1;
        out_info.rect = 1;
        out_info.rank = 2;
        out_info.dims_n = 2;
        out_info.dims[0] = (uint16_t)a_rows;
        out_info.dims[1] = (uint16_t)b_cols;
        out_info.elem_known = ia.elem_known;
        out_info.elem_kind = ia.elem_kind;
        if (tags.has_ty) {
            mdim_shape_apply_elem(&out_info, tags.result_ty.kind, 1);
        }
        mdim_stamp_shape(ctx->coll, out_id, &out_info);
    }
    if (mdim_apply_result_list(ctx, out_id, &tags, out_elem, out_elem_known, &final_id) != 0) {
        goto out;
    }
    if (final_id != out_id) {
        FlatListShapeInfo out_info;
        memset(&out_info, 0, sizeof out_info);
        out_info.valid = 1;
        out_info.rect = 1;
        out_info.rank = 2;
        out_info.dims_n = 2;
        out_info.dims[0] = (uint16_t)a_rows;
        out_info.dims[1] = (uint16_t)b_cols;
        if (tags.has_ty) {
            mdim_shape_apply_elem(&out_info, tags.result_ty.kind, 1);
        } else {
            out_info.elem_known = ia.elem_known;
            out_info.elem_kind = ia.elem_kind;
        }
        mdim_stamp_shape(ctx->coll, final_id, &out_info);
    }
    if (flat_value_set_list(ctx->out, final_id) != 0) {
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &a_v);
    flat_native_parse_rhs_release(ctx, &b_v);
    return rc;
}

static int mdim_reshape_native(FlatNativeCtx *ctx) {
    char inner_buf[512];
    char left_buf[384];
    char right_buf[384];
    char *left;
    char *right;
    FlatValue src_v;
    FlatValue shape_v;
    FlatListShapeInfo src_info;
    MdimCallTags tags;
    double flat[FLAT_LIST_MAX_ELEMS * 4];
    uint16_t dims[FLAT_LIST_TYPED_MAX_RANK];
    int shape_rank;
    int i;
    int leaf_n = 0;
    int pos = 0;
    int prod = 1;
    uint32_t out_id;
    uint32_t final_id;
    int rc = -1;

    memset(&src_v, 0, sizeof src_v);
    memset(&shape_v, 0, sizeof shape_v);
    if (!ctx->out || !ctx->io_status || !ctx->inner) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    *ctx->io_status = FLAT_IO_OK;
    if (mdim_resolve_call_tags(ctx, FLAT_NATIVE_MATH_MDIM_RESHAPE, &tags) != 0) {
        return -1;
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (split_two_args(inner_buf, &left, &right) != 0) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        return -1;
    }
    snprintf(left_buf, sizeof left_buf, "%s", left);
    snprintf(right_buf, sizeof right_buf, "%s", right);
    if (mdim_parse_list(ctx, left_buf, &src_v) != 0 ||
        mdim_parse_list(ctx, right_buf, &shape_v) != 0) {
        goto out;
    }
    if (mdim_operand_info(ctx, &src_v, 1, &src_info) != 0) {
        goto out;
    }
    if (mdim_role_ok(&src_info, &tags) != 0) {
        mdim_panic(ctx, "type.mdim.tag_shape");
        goto out;
    }
    {
        FlatListShapeInfo sh;
        int sh_len;
        FlatValue el;
        if (mdim_operand_info(ctx, &shape_v, 1, &sh) != 0 || sh.rank != 1) {
            mdim_panic(ctx, "type.mdim.dim_mismatch");
            goto out;
        }
        if (flat_list_len(ctx->coll, shape_v.u.coll_id, &sh_len) != 0 || sh_len <= 0 ||
            sh_len > FLAT_LIST_TYPED_MAX_RANK) {
            mdim_panic(ctx, "type.mdim.dim_mismatch");
            goto out;
        }
        shape_rank = sh_len;
        for (i = 0; i < sh_len; i++) {
            double d;
            if (flat_list_get(ctx->coll, shape_v.u.coll_id, i, &el) != 0 ||
                el.tag != VAL_NUMBER) {
                mdim_panic(ctx, "type.mdim.not_numeric");
                goto out;
            }
            d = flat_number_as_f64(&el);
            if (!isfinite(d) || d <= 0.0 || d != floor(d) ||
                d > (double)(FLAT_LIST_MAX_ELEMS / prod)) {
                mdim_panic(ctx, "type.mdim.dim_mismatch");
                goto out;
            }
            dims[i] = (uint16_t)d;
            prod *= (int)d;
        }
    }
  if (mdim_flatten(ctx->coll, src_v.u.coll_id, flat, (int)(sizeof flat / sizeof flat[0]),
                   &leaf_n) != 0) {
        mdim_panic(ctx, "type.mdim.not_numeric");
        goto out;
    }
    {
        int src_n = 1;
        for (i = 0; i < (int)src_info.dims_n; i++) {
            src_n *= (int)src_info.dims[i];
        }
        if (prod != src_n || prod != leaf_n) {
            mdim_panic(ctx, "type.mdim.dim_mismatch");
            goto out;
        }
    }
    if (mdim_build_from_shape(ctx, flat, &pos, dims, 0, shape_rank, &out_id) != 0) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        goto out;
    }
    {
        FlatListShapeInfo out_info;
        memset(&out_info, 0, sizeof out_info);
        out_info.valid = 1;
        out_info.rect = 1;
        out_info.rank = (unsigned char)shape_rank;
        out_info.dims_n = (unsigned char)shape_rank;
        for (i = 0; i < shape_rank; i++) {
            out_info.dims[i] = dims[i];
        }
        out_info.elem_known = src_info.elem_known;
        out_info.elem_kind = src_info.elem_kind;
        mdim_stamp_shape(ctx->coll, out_id, &out_info);
    }
    if (mdim_apply_result_list(ctx, out_id, &tags, src_info.elem_kind,
                               src_info.elem_known, &final_id) != 0) {
        if (ctx->panic_detail[0] == '\0') {
            mdim_panic(ctx, "type.mdim.incompatible");
        }
        goto out;
    }
    if (flat_value_set_list(ctx->out, final_id) != 0) {
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &src_v);
    flat_native_parse_rhs_release(ctx, &shape_v);
    return rc;
}

#define MDIM_LA_MAX_ORDER 64

static double mdim_la_pivot_tol(int n, const double *a) {
    double scale = 0.0;
    int i;
    int nn = n * n;

    for (i = 0; i < nn; i++) {
        double av = fabs(a[i]);
        if (av > scale) {
            scale = av;
        }
    }
    if (scale == 0.0) {
        return 1e-15;
    }
    return scale * (double)n * 1e-12;
}

static int mdim_load_square(FlatCollectionPool *coll, uint32_t mat_id, int n, double *out) {
    int r;
    int c;

    for (r = 0; r < n; r++) {
        for (c = 0; c < n; c++) {
            if (mdim_at2(coll, mat_id, r, c, &out[r * n + c]) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int mdim_matrix_det(int n, const double *a_in, double *det_out) {
    double *a;
    double tol;
    int sign = 1;
    int i;
    int j;
    int k;
    int p;
    int rc = -1;

    if (!det_out || n <= 0 || n > MDIM_LA_MAX_ORDER) {
        return -1;
    }
    a = (double *)malloc((size_t)n * (size_t)n * sizeof *a);
    if (!a) {
        return -1;
    }
    memcpy(a, a_in, (size_t)n * (size_t)n * sizeof *a);
    tol = mdim_la_pivot_tol(n, a);
    *det_out = 1.0;
    for (i = 0; i < n; i++) {
        p = i;
        for (k = i + 1; k < n; k++) {
            if (fabs(a[k * n + i]) > fabs(a[p * n + i])) {
                p = k;
            }
        }
        if (fabs(a[p * n + i]) <= tol) {
            *det_out = 0.0;
            rc = 0;
            goto done;
        }
        if (p != i) {
            for (j = 0; j < n; j++) {
                double t = a[i * n + j];
                a[i * n + j] = a[p * n + j];
                a[p * n + j] = t;
            }
            sign = -sign;
        }
        *det_out *= a[i * n + i];
        for (k = i + 1; k < n; k++) {
            double f = a[k * n + i] / a[i * n + i];
            for (j = i + 1; j < n; j++) {
                a[k * n + j] -= f * a[i * n + j];
            }
        }
    }
    if (sign < 0) {
        *det_out = -*det_out;
    }
    rc = 0;
done:
    free(a);
    return rc;
}

static int mdim_matrix_inv(int n, const double *a_in, double *inv_out) {
    double *aug;
    double tol;
    int i;
    int j;
    int k;
    int p;
    int rc = -1;
    size_t cols = (size_t)(2 * n);

    if (!inv_out || n <= 0 || n > MDIM_LA_MAX_ORDER) {
        return -1;
    }
    aug = (double *)malloc((size_t)n * cols * sizeof *aug);
    if (!aug) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            aug[i * cols + j] = a_in[i * n + j];
            aug[i * cols + n + j] = (i == j) ? 1.0 : 0.0;
        }
    }
    tol = mdim_la_pivot_tol(n, a_in);
    for (i = 0; i < n; i++) {
        p = i;
        for (k = i + 1; k < n; k++) {
            if (fabs(aug[k * cols + i]) > fabs(aug[p * cols + i])) {
                p = k;
            }
        }
        if (fabs(aug[p * cols + i]) <= tol) {
            rc = -2;
            goto done;
        }
        if (p != i) {
            for (j = 0; j < (int)cols; j++) {
                double t = aug[i * cols + j];
                aug[i * cols + j] = aug[p * cols + j];
                aug[p * cols + j] = t;
            }
        }
        {
            double div = aug[i * cols + i];
            for (j = 0; j < (int)cols; j++) {
                aug[i * cols + j] /= div;
            }
        }
        for (k = 0; k < n; k++) {
            if (k == i) {
                continue;
            }
            {
                double f = aug[k * cols + i];
                if (f == 0.0) {
                    continue;
                }
                for (j = 0; j < (int)cols; j++) {
                    aug[k * cols + j] -= f * aug[i * cols + j];
                }
            }
        }
    }
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            inv_out[i * n + j] = aug[i * cols + n + j];
        }
    }
    rc = 0;
done:
    free(aug);
    return rc;
}

static int mdim_build_square_from_flat(FlatNativeCtx *ctx, const double *flat, int n,
                                       uint32_t *out_id) {
    FlatValue built[MDIM_LA_MAX_ORDER];
    int r;

    for (r = 0; r < n; r++) {
        FlatValue cells[MDIM_LA_MAX_ORDER];
        uint32_t row_id;
        int c;
        for (c = 0; c < n; c++) {
            flat_number_init_legacy_double(&cells[c], flat[r * n + c]);
        }
        if (flat_list_build(ctx->coll, ctx->pool, cells, n, &row_id) != 0) {
            return -1;
        }
        if (mdim_dup_list(ctx, row_id, &row_id) != 0) {
            return -1;
        }
        flat_value_set_list(&built[r], row_id);
        flat_value_retain(ctx->coll, ctx->pool, &built[r]);
    }
    if (flat_list_build(ctx->coll, ctx->pool, built, n, out_id) != 0) {
        for (r = 0; r < n; r++) {
            flat_value_release(ctx->coll, ctx->pool, &built[r]);
        }
        return -1;
    }
    for (r = 0; r < n; r++) {
        flat_value_release(ctx->coll, ctx->pool, &built[r]);
    }
    return 0;
}

static int mdim_square_matrix_native(FlatNativeCtx *ctx, uint16_t native_id, int want_inverse) {
    FlatValue m_v;
    FlatListShapeInfo info;
    MdimCallTags tags;
    int n;
    double a[MDIM_LA_MAX_ORDER * MDIM_LA_MAX_ORDER];
    double work[MDIM_LA_MAX_ORDER * MDIM_LA_MAX_ORDER];
    uint32_t out_id = 0;
    uint32_t final_id = 0;
    int rc = -1;

    memset(&m_v, 0, sizeof m_v);
    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    *ctx->io_status = FLAT_IO_OK;
    if (mdim_resolve_call_tags(ctx, native_id, &tags) != 0) {
        return -1;
    }
    if (mdim_parse_list(ctx, ctx->inner, &m_v) != 0) {
        goto out;
    }
    if (mdim_operand_info(ctx, &m_v, 1, &info) != 0) {
        goto out;
    }
    if (mdim_role_ok(&info, &tags) != 0) {
        mdim_panic(ctx, "type.mdim.tag_shape");
        goto out;
    }
    mdim_tags_apply_inferred(&tags, info.elem_kind, info.elem_known);
    if (info.rank != 2) {
        mdim_panic(ctx, "type.mdim.rank");
        goto out;
    }
    n = (int)info.dims[0];
    if (n != (int)info.dims[1] || n <= 0 || n > MDIM_LA_MAX_ORDER) {
        mdim_panic(ctx, "type.mdim.dim_mismatch");
        goto out;
    }
    if (mdim_load_square(ctx->coll, m_v.u.coll_id, n, a) != 0) {
        goto out;
    }
    if (!want_inverse) {
        double det;
        if (mdim_matrix_det(n, a, &det) != 0) {
            goto out;
        }
        if (mdim_stage_scalar(ctx, det, &tags) != 0) {
            goto out;
        }
        rc = 0;
        goto out;
    }
    {
        int inv_rc = mdim_matrix_inv(n, a, work);

        if (inv_rc == -2) {
            mdim_panic(ctx, "type.mdim.singular");
            goto out;
        }
        if (inv_rc != 0) {
            goto out;
        }
    }
    if (mdim_build_square_from_flat(ctx, work, n, &out_id) != 0) {
        goto out;
    }
    {
        FlatListShapeInfo out_info;
        memset(&out_info, 0, sizeof out_info);
        out_info.valid = 1;
        out_info.rect = 1;
        out_info.rank = 2;
        out_info.dims_n = 2;
        out_info.dims[0] = (uint16_t)n;
        out_info.dims[1] = (uint16_t)n;
        out_info.elem_known = info.elem_known;
        out_info.elem_kind = info.elem_kind;
        if (tags.has_ty) {
            mdim_shape_apply_elem(&out_info, tags.result_ty.kind, 1);
        }
        mdim_stamp_shape(ctx->coll, out_id, &out_info);
    }
    if (mdim_apply_result_list(ctx, out_id, &tags, info.elem_kind, info.elem_known, &final_id) !=
        0) {
        goto out;
    }
    if (final_id != out_id) {
        FlatListShapeInfo out_info;
        memset(&out_info, 0, sizeof out_info);
        out_info.valid = 1;
        out_info.rect = 1;
        out_info.rank = 2;
        out_info.dims_n = 2;
        out_info.dims[0] = (uint16_t)n;
        out_info.dims[1] = (uint16_t)n;
        if (tags.has_ty) {
            mdim_shape_apply_elem(&out_info, tags.result_ty.kind, 1);
        } else {
            out_info.elem_known = info.elem_known;
            out_info.elem_kind = info.elem_kind;
        }
        mdim_stamp_shape(ctx->coll, final_id, &out_info);
    }
    if (flat_value_set_list(ctx->out, final_id) != 0) {
        goto out;
    }
    rc = 0;
out:
    flat_native_parse_rhs_release(ctx, &m_v);
    return rc;
}

int flat_native_math_mdim_add(FlatNativeCtx *ctx) {
    return mdim_binary_native(ctx, FLAT_NATIVE_MATH_MDIM_ADD, mdim_op_add);
}

int flat_native_math_mdim_subtract(FlatNativeCtx *ctx) {
    return mdim_binary_native(ctx, FLAT_NATIVE_MATH_MDIM_SUBTRACT, mdim_op_sub);
}

int flat_native_math_mdim_multiply(FlatNativeCtx *ctx) {
    return mdim_binary_native(ctx, FLAT_NATIVE_MATH_MDIM_MULTIPLY, mdim_op_mul);
}

int flat_native_math_mdim_divide(FlatNativeCtx *ctx) {
    return mdim_binary_native(ctx, FLAT_NATIVE_MATH_MDIM_DIVIDE, mdim_op_div);
}

int flat_native_math_mdim_sum(FlatNativeCtx *ctx) {
    return mdim_reduce_native(ctx, FLAT_NATIVE_MATH_MDIM_SUM, 0);
}

int flat_native_math_mdim_min(FlatNativeCtx *ctx) {
    return mdim_reduce_native(ctx, FLAT_NATIVE_MATH_MDIM_MIN, 2);
}

int flat_native_math_mdim_max(FlatNativeCtx *ctx) {
    return mdim_reduce_native(ctx, FLAT_NATIVE_MATH_MDIM_MAX, 3);
}

int flat_native_math_mdim_avg(FlatNativeCtx *ctx) {
    return mdim_reduce_native(ctx, FLAT_NATIVE_MATH_MDIM_AVG, 1);
}

int flat_native_math_mdim_dot(FlatNativeCtx *ctx) {
    return mdim_dot_native(ctx);
}

int flat_native_math_mdim_transpose(FlatNativeCtx *ctx) {
    return mdim_transpose_native(ctx);
}

int flat_native_math_mdim_matmul(FlatNativeCtx *ctx) {
    return mdim_matmul_native(ctx);
}

int flat_native_math_mdim_reshape(FlatNativeCtx *ctx) {
    return mdim_reshape_native(ctx);
}

int flat_native_math_mdim_determinant(FlatNativeCtx *ctx) {
    return mdim_square_matrix_native(ctx, FLAT_NATIVE_MATH_MDIM_DETERMINANT, 0);
}

int flat_native_math_mdim_inverse(FlatNativeCtx *ctx) {
    return mdim_square_matrix_native(ctx, FLAT_NATIVE_MATH_MDIM_INVERSE, 1);
}