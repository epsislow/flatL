#ifndef FLAT_NATIVE_H
#define FLAT_NATIVE_H

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
    FLAT_NATIVE_COUNT
} FlatNativeId;

#define FLAT_NATIVE_CORE_COUNT ((int)FLAT_NATIVE_MATH_TRIG_SIN)
#define FLAT_NATIVE_MODULE_MATH_TRIG "math.trig"
#define FLAT_NATIVE_MODULE_MATH_STATS "math.stats"
#define FLAT_NATIVE_MODULE_MATH "math"
#define FLAT_NATIVE_MODULE_LIST "list"

typedef struct {
    FlatStringPool *pool;
    FlatCollectionPool *coll;
    FlatValue *vars;
    int var_count;
    const char (*var_names)[64];
    const char *inner;
    FlatValue *out;
    FlatIoStatus *io_status;
} FlatNativeCtx;

int flat_native_is_core_module(const char *mod);
int flat_native_is_call_line(const char *line);
int flat_native_lookup(const char *mod, const char *func, uint16_t *out_id);
int flat_native_lookup_optional(const char *mod, const char *func, uint16_t *out_id);
int flat_native_split_call(const char *line, uint16_t *out_id, char *inner, size_t inner_max);
const char *flat_native_name(uint16_t id);
int flat_native_module_for_id(uint16_t id, char *mod_out, size_t mod_out_sz);
int flat_native_is_optional_id(uint16_t id);
int flat_native_invoke(uint16_t id, FlatNativeCtx *ctx);

#endif
