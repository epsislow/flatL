#ifndef FLAT_NATIVE_H
#define FLAT_NATIVE_H

#include "fgir.h"
#include "flat_value.h"

#include <stdint.h>

typedef enum {
    FLAT_NATIVE_IO_OUTLN = 0,
    FLAT_NATIVE_IO_OUT,
    FLAT_NATIVE_IO_OUTVAR,
    FLAT_NATIVE_IO_IN,
    FLAT_NATIVE_IO_INLN,
    FLAT_NATIVE_IO_IN_NUM,
    FLAT_NATIVE_IO_IN_BOOL,
    FLAT_NATIVE_IO_IN_LIST,
    FLAT_NATIVE_IO_IN_OBJECT,
    FLAT_NATIVE_IO_IN_BOOLS,
    FLAT_NATIVE_STR_TO_NUM,
    FLAT_NATIVE_STR_TO_BOOL,
    FLAT_NATIVE_STR_LEN,
    FLAT_NATIVE_STR_REPEAT,
    FLAT_NATIVE_STR_CONCAT,
    FLAT_NATIVE_STR_SLICE,
    FLAT_NATIVE_STR_INDEX_OF,
    FLAT_NATIVE_STR_CONTAINS,
    FLAT_NATIVE_STR_STARTS_WITH,
    FLAT_NATIVE_STR_ENDS_WITH,
    FLAT_NATIVE_STR_TRIM,
    FLAT_NATIVE_STR_SPLIT,
    FLAT_NATIVE_STR_JOIN,
    FLAT_NATIVE_STR_LOWER,
    FLAT_NATIVE_STR_UPPER,
    FLAT_NATIVE_STR_REPLACE,
    FLAT_NATIVE_STR_REPLACE_ALL,
    FLAT_NATIVE_JSON_PARSE,
    FLAT_NATIVE_JSON_PARSE_LIST,
    FLAT_NATIVE_JSON_PARSE_OBJECT,
    FLAT_NATIVE_JSON_PARSE_BOOLS,
    FLAT_NATIVE_JSON_STRINGIFY,
    FLAT_NATIVE_MATH_TRIG_SIN,
    FLAT_NATIVE_MATH_TRIG_COS,
    FLAT_NATIVE_MATH_TRIG_TAN,
    FLAT_NATIVE_MATH_TRIG_NUM_PI,
    FLAT_NATIVE_MATH_TRIG_NUM_TAU,
    FLAT_NATIVE_MATH_TRIG_DEG_TO_RAD,
    FLAT_NATIVE_MATH_TRIG_RAD_TO_DEG,
    FLAT_NATIVE_MATH_TRIG_SIN_DEG,
    FLAT_NATIVE_MATH_TRIG_COS_DEG,
    FLAT_NATIVE_MATH_TRIG_TAN_DEG,
    FLAT_NATIVE_MATH_STATS_MEAN,
    FLAT_NATIVE_MATH_STATS_MEDIAN,
    FLAT_NATIVE_MATH_STATS_VAR,
    FLAT_NATIVE_MATH_STATS_STDEV,
    FLAT_NATIVE_MATH_POW,
    FLAT_NATIVE_MATH_SQRT,
    FLAT_NATIVE_MATH_EXP,
    FLAT_NATIVE_MATH_LOG,
    FLAT_NATIVE_MATH_LOG10,
    FLAT_NATIVE_MATH_FLOOR,
    FLAT_NATIVE_MATH_CEIL,
    FLAT_NATIVE_MATH_ROUND,
    FLAT_NATIVE_MATH_MOD,
    FLAT_NATIVE_MATH_FLOOR_MOD,
    FLAT_NATIVE_MATH_SUM,
    FLAT_NATIVE_MATH_MIN,
    FLAT_NATIVE_MATH_MAX,
    FLAT_NATIVE_MATH_SUM2,
    FLAT_NATIVE_MATH_MIN2,
    FLAT_NATIVE_MATH_MAX2,
    FLAT_NATIVE_MATH_NUM_PI,
    FLAT_NATIVE_MATH_NUM_TAU,
    FLAT_NATIVE_MATH_NUM_E,
    FLAT_NATIVE_LIST_EMPTY,
    FLAT_NATIVE_LIST_PACK,
    FLAT_NATIVE_LIST_PUSH,
    FLAT_NATIVE_LIST_GET,
    FLAT_NATIVE_LIST_LEN,
    FLAT_NATIVE_LIST_PREPEND,
    FLAT_NATIVE_LIST_INSERT,
    FLAT_NATIVE_LIST_SET,
    FLAT_NATIVE_LIST_INDEX_OF,
    FLAT_NATIVE_LIST_CONTAINS,
    FLAT_NATIVE_LIST_REMOVE_AT,
    FLAT_NATIVE_LIST_SLICE,
    FLAT_NATIVE_LIST_SPLICE,
    FLAT_NATIVE_LIST_RANGE,
    FLAT_NATIVE_LIST_REPEAT,
    FLAT_NATIVE_OBJ_EMPTY,
    FLAT_NATIVE_OBJ_GET,
    FLAT_NATIVE_OBJ_SET,
    FLAT_NATIVE_OBJ_HAS,
    FLAT_NATIVE_OBJ_LEN,
    FLAT_NATIVE_OBJ_REMOVE,
    FLAT_NATIVE_OBJ_KEYS,
    FLAT_NATIVE_OBJ_VALUES,
    FLAT_NATIVE_MEM_VARS_MAX,
    FLAT_NATIVE_MEM_VARS_USED,
    FLAT_NATIVE_MEM_VARS_AVAIL,
    FLAT_NATIVE_MEM_LIST_MAX,
    FLAT_NATIVE_MEM_LIST_USED,
    FLAT_NATIVE_MEM_LIST_AVAIL,
    FLAT_NATIVE_MEM_OBJECT_MAX,
    FLAT_NATIVE_MEM_OBJECT_USED,
    FLAT_NATIVE_MEM_OBJECT_AVAIL,
    FLAT_NATIVE_MEM_STRREF_MAX,
    FLAT_NATIVE_MEM_STRREF_USED,
    FLAT_NATIVE_MEM_STRREF_AVAIL,
    FLAT_NATIVE_MEM_STR_USED,
    FLAT_NATIVE_MEM_BOOL_USED,
    FLAT_NATIVE_MEM_NUM_USED,
    FLAT_NATIVE_MEM_NULL_USED,
    FLAT_NATIVE_COUNT
} FlatNativeId;

