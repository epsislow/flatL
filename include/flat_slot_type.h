#ifndef FLAT_SLOT_TYPE_H
#define FLAT_SLOT_TYPE_H

#include "flat_value.h"

typedef struct FlatSchemaCatalog FlatSchemaCatalog;

#include <stddef.h>
#include <stdint.h>

typedef enum {
    FLAT_SLOT_TYPE_NONE = 0,
    FLAT_SLOT_TYPE_U8,
    FLAT_SLOT_TYPE_U16,
    FLAT_SLOT_TYPE_U32,
    FLAT_SLOT_TYPE_U64,
    FLAT_SLOT_TYPE_S8,
    FLAT_SLOT_TYPE_S16,
    FLAT_SLOT_TYPE_S32,
    FLAT_SLOT_TYPE_S64,
    FLAT_SLOT_TYPE_F32,
    FLAT_SLOT_TYPE_F64,
    FLAT_SLOT_TYPE_B,
    FLAT_SLOT_TYPE_T,
    FLAT_SLOT_TYPE_TN,
    FLAT_SLOT_TYPE_L,
    FLAT_SLOT_TYPE_O,
    FLAT_SLOT_TYPE_SCHEMA,
    FLAT_SLOT_TYPE_DYNAMIC /* LHS :/expr — resolved at assign (not stored on slot) */
} FlatSlotTypeKind;

typedef struct {
    FlatSlotTypeKind kind;
    uint8_t aux; /* tN → N; t → 127 */
    uint16_t schema_id;
    uint16_t list_max; /* schema field or list slot: 0 = unbounded [] */
    uint8_t list_slot; /* 1 when variable slot is a LIST (e.g. xs:<stock>[4]) */
    char dynamic_expr[FLAT_VAR_NAME_MAX + 1]; /* FLAT_SLOT_TYPE_DYNAMIC */
} FlatSlotType;

#define FLAT_SLOT_TYPE_AUX_T_DEFAULT 127u

/* Parse LHS bind (after assign split). *has_type_out = 1 if :type present. */
int flat_bind_parse_lhs(const char *lhs, char *name_out, size_t name_sz, FlatSlotType *type_out,
                        int *has_type_out);

/* Compile-time without schema catalog (rejects `<name>`). */
int flat_bind_validate_t1_lhs(const char *lhs, char *err, size_t err_sz);

int flat_slot_type_parse_spec(const char *spec, FlatSlotType *out, const FlatSchemaCatalog *cat,
                              char *err, size_t err_sz);

int flat_slot_type_equals(const FlatSlotType *a, const FlatSlotType *b);

/* 0 ok; -1 fail, panic reason written */
int flat_slot_type_check_value(const FlatSlotType *contract, const FlatValue *val,
                               FlatStringPool *pool, FlatCollectionPool *coll,
                               const FlatSchemaCatalog *schemas, FlatValue *inout_val,
                               char *panic, size_t panic_sz);

void flat_slot_type_clear(FlatSlotType *t);

/* Guard eval: 1 = true, 0 = false, -1 = panic (type.compare.incompatible, …). */
int flat_value_eval_guard(const char *expr, FlatValue *vars, int var_count,
                          const char (*names)[64], FlatStringPool *pool, FlatCollectionPool *coll,
                          const uint8_t *var_has_slot_type, const FlatSlotType *var_slot_types,
                          char *panic, size_t panic_sz);

int flat_slot_type_is_scalar(const FlatSlotType *t);

/* u* / s* / f* only (for m.calc homogeneity). */
int flat_slot_type_is_numeric(const FlatSlotType *t);

#endif
