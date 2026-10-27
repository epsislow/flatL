#ifndef FLAT_VALUE_H
#define FLAT_VALUE_H

#include <stddef.h>
#include <stdint.h>

/* Documented in docs/LANGUAGE_TYPES.md */
#define FLAT_INLINE_STR_MAX 127
#define FLAT_VAR_NAME_MAX 63
#define FLAT_MAX_VARS 128
#define FLAT_POOL_MAX_SLOTS 1024
#define FLAT_POOL_MAX_CHUNK (16u * 1024u * 1024u)
#define FLAT_POOL_MAX_TOTAL (64u * 1024u * 1024u)

typedef enum {
    VAL_EMPTY,
    VAL_BOOL,
    VAL_NUMBER,
    VAL_STR_INLINE,
    VAL_STR_REF
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
    } u;
} FlatValue;

typedef struct FlatStringPool FlatStringPool;

FlatStringPool *flat_pool_create(void);
void flat_pool_destroy(FlatStringPool *pool);

int flat_pool_alloc_copy(FlatStringPool *pool, const void *data, size_t len, uint32_t *out_id);
int flat_pool_slice(FlatStringPool *pool, uint32_t parent_id, size_t offset, size_t len,
                    uint32_t *out_id);

int flat_value_set_bool(FlatValue *v, int b);
int flat_value_set_number(FlatValue *v, double n);
int flat_value_set_str_inline(FlatValue *v, const char *s, size_t len);
int flat_value_set_str_ref(FlatValue *v, uint32_t ref_id);

int flat_value_equals(const FlatValue *a, const FlatValue *b, const FlatStringPool *pool);
int flat_value_compare_number(const FlatValue *v, double rhs, int op); /* op: 0== 1>= 2<= 3> 4< */

int flat_value_get_cstr(const FlatValue *v, const FlatStringPool *pool, char *buf, size_t buf_size);
int flat_value_str_equal_cstr(const FlatValue *v, const FlatStringPool *pool, const char *lit,
                              size_t lit_len);

int flat_value_parse_assign_rhs(const char *rhs, FlatValue *out, FlatStringPool *pool,
                                FlatValue *vars, int var_count, const char (*var_names)[64]);

int flat_value_eval_guard(const char *expr, FlatValue *vars, int var_count,
                          const char (*names)[64], const FlatStringPool *pool);

#endif
