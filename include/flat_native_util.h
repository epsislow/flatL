#ifndef FLAT_NATIVE_UTIL_H
#define FLAT_NATIVE_UTIL_H

#include "flat_native.h"

#include <stddef.h>

char *flat_native_trim(char *s);
/* Unpinned assign-RHS (scalars / last operand). */
int flat_native_parse_expr(FlatNativeCtx *ctx, const char *expr, FlatValue *out);
/* Parse assign-RHS; pins LIST/OBJECT until flat_native_parse_rhs_release (multi-arg natives). */
int flat_native_parse_rhs(FlatNativeCtx *ctx, const char *expr, FlatValue *out);
void flat_native_parse_rhs_release(FlatNativeCtx *ctx, const FlatValue *v);
int flat_native_parse_dotted_call(const char *line, char *mod, size_t mod_max, char *func,
                                  size_t func_max, char *inner, size_t inner_max);
int flat_native_find_var(const char *name, int var_count, const char (*names)[64]);

#endif