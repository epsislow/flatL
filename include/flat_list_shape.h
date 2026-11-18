#ifndef FLAT_LIST_SHAPE_H
#define FLAT_LIST_SHAPE_H

#include "flat_slot_type.h"
#include "flat_value.h"

typedef enum {
    FLAT_LIST_SHAPE_SRC_NONE = 0,
    FLAT_LIST_SHAPE_SRC_CONTRACT,
    FLAT_LIST_SHAPE_SRC_LITERAL,
    FLAT_LIST_SHAPE_SRC_WALK,
} FlatListShapeSource;

typedef struct FlatListShapeInfo {
    unsigned char valid;
    unsigned char rect;
    unsigned char rank;
    unsigned char dims_n;
    uint16_t dims[FLAT_LIST_TYPED_MAX_RANK];
    FlatSlotTypeKind elem_kind;
    unsigned char elem_known;
    FlatListShapeSource source;
} FlatListShapeInfo;

/* Walk only — does not read stamp/memo. */
int flat_list_shape_walk(FlatCollectionPool *coll, uint32_t list_id, FlatListShapeInfo *out);

/* Stamp → memo → walk (memo writes source WALK). */
int flat_list_shape_query(FlatCollectionPool *coll, uint32_t list_id, FlatListShapeInfo *out);

void flat_list_shape_stamp_typed(FlatCollectionPool *coll, uint32_t list_id,
                                 const FlatSlotType *contract, FlatListShapeSource src);

void flat_list_shape_clear(FlatCollectionPool *coll, uint32_t list_id);

int flat_list_shape_kind_numeric(FlatSlotTypeKind kind);
int flat_list_shape_info_is_vector(const FlatListShapeInfo *info);
int flat_list_shape_info_is_matrix(const FlatListShapeInfo *info);

/* Internal: pool slot sidecar (implemented in value_collection.c). */
void flat_coll_shape_slot_clear(FlatCollectionPool *pool, uint32_t id);
int flat_coll_shape_slot_read(const FlatCollectionPool *pool, uint32_t id, FlatListShapeInfo *out);
void flat_coll_shape_slot_write(FlatCollectionPool *pool, uint32_t id, const FlatListShapeInfo *info);

#endif