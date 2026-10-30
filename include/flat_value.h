#ifndef FLAT_VALUE_H
#define FLAT_VALUE_H

#include "fgir.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Documented in docs/LANGUAGE_TYPES.md */
#define FLAT_INLINE_STR_MAX 127
#define FLAT_VAR_NAME_MAX 63
#define FLAT_MAX_VARS 32768
#define FLAT_POOL_MAX_SLOTS 1024
#define FLAT_POOL_MAX_CHUNK (16u * 1024u * 1024u)
#define FLAT_POOL_MAX_TOTAL (64u * 1024u * 1024u)

#define FLAT_COLL_MAX_LISTS 256
#define FLAT_COLL_MAX_OBJECTS 256
#define FLAT_LIST_MAX_ELEMS 1024
#define FLAT_OBJ_MAX_KEYS 128
#define FLAT_OBJ_KEY_MAX 127

#define FLAT_JSON_MAX_DEPTH 128

typedef enum {
    VAL_EMPTY,
    VAL_BOOL,
    VAL_NUMBER,
    VAL_STR_INLINE,
    VAL_STR_REF,
    VAL_LIST,
    VAL_OBJECT,
    VAL_NULL
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
typedef struct FlatMemLiveStats FlatMemLiveStats;

FlatStringPool *flat_pool_create(void);
void flat_pool_destroy(FlatStringPool *pool);

int flat_pool_alloc_copy(FlatStringPool *pool, const void *data, size_t len, uint32_t *out_id);
int flat_pool_slice(FlatStringPool *pool, uint32_t parent_id, size_t offset, size_t len,
                    uint32_t *out_id);
const uint8_t *flat_pool_read(const FlatStringPool *pool, uint32_t id, size_t *out_len);
/* refcnt = number of binds (variables, list/object elements) + slice pins on the slot.
 * alloc_copy/slice start at 0; the first bind is flat_value_retain. release to 0 frees the slot
 * (owned bytes returned to the pool, slice views unpin their parent) and recycles the id. */
void flat_pool_retain(FlatStringPool *pool, uint32_t id);
void flat_pool_release(FlatStringPool *pool, uint32_t id);
int flat_pool_live_slot_count(const FlatStringPool *pool); /* <= FLAT_POOL_MAX_SLOTS */
size_t flat_pool_bytes_used(const FlatStringPool *pool);   /* owned bytes, <= FLAT_POOL_MAX_TOTAL */
void flat_pool_reap_idle(FlatStringPool *pool); /* free never-bound temporaries (refcnt 0) */

void flat_value_fprint_ex(FILE *out, const FlatValue *v, const FlatStringPool *pool,
                          const FlatCollectionPool *coll, int raw_strings);
void flat_value_fprint(FILE *out, const FlatValue *v, const FlatStringPool *pool,
                       const FlatCollectionPool *coll);
void flat_value_fprint_ln(FILE *out, const FlatValue *v, const FlatStringPool *pool,
                          const FlatCollectionPool *coll);
void flat_value_fprint_io(FILE *out, const FlatValue *v, const FlatStringPool *pool,
                          const FlatCollectionPool *coll);
void flat_value_fprint_io_ln(FILE *out, const FlatValue *v, const FlatStringPool *pool,
                             const FlatCollectionPool *coll);

int flat_value_set_bool(FlatValue *v, int b);
int flat_value_set_number(FlatValue *v, double n);
int flat_value_set_str_inline(FlatValue *v, const char *s, size_t len);
int flat_value_set_str_ref(FlatValue *v, uint32_t ref_id);
int flat_value_set_list(FlatValue *v, uint32_t list_id);
int flat_value_set_object(FlatValue *v, uint32_t obj_id);
int flat_value_set_null(FlatValue *v);

void flat_value_retain(FlatCollectionPool *coll, FlatStringPool *pool, const FlatValue *v);
void flat_value_release(FlatCollectionPool *coll, FlatStringPool *pool, const FlatValue *v);

FlatCollectionPool *flat_coll_create(void);
void flat_coll_destroy(FlatCollectionPool *pool);
void flat_coll_set_live_stats(FlatCollectionPool *pool, FlatMemLiveStats *stats);
void flat_coll_reap_idle(FlatCollectionPool *pool, FlatStringPool *str_pool);
int flat_coll_live_list_count(const FlatCollectionPool *pool);
int flat_coll_live_object_count(const FlatCollectionPool *pool);

int flat_list_empty(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t *out_id);
int flat_list_len(const FlatCollectionPool *pool, uint32_t list_id, int *out_len);
int flat_list_get(const FlatCollectionPool *pool, uint32_t list_id, int index, FlatValue *out);
int flat_list_push_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t list_id,
                        const FlatValue *item, uint32_t *out_id);
