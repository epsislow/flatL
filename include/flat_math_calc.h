#ifndef FLAT_MATH_CALC_H
#define FLAT_MATH_CALC_H

#include "flat_value.h"

#include <stddef.h>

typedef enum {
    FLAT_MATH_CALC_OK = 0,
    FLAT_MATH_CALC_SYNTAX,
    FLAT_MATH_CALC_UNKNOWN_VAR,
    FLAT_MATH_CALC_TYPE,
    FLAT_MATH_CALC_DIVZERO,
    FLAT_MATH_CALC_RANGE
} FlatMathCalcErr;

/* Writes full panic sub-reason (e.g. math.calc.syntax) into buf when buf_sz > 0. */
const char *flat_math_calc_reason(FlatMathCalcErr err, char *buf, size_t buf_sz);

/*
 * Evaluate arithmetic expression in expr (UTF-8, no surrounding quotes).
 * Variables resolved from vars/var_names; must be NUMBER.
 * On success writes result to *out and returns FLAT_MATH_CALC_OK.
 */
FlatMathCalcErr flat_math_calc_eval(const char *expr, const FlatValue *vars, int var_count,
                                    const char (*var_names)[64], double *out);

#endif
