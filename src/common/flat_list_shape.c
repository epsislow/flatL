#include "flat_list_shape.h"

#include "flat_list_typed.h"

#include <string.h>

static int value_leaf_kind(const FlatValue *v, FlatSlotTypeKind *kind_out) {
    if (!v || !kind_out) {
        return -1;
    }
    switch (v->tag) {
    case VAL_BOOL:
        *kind_out = FLAT_SLOT_TYPE_B;
        return 0;
    case VAL_NUMBER:
        *kind_out = FLAT_SLOT_TYPE_F64;
        return 0;
    case VAL_STR_INLINE:
    case VAL_STR_REF:
        *kind_out = FLAT_SLOT_TYPE_T;
        return 0;
    case VAL_OBJECT:
        *kind_out = FLAT_SLOT_TYPE_O;
        return 0;
    case VAL_LIST:
        *kind_out = FLAT_SLOT_TYPE_L;
        return 0;
    default:
        return -1;
    }
}

static int kind_is_numeric(FlatSlotTypeKind kind) {
    switch (kind) {
    case FLAT_SLOT_TYPE_U8:
    case FLAT_SLOT_TYPE_U16:
    case FLAT_SLOT_TYPE_U32:
    case FLAT_SLOT_TYPE_U64:
    case FLAT_SLOT_TYPE_S8:
    case FLAT_SLOT_TYPE_S16:
    case FLAT_SLOT_TYPE_S32:
    case FLAT_SLOT_TYPE_S64:
    case FLAT_SLOT_TYPE_F32:
    case FLAT_SLOT_TYPE_F64:
        return 1;
    default:
        return 0;
    }
}

static int list_dims_equal(const FlatCollectionPool *coll, uint32_t a_id, uint32_t b_id) {
    FlatListShapeInfo sa, sb;

    if (flat_list_shape_walk((FlatCollectionPool *)coll, a_id, &sa) != 0 ||
        flat_list_shape_walk((FlatCollectionPool *)coll, b_id, &sb) != 0) {
        return 0;
    }
    if (sa.rect != sb.rect || sa.rank != sb.rank || sa.dims_n != sb.dims_n) {
        return 0;
    }
    return memcmp(sa.dims, sb.dims, (size_t)sa.dims_n * sizeof sa.dims[0]) == 0;
}

static int walk_rect_recurse(FlatCollectionPool *coll, uint32_t list_id, FlatListShapeInfo *out) {
    int len = 0;
    int i;
    FlatValue el;

    memset(out, 0, sizeof *out);
    if (!coll || flat_list_len(coll, list_id, &len) != 0) {
        return -1;
    }
    if (len == 0) {
        out->valid = 1;
        out->rect = 1;
        out->rank = 1;
        out->dims_n = 1;
        out->dims[0] = 0;
        return 0;
    }
    if (flat_list_get(coll, list_id, 0, &el) != 0) {
        return -1;
    }
    if (el.tag == VAL_LIST) {
        FlatListShapeInfo child;
        int all_list = 1;

        for (i = 0; i < len; i++) {
            if (flat_list_get(coll, list_id, i, &el) != 0) {
                return -1;
            }
            if (el.tag != VAL_LIST) {
                all_list = 0;
                break;
            }
        }
        if (!all_list) {
            out->valid = 1;
            out->rect = 0;
            out->rank = (uint8_t)flat_list_value_rank(coll, list_id);
            return 0;
        }
        if (flat_list_get(coll, list_id, 0, &el) != 0 ||
            flat_list_shape_walk(coll, el.u.coll_id, &child) != 0) {
            return -1;
        }
        for (i = 1; i < len; i++) {
            FlatValue row;
            if (flat_list_get(coll, list_id, i, &row) != 0 || row.tag != VAL_LIST) {
                out->valid = 1;
                out->rect = 0;
                out->rank = (uint8_t)(child.rank + 1);
                return 0;
            }
            if (!list_dims_equal(coll, el.u.coll_id, row.u.coll_id)) {
                out->valid = 1;
                out->rect = 0;
                out->rank = (uint8_t)(child.rank + 1);
                return 0;
            }
        }
        out->valid = 1;
        if (!child.rect) {
            out->rect = 0;
            out->rank = (uint8_t)(child.rank + 1);
            return 0;
        }
        out->rect = 1;
        out->rank = (uint8_t)(child.rank + 1);
        out->dims_n = (unsigned char)(child.dims_n + 1);
        if (out->dims_n > FLAT_LIST_TYPED_MAX_RANK) {
            out->rect = 0;
            return 0;
        }
        out->dims[0] = (uint16_t)len;
        memcpy(&out->dims[1], child.dims, (size_t)child.dims_n * sizeof child.dims[0]);
        out->elem_kind = child.elem_kind;
        out->elem_known = child.elem_known;
        return 0;
    }

    out->valid = 1;
    out->rect = 1;
    out->rank = 1;
    out->dims_n = 1;
    out->dims[0] = (uint16_t)len;
    if (value_leaf_kind(&el, &out->elem_kind) == 0) {
        out->elem_known = 1;
        for (i = 1; i < len; i++) {
            FlatSlotTypeKind k;
            if (flat_list_get(coll, list_id, i, &el) != 0) {
                return -1;
            }
            if (el.tag == VAL_LIST) {
                out->rect = 0;
                out->rank = (uint8_t)flat_list_value_rank(coll, list_id);
                out->dims_n = 0;
                out->elem_known = 0;
                return 0;
            }
            if (value_leaf_kind(&el, &k) != 0 || k != out->elem_kind) {
                out->elem_known = 0;
                break;
            }
        }
    }
    return 0;
}

