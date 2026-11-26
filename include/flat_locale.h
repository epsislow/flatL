#ifndef FLAT_LOCALE_H
#define FLAT_LOCALE_H

#include <stdarg.h>
#include <stddef.h>

/* Locale-independent numeric conversion/formatting. These helpers never call
 * setlocale() and therefore never mutate the embedding process locale. */
double flat_strtod(const char *text, char **end);
int flat_vsnprintf(char *dst, size_t size, const char *format, va_list args);
int flat_snprintf(char *dst, size_t size, const char *format, ...);

#endif
