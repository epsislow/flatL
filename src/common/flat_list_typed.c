#include "flat_list_typed.h"

#include "flat_schema.h"

#include <string.h>

static int axis_len_ok(uint8_t mode, uint16_t val, int len, int depth, int list_rank, char *panic,
                      size_t panic_sz) {
    if (len < 1) {
        if (list_rank >= 2 && depth == 0) {
            snprintf(panic, panic_sz, "type.assign.list.empty");
            return -1;
        }
        if (mode == FLAT_AXIS_EXACT) {
            snprintf(panic, panic_sz, "type.assign.shape");
            return -1;
        }
        return 0;
    }
    switch (mode) {
    case FLAT_AXIS_FREE:
        return 0;
    case FLAT_AXIS_CAP:
        if ((unsigned)len > (unsigned)val) {
            snprintf(panic, panic_sz, "type.assign.shape");
            return -1;
        }
        return 0;
    case FLAT_AXIS_EXACT:
        if (len != (int)val) {
            snprintf(panic, panic_sz, "type.assign.shape");
            return -1;
        }
        return 0;
    default:
        snprintf(panic, panic_sz, "type.assign.incompatible");
        return -1;
    }
}

static int leaf_ok(const FlatSlotType *contract, const FlatValue *val, FlatStringPool *pool,
                   FlatCollectionPool *coll, const FlatSchemaCatalog *schemas, char *panic,
                   size_t panic_sz) {
    FlatSlotType scalar;
    FlatValue dummy;

    if (contract->list_leaf_sublist) {
        return val->tag == VAL_LIST ? 0 : -1;
    }
    if (contract->kind == FLAT_SLOT_TYPE_O && contract->list_rank >= 1) {
        return val->tag == VAL_OBJECT ? 0 : -1;
    }
    if (contract->kind == FLAT_SLOT_TYPE_L) {
        if (contract->list_rank >= 2) {
            return 0;
        }
        if (contract->list_rank == 1) {
            return 0;
        }
    }
    memset(&scalar, 0, sizeof scalar);
    scalar.kind = contract->kind;
    scalar.aux = contract->aux;
    scalar.schema_id = contract->schema_id;
    dummy = *val;
    return flat_slot_type_check_value(&scalar, val, pool, coll, schemas, &dummy, panic, panic_sz);
}

