#ifndef FLAT_OBJ_PATH_H
#define FLAT_OBJ_PATH_H

#include "flat_value.h"
#include "flat_slot_type.h"

typedef struct FlatSchemaCatalog FlatSchemaCatalog;

#include <stddef.h>

#define FLAT_OBJ_PATH_MAX_SEG 32

typedef enum {
    FLAT_PATH_OK = 0,
    FLAT_PATH_SYNTAX,
    FLAT_PATH_ARG_TYPE,
    FLAT_PATH_MISSING,
    FLAT_PATH_TYPE,
    FLAT_PATH_INDEX,
    FLAT_PATH_SET,
    FLAT_PATH_ROOT
} FlatPathErr;

typedef struct {
    char keys[FLAT_OBJ_PATH_MAX_SEG][FLAT_OBJ_KEY_MAX + 1];
    int n;
} FlatObjPathSegs;

typedef enum {
    FLAT_PATH_STEP_FIELD,
    FLAT_PATH_STEP_INDEX
} FlatPathStepKind;

typedef struct {
    FlatPathStepKind kind;
    char key[FLAT_OBJ_KEY_MAX + 1];
    int index;
} FlatPathStep;

int flat_obj_path_parse_cstr(const char *path, FlatObjPathSegs *out);

int flat_obj_path_from_value(const FlatValue *v, const FlatStringPool *pool, FlatObjPathSegs *out);

int flat_obj_get_path(const FlatCollectionPool *pool, uint32_t obj_id, const FlatObjPathSegs *path,
                      FlatValue *out);
int flat_obj_has_path(const FlatCollectionPool *pool, uint32_t obj_id, const FlatObjPathSegs *path,
                      int *out_has);
int flat_obj_set_path_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t obj_id,
                           const FlatObjPathSegs *path, const FlatValue *val, uint32_t *out_id);
int flat_obj_remove_path_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t obj_id,
                              const FlatObjPathSegs *path, uint32_t *out_id);

int flat_path_read_suffixes(const char **p, FlatValue *cur, FlatStringPool *pool,
                            FlatCollectionPool *coll, FlatValue *vars, int var_count,
                            const char (*var_names)[64], FlatPathErr *err_out);

typedef struct {
    const FlatSchemaCatalog *schemas;
    const FlatSlotType *var_slot_types;
    const uint8_t *var_has_slot_type;
    char *type_panic;
    size_t type_panic_sz;
} FlatPathTypedCtx;

int flat_path_set2(FlatCollectionPool *coll, FlatStringPool *pool, FlatValue *vars, int var_count,
                   const char (*var_names)[64], const char *path_expr, const FlatValue *val,
                   uint32_t *out_root_obj, FlatPathErr *err_out,
                   const FlatPathTypedCtx *typed);

double flat_path_resolve_number(const char *name, const char **rest,
                                const FlatCollectionPool *coll, FlatStringPool *pool,
                                const FlatValue *vars, int var_count,
                                const char (*var_names)[64], FlatPathErr *err_out);

const char *flat_path_reason(FlatPathErr err, const char *ctx, char *buf, size_t buf_sz);

#endif
