#ifndef FLAT_VALUE_H
#define FLAT_VALUE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Documented in docs/LANGUAGE_TYPES.md */
#define FLAT_INLINE_STR_MAX 127
#define FLAT_VAR_NAME_MAX 63
#define FLAT_MAX_VARS 128
#define FLAT_POOL_MAX_SLOTS 1024
#define FLAT_POOL_MAX_CHUNK (16u * 1024u * 1024u)
#define FLAT_POOL_MAX_TOTAL (64u * 1024u * 1024u)

#define FLAT_COLL_MAX_LISTS 256
#define FLAT_COLL_MAX_OBJECTS 256
#define FLAT_LIST_MAX_ELEMS 1024
#define FLAT_OBJ_MAX_KEYS 128
#define FLAT_OBJ_KEY_MAX 127

typedef enum {
    VAL_EMPTY,
    VAL_BOOL,
    VAL_NUMBER,
    VAL_STR_INLINE,
    VAL_STR_REF,
    VAL_LIST,
    VAL_OBJECT
} FlatValueTag;

typedef struct {
    FlatValueTag tag;
    union {
        int boolean;
        double number;
        struct {
            char bytes[FLAT_INLINE_STR_MAX + 1];
            size_t len;
        } ins;
        uint32_t ref_id;
        uint32_t coll_id;
    } u;
} FlatValue;

typedef struct FlatStringPool FlatStringPool;
typedef struct FlatCollectionPool FlatCollectionPool;

FlatStringPool *flat_pool_create(void);
void flat_pool_destroy(FlatStringPool *pool);

int flat_pool_alloc_copy(FlatStringPool *pool, const void *data, size_t len, uint32_t *out_id);
int flat_pool_slice(FlatStringPool *pool, uint32_t parent_id, size_t offset, size_t len,
                    uint32_t *out_id);
const uint8_t *flat_pool_read(const FlatStringPool *pool, uint32_t id, size_t *out_len);

void flat_value_fprint(FILE *out, const FlatValue *v, const FlatStringPool *pool,
                       const FlatCollectionPool *coll);
void flat_value_fprint_ln(FILE *out, const FlatValue *v, const FlatStringPool *pool,
                          const FlatCollectionPool *coll);

int flat_value_set_bool(FlatValue *v, int b);
int flat_value_set_number(FlatValue *v, double n);
int flat_value_set_str_inline(FlatValue *v, const char *s, size_t len);
int flat_value_set_str_ref(FlatValue *v, uint32_t ref_id);
int flat_value_set_list(FlatValue *v, uint32_t list_id);
int flat_value_set_object(FlatValue *v, uint32_t obj_id);

FlatCollectionPool *flat_coll_create(void);
void flat_coll_destroy(FlatCollectionPool *pool);
int flat_list_empty(FlatCollectionPool *pool, uint32_t *out_id);
int flat_list_len(const FlatCollectionPool *pool, uint32_t list_id, int *out_len);
int flat_list_get(const FlatCollectionPool *pool, uint32_t list_id, int index, FlatValue *out);
int flat_list_push_copy(FlatCollectionPool *pool, uint32_t list_id, const FlatValue *item,
                        uint32_t *out_id);
int flat_list_build(FlatCollectionPool *pool, const FlatValue *items, int count, uint32_t *out_id);
int flat_obj_empty(FlatCollectionPool *pool, uint32_t *out_id);
int flat_obj_len(const FlatCollectionPool *pool, uint32_t obj_id, int *out_len);
int flat_obj_entry_at(const FlatCollectionPool *pool, uint32_t obj_id, int index,
                      const char **out_key, FlatValue *out_val);
int flat_obj_get(const FlatCollectionPool *pool, uint32_t obj_id, const char *key,
                 FlatValue *out);
int flat_obj_set_copy(FlatCollectionPool *pool, uint32_t obj_id, const char *key,
                      const FlatValue *val, uint32_t *out_id);
int flat_obj_build(FlatCollectionPool *pool, const char (*keys)[FLAT_OBJ_KEY_MAX + 1],
                   const FlatValue *vals, int count, uint32_t *out_id);

int flat_value_equals(const FlatValue *a, const FlatValue *b, const FlatStringPool *pool);
int flat_value_compare_number(const FlatValue *v, double rhs, int op); /* op: 0== 1>= 2<= 3> 4< */

int flat_value_get_cstr(const FlatValue *v, const FlatStringPool *pool, char *buf, size_t buf_size);
int flat_value_str_equal_cstr(const FlatValue *v, const FlatStringPool *pool, const char *lit,
                              size_t lit_len);

int flat_value_parse_assign_rhs(const char *rhs, FlatValue *out, FlatStringPool *pool,
                                FlatCollectionPool *coll, FlatValue *vars, int var_count,
                                const char (*var_names)[64]);

int flat_value_eval_guard(const char *expr, FlatValue *vars, int var_count,
                          const char (*names)[64], const FlatStringPool *pool);

int flat_io_exec_line(const char *line, FlatStringPool *pool, FlatCollectionPool *coll,
                      FlatValue *vars, int var_count, const char (*var_names)[64]);

#endif