static int validate_level(const FlatSlotType *contract, FlatCollectionPool *coll,
                          FlatStringPool *pool, const FlatSchemaCatalog *schemas,
                          uint32_t list_id, int depth, char *panic, size_t panic_sz) {
    int len = 0;
    int i;
    int expect_len = -1;

    if (!coll || flat_list_len(coll, list_id, &len) != 0) {
        snprintf(panic, panic_sz, "type.assign.incompatible");
        return -1;
    }
    if (depth >= (int)contract->list_rank) {
        snprintf(panic, panic_sz, "type.assign.list.rank");
        return -1;
    }
    if (axis_len_ok(contract->axis_mode[depth], contract->axis_val[depth], len, depth,
                    (int)contract->list_rank, panic, panic_sz) != 0) {
        return -1;
    }
    if (contract->list_rank >= 2 && depth < (int)contract->list_rank - 1) {
        int all_children_list = 1;
        for (i = 0; i < len; i++) {
            FlatValue row;
            if (flat_list_get(coll, list_id, i, &row) != 0 || row.tag != VAL_LIST) {
                all_children_list = 0;
                break;
            }
        }
        if (all_children_list) {
            for (i = 0; i < len; i++) {
                FlatValue row;
                int row_len;
                if (flat_list_get(coll, list_id, i, &row) != 0 ||
                    flat_list_len(coll, row.u.coll_id, &row_len) != 0) {
                    snprintf(panic, panic_sz, "type.assign.list.jagged");
                    return -1;
                }
                if (expect_len < 0) {
                    expect_len = row_len;
                } else if (row_len != expect_len) {
                    snprintf(panic, panic_sz, "type.assign.list.jagged");
                    return -1;
                }
            }
        }
    }
    for (i = 0; i < len; i++) {
        FlatValue el;
        if (flat_list_get(coll, list_id, i, &el) != 0) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        if (depth == (int)contract->list_rank - 1) {
            if (leaf_ok(contract, &el, pool, coll, schemas, panic, panic_sz) != 0) {
                if (panic[0] == '\0') {
                    snprintf(panic, panic_sz, "type.assign.incompatible");
                }
                return -1;
            }
        } else {
            if (el.tag != VAL_LIST) {
                snprintf(panic, panic_sz, "type.assign.incompatible");
                return -1;
            }
            if (validate_level(contract, coll, pool, schemas, el.u.coll_id, depth + 1, panic,
                               panic_sz) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

int flat_slot_type_list_index_contract(const FlatSlotType *list, FlatSlotType *inner) {
    if (!list || !inner || list->list_rank < 1) {
        return -1;
    }
    if (list->list_rank == 1) {
        if (list->list_leaf_sublist) {
            flat_slot_type_clear(inner);
            inner->kind = FLAT_SLOT_TYPE_L;
            return 0;
        }
        if (list->kind == FLAT_SLOT_TYPE_L) {
            flat_slot_type_clear(inner);
            return 0;
        }
        memset(inner, 0, sizeof *inner);
        inner->kind = list->kind;
        inner->aux = list->aux;
        inner->schema_id = list->schema_id;
        return 0;
    }
    return flat_slot_type_list_inner_contract(list, inner);
}

static int enforce_rect_at_depth(FlatCollectionPool *coll, uint32_t list_id, int depth,
                                 int target_depth) {
    int len = 0;
    int i;
    int expect = -1;

    if (depth >= target_depth) {
        return 0;
    }
    if (flat_list_len(coll, list_id, &len) != 0) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        FlatValue row;
        int row_len;

        if (flat_list_get(coll, list_id, i, &row) != 0 || row.tag != VAL_LIST) {
            return -1;
        }
        if (enforce_rect_at_depth(coll, row.u.coll_id, depth + 1, target_depth) != 0) {
            return -1;
        }
        if (flat_list_len(coll, row.u.coll_id, &row_len) != 0) {
            return -1;
        }
        if (expect < 0) {
            expect = row_len;
        } else if (row_len != expect) {
            return -1;
        }
    }
    return 0;
}

int flat_list_value_rank(FlatCollectionPool *coll, uint32_t list_id) {
    int len = 0;
    int i;
    int max_sub = 0;
    int all_list = 1;

    if (!coll || flat_list_len(coll, list_id, &len) != 0) {
        return -1;
    }
    if (len == 0) {
        return 1;
    }
    for (i = 0; i < len; i++) {
        FlatValue el;
        if (flat_list_get(coll, list_id, i, &el) != 0) {
            return -1;
        }
        if (el.tag != VAL_LIST) {
            all_list = 0;
            break;
        }
    }
    if (!all_list) {
        return 1;
    }
    for (i = 0; i < len; i++) {
        FlatValue el;
        int sub;
        if (flat_list_get(coll, list_id, i, &el) != 0 || el.tag != VAL_LIST) {
            return -1;
        }
        sub = flat_list_value_rank(coll, el.u.coll_id);
        if (sub < 0) {
            return -1;
        }
        if (sub > max_sub) {
            max_sub = sub;
        }
    }
    return 1 + max_sub;
}

int flat_slot_type_literal_suffix_compatible(const FlatSlotType *slot, const FlatSlotType *lit,
                                             char *panic, size_t panic_sz) {
    if (!slot || !lit || lit->list_rank == 0) {
        return 0;
    }
    if (slot->list_rank == 0) {
        return 0;
    }
    if (slot->kind != lit->kind) {
        snprintf(panic, panic_sz, "type.assign.literal.type");
        return -1;
    }
    if (slot->kind == FLAT_SLOT_TYPE_TN && slot->aux != lit->aux) {
        snprintf(panic, panic_sz, "type.assign.literal.type");
        return -1;
    }
    if (slot->kind == FLAT_SLOT_TYPE_SCHEMA && slot->schema_id != lit->schema_id) {
        snprintf(panic, panic_sz, "type.assign.literal.type");
        return -1;
    }
    if (slot->list_leaf_sublist != lit->list_leaf_sublist) {
        snprintf(panic, panic_sz, "type.assign.literal.type");
        return -1;
    }
    return 0;
}

int flat_slot_type_list_inner_contract(const FlatSlotType *list, FlatSlotType *inner) {
    int r;
    int i;

    if (!list || !inner || list->list_rank < 2) {
        return -1;
    }
    *inner = *list;
    r = (int)list->list_rank - 1;
    inner->list_rank = (uint8_t)r;
    for (i = 0; i < FLAT_LIST_TYPED_MAX_RANK; i++) {
        if (i < r) {
            inner->axis_mode[i] = list->axis_mode[i + 1];
            inner->axis_val[i] = list->axis_val[i + 1];
        } else {
            inner->axis_mode[i] = FLAT_AXIS_FREE;
            inner->axis_val[i] = 0;
        }
    }
    return 0;
}

int flat_list_typed_check_value(const FlatSlotType *contract, const FlatValue *val,
                                FlatStringPool *pool, FlatCollectionPool *coll,
                                const FlatSchemaCatalog *schemas, FlatValue *inout_val,
                                char *panic, size_t panic_sz) {
    FlatValue items[FLAT_LIST_MAX_ELEMS];
    int n = 0;
    int i;
    int len = 0;
    uint32_t new_id;

    if (!contract || contract->list_rank == 0) {
        snprintf(panic, panic_sz, "type.assign.incompatible");
        return -1;
    }
    if (!val) {
        snprintf(panic, panic_sz, "type.assign.incompatible");
        return -1;
    }
    if (val->tag == VAL_NULL) {
        if (inout_val) {
            *inout_val = *val;
        }
        return 0;
    }
    if (val->tag != VAL_LIST) {
        snprintf(panic, panic_sz, "type.assign.incompatible");
        return -1;
    }
    if (!(contract->list_leaf_sublist && contract->list_rank == 1)) {
        int vr = flat_list_value_rank(coll, val->u.coll_id);
        if (vr >= 0 && vr != (int)contract->list_rank) {
            snprintf(panic, panic_sz, "type.assign.list.rank");
            return -1;
        }
    }
    if (validate_level(contract, coll, pool, schemas, val->u.coll_id, 0, panic, panic_sz) != 0) {
        return -1;
    }
    if (!inout_val || contract->list_rank != 1 || contract->list_leaf_sublist ||
        contract->kind == FLAT_SLOT_TYPE_L) {
        if (inout_val) {
            *inout_val = *val;
        }
        return 0;
    }
    if (contract->kind == FLAT_SLOT_TYPE_SCHEMA && schemas) {
        if (flat_list_len(coll, val->u.coll_id, &len) != 0) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        for (i = 0; i < len; i++) {
            FlatValue el;
            FlatValue checked;
            if (flat_list_get(coll, val->u.coll_id, i, &el) != 0) {
                snprintf(panic, panic_sz, "type.assign.incompatible");
                return -1;
            }
            checked = el;
            if (flat_schema_bind_value(schemas, contract->schema_id, &checked, pool, coll, panic,
                                       panic_sz) != 0) {
                return -1;
            }
            if (n >= FLAT_LIST_MAX_ELEMS) {
                snprintf(panic, panic_sz, "type.assign.list.len");
                return -1;
            }
            items[n++] = checked;
        }
        if (flat_list_build(coll, pool, items, n, &new_id) != 0) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        flat_value_set_list(inout_val, new_id);
        return 0;
    }
    if (flat_list_len(coll, val->u.coll_id, &len) != 0) {
        snprintf(panic, panic_sz, "type.assign.incompatible");
        return -1;
    }
    for (i = 0; i < len; i++) {
        FlatValue el;
        FlatValue checked;
        FlatSlotType scalar;

        if (flat_list_get(coll, val->u.coll_id, i, &el) != 0) {
            snprintf(panic, panic_sz, "type.assign.incompatible");
            return -1;
        }
        memset(&scalar, 0, sizeof scalar);
        scalar.kind = contract->kind;
        scalar.aux = contract->aux;
        checked = el;
        if (flat_slot_type_check_value(&scalar, &el, pool, coll, schemas, &checked, panic,
                                       panic_sz) != 0) {
            return -1;
        }
        if (n >= FLAT_LIST_MAX_ELEMS) {
            snprintf(panic, panic_sz, "type.assign.list.len");
            return -1;
        }
        items[n++] = checked;
    }
    if (flat_list_build(coll, pool, items, n, &new_id) != 0) {
        snprintf(panic, panic_sz, "type.assign.incompatible");
        return -1;
    }
    flat_value_set_list(inout_val, new_id);
    return 0;
}