int flat_list_shape_walk(FlatCollectionPool *coll, uint32_t list_id, FlatListShapeInfo *out) {
    if (!out) {
        return -1;
    }
    return walk_rect_recurse(coll, list_id, out);
}

int flat_list_shape_query(FlatCollectionPool *coll, uint32_t list_id, FlatListShapeInfo *out) {
    FlatListShapeInfo walked;

    if (!coll || !out) {
        return -1;
    }
    if (flat_coll_shape_slot_read(coll, list_id, out) == 0) {
        return 0;
    }
    if (flat_list_shape_walk(coll, list_id, &walked) != 0) {
        return -1;
    }
    walked.source = FLAT_LIST_SHAPE_SRC_WALK;
    flat_coll_shape_slot_write(coll, list_id, &walked);
    *out = walked;
    return 0;
}

void flat_list_shape_stamp_typed(FlatCollectionPool *coll, uint32_t list_id,
                                 const FlatSlotType *contract, FlatListShapeSource src) {
    FlatListShapeInfo walked;
    FlatListShapeInfo st;

    if (!coll || !contract || contract->list_rank == 0) {
        return;
    }
    if (flat_list_shape_walk(coll, list_id, &walked) != 0) {
        return;
    }
    walked.source = src;
    if (contract->kind != FLAT_SLOT_TYPE_L && contract->kind != FLAT_SLOT_TYPE_NONE) {
        walked.elem_kind = contract->kind;
        walked.elem_known = 1;
    }
    if (contract->list_rank >= 1 && walked.rank == 0) {
        walked.rank = contract->list_rank;
    }
    st = walked;
    flat_coll_shape_slot_write(coll, list_id, &st);
}

void flat_list_shape_clear(FlatCollectionPool *coll, uint32_t list_id) {
    flat_coll_shape_slot_clear(coll, list_id);
}

int flat_list_shape_kind_numeric(FlatSlotTypeKind kind) {
    return kind_is_numeric(kind);
}

int flat_list_shape_info_is_vector(const FlatListShapeInfo *info) {
    if (!info || !info->valid || !info->rect || info->rank != 1) {
        return 0;
    }
    return info->elem_known && kind_is_numeric(info->elem_kind);
}

int flat_list_shape_info_is_matrix(const FlatListShapeInfo *info) {
    if (!info || !info->valid || !info->rect || info->rank != 2) {
        return 0;
    }
    return info->elem_known && kind_is_numeric(info->elem_kind);
}