#ifndef FLAT_LIST_TYPED_H
#define FLAT_LIST_TYPED_H

#include "flat_schema.h"
#include "flat_slot_type.h"
#include "flat_value.h"

/* Peels one LIST index level for path / nested assign (L.2). */
int flat_slot_type_list_inner_contract(const FlatSlotType *list, FlatSlotType *inner);

/* Contract at list[i] (rank 1 → leaf elem type; rank ≥ 2 → inner list type). */
int flat_slot_type_list_index_contract(const FlatSlotType *list, FlatSlotType *inner);

/* LIST nesting depth (1 = vector of scalars/objects). */
int flat_list_value_rank(FlatCollectionPool *coll, uint32_t list_id);

int flat_slot_type_literal_suffix_compatible(const FlatSlotType *slot, const FlatSlotType *lit,
                                             char *panic, size_t panic_sz);

/*
 * Validates LIST value against typed multidim contract (list_rank > 0).
 * May coerce leaf scalars into *inout_val when non-NULL.
 */
int flat_list_typed_check_value(const FlatSlotType *contract, const FlatValue *val,
                                FlatStringPool *pool, FlatCollectionPool *coll,
                                const FlatSchemaCatalog *schemas, FlatValue *inout_val,
                                char *panic, size_t panic_sz);

#endif