#ifndef FLAT_STR_H
#define FLAT_STR_H

#include <stddef.h>

/* Pure C string helpers (no strtok). See docs/PORTABILITY.md */

char *flat_str_trim(char *s);

/* Split at delim; null-terminates token; advances *cursor past delim. */
char *flat_str_next(char **cursor, char delim);

/* Split inner arg list at top-level commas (respects quotes and bracket depth). */
int flat_str_split_three(char *inner, char **first, char **second, char **third);

/*
 * Bounded string join / assign-line builders (memcpy + length checks).
 * Prefer these over snprintf(..., "%s", big_rhs) into FGIR-sized buffers so
 * GCC -Wformat-truncation stays quiet. Returns 0 or -1 if cap exceeded.
 */
int flat_str_join(char *out, size_t out_sz, const char *a, const char *b);
int flat_str_assign_line(char *out, size_t cap, const char *lhs, const char *rhs);
int flat_str_assign_fl_tmp(char *out, size_t cap, int tmp_index, const char *rhs);

#endif
