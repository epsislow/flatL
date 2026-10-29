#ifndef FLAT_STR_H
#define FLAT_STR_H

/* Pure C string helpers (no strtok). See docs/PORTABILITY.md */

char *flat_str_trim(char *s);

/* Split at delim; null-terminates token; advances *cursor past delim. */
char *flat_str_next(char **cursor, char delim);

#endif
