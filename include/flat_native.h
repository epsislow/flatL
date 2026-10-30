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
    FLAT_NATIVE_MATH_TRIG_SQRT,
    FLAT_NATIVE_COUNT
} FlatNativeId;

#define FLAT_NATIVE_CORE_COUNT ((int)FLAT_NATIVE_MATH_TRIG_SIN)
#define FLAT_NATIVE_MODULE_MATH_TRIG "math.trig"

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
