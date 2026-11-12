#ifndef FLAT_MATH_CALC_H
#define FLAT_MATH_CALC_H

#include "flat_value.h"
#include "flat_slot_type.h"

#include <stddef.h>

typedef enum {
    FLAT_MATH_CALC_OK = 0,
    FLAT_MATH_CALC_SYNTAX,
    FLAT_MATH_CALC_UNKNOWN_VAR,
    FLAT_MATH_CALC_TYPE,
    FLAT_MATH_CALC_DIVZERO,
    FLAT_MATH_CALC_RANGE,
    FLAT_MATH_CALC_TYPE_STRICT
} FlatMathCalcErr;

/* Writes full panic sub-reason (e.g. math.calc.syntax) into buf when buf_sz > 0. */
const char *flat_math_calc_reason(FlatMathCalcErr err, char *buf, size_t buf_sz);

/*
 * Evaluate arithmetic expression in expr (UTF-8, no surrounding quotes).
 * Variables resolved from vars/var_names; must be NUMBER.
 * On success writes result to *out and returns FLAT_MATH_CALC_OK.
 */
FlatMathCalcErr flat_math_calc_eval(const char *expr, const FlatValue *vars, int var_count,
                                    const char (*var_names)[64], FlatStringPool *pool,
                                    FlatCollectionPool *coll, double *out);

/*
 * Like flat_math_calc_eval; when var_has_slot_type is set, enforces homogeneous numeric
 * contracts (type.calc.type). On FLAT_MATH_CALC_TYPE_STRICT, type_panic receives e.g.
 * type.calc.type (buf may be NULL to skip).
 */
FlatMathCalcErr flat_math_calc_eval_strict(
    const char *expr, const FlatValue *vars, int var_count, const char (*var_names)[64],
    FlatStringPool *pool, FlatCollectionPool *coll, const FlatSlotType *var_slot_types,
    const uint8_t *var_has_slot_type, double *out, char *type_panic, size_t type_panic_sz);

#endif