int flat_list_prepend_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t list_id,
                           const FlatValue *item, uint32_t *out_id);
int flat_list_insert_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t list_id,
                          int index, const FlatValue *item, uint32_t *out_id);
int flat_list_set_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t list_id,
                       int index, const FlatValue *val, uint32_t *out_id);
int flat_list_remove_at_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t list_id,
                             int index, uint32_t *out_id);
int flat_list_slice_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t list_id,
                         int start, int end, uint32_t *out_id);
int flat_list_splice_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t list_id,
                          int start, int count, uint32_t *out_id);
int flat_list_index_of(const FlatCollectionPool *pool, const FlatStringPool *str_pool,
                       uint32_t list_id, const FlatValue *needle, int *out_index);
int flat_list_build(FlatCollectionPool *pool, FlatStringPool *str_pool, const FlatValue *items,
                    int count, uint32_t *out_id);
int flat_obj_empty(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t *out_id);
int flat_obj_len(const FlatCollectionPool *pool, uint32_t obj_id, int *out_len);
int flat_obj_entry_at(const FlatCollectionPool *pool, uint32_t obj_id, int index,
                      const char **out_key, FlatValue *out_val);
int flat_obj_get(const FlatCollectionPool *pool, uint32_t obj_id, const char *key,
                 FlatValue *out);
int flat_obj_set_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t obj_id,
                      const char *key, const FlatValue *val, uint32_t *out_id);
int flat_obj_remove_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t obj_id,
                         const char *key, uint32_t *out_id);
int flat_obj_build(FlatCollectionPool *pool, FlatStringPool *str_pool,
                   const char (*keys)[FLAT_OBJ_KEY_MAX + 1], const FlatValue *vals, int count,
                   uint32_t *out_id);

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

typedef enum {
    FLAT_IO_OK = 0,
    FLAT_IO_ERR = 1,
    FLAT_IO_PANIC_EOF = 2,
    FLAT_IO_PANIC_CANCEL = 3,
    FLAT_IO_PANIC_PARSE = 4
} FlatIoStatus;

int flat_parse_bool_token(const char *token, int *out_bool);
int flat_parse_bool_cancel_token(const char *token);
int flat_parse_number_token(const char *token, double *out_num);

int flat_io_exec_line(const char *line, FlatStringPool *pool, FlatCollectionPool *coll,
                      FlatValue *vars, int var_count, const char (*var_names)[64],
                      FlatValue *out_result, FlatIoStatus *out_status);
int flat_io_exec_line_manifest(const char *line, FlatStringPool *pool, FlatCollectionPool *coll,
                               FlatValue *vars, int var_count, const char (*var_names)[64],
                               FlatValue *out_result, FlatIoStatus *out_status,
                               const FlatOptionalNativeDecl *decls, int decl_count);

int flat_json_parse(const char *s, FlatValue *out, FlatStringPool *pool, FlatCollectionPool *coll);
int flat_json_parse_list(const char *s, FlatValue *out, FlatStringPool *pool,
                         FlatCollectionPool *coll);
int flat_json_parse_object(const char *s, FlatValue *out, FlatStringPool *pool,
                           FlatCollectionPool *coll);
int flat_json_parse_bools(const char *s, FlatValue *out, FlatStringPool *pool,
                          FlatCollectionPool *coll);
int flat_json_parse_bools_io(const char *s, FlatValue *out, FlatStringPool *pool,
                             FlatCollectionPool *coll);
int flat_json_stringify(const FlatValue *v, FlatStringPool *pool, FlatCollectionPool *coll,
                        char *out, size_t out_size);
int flat_json_list_is_homogeneous_numbers(const FlatCollectionPool *coll, uint32_t list_id);
int flat_json_list_is_homogeneous_strings(const FlatCollectionPool *coll, uint32_t list_id);
int flat_json_fprint_list_io(FILE *out, const FlatValue *v, FlatStringPool *pool,
                             FlatCollectionPool *coll);
int flat_json_fprint_object_io(FILE *out, const FlatValue *v, FlatStringPool *pool,
                               FlatCollectionPool *coll);
int flat_json_fprint_null_io(FILE *out);

#endif
