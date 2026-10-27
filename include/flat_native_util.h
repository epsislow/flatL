#ifndef FLAT_NATIVE_UTIL_H
#define FLAT_NATIVE_UTIL_H

#include "flat_value.h"

#include <stddef.h>

char *flat_native_trim(char *s);
int flat_native_parse_dotted_call(const char *line, char *mod, size_t mod_max, char *func,
                                  size_t func_max, char *inner, size_t inner_max);
int flat_native_find_var(const char *name, int var_count, const char (*names)[64]);

#endif