#define FLAT_NATIVE_CORE_COUNT ((int)FLAT_NATIVE_MATH_TRIG_SIN)
#define FLAT_NATIVE_MODULE_MATH_TRIG "math.trig"
#define FLAT_NATIVE_MODULE_MATH_STATS "math.stats"
#define FLAT_NATIVE_MODULE_MATH "math"
#define FLAT_NATIVE_MODULE_LIST "list"
#define FLAT_NATIVE_MODULE_OBJ "obj"
#define FLAT_NATIVE_MODULE_MEM "mem"

struct FlatMemLiveStats;

typedef struct {
    FlatStringPool *pool;
    FlatCollectionPool *coll;
    FlatValue *vars;
    int var_count;
    const char (*var_names)[64];
    const char *inner;
    FlatValue *out;
    FlatIoStatus *io_status;
    struct FlatMemLiveStats *mem_live;
    uint32_t vars_max;
} FlatNativeCtx;

int flat_native_is_core_module(const char *mod);
int flat_native_is_call_line(const char *line);
int flat_native_lookup(const char *mod, const char *func, uint16_t *out_id);
int flat_native_lookup_optional(const char *mod, const char *func, uint16_t *out_id);
int flat_native_split_call(const char *line, uint16_t *out_id, char *inner, size_t inner_max);
int flat_native_split_call_manifest(const char *line, const FlatOptionalNativeDecl *decls,
                                    int decl_count, uint16_t *out_id, char *inner,
                                    size_t inner_max);
const char *flat_native_name(uint16_t id);
int flat_native_module_for_id(uint16_t id, char *mod_out, size_t mod_out_sz);
int flat_native_is_optional_id(uint16_t id);
int flat_native_invoke(uint16_t id, FlatNativeCtx *ctx);

#endif
