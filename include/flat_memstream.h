#ifndef FLAT_MEMSTREAM_H
#define FLAT_MEMSTREAM_H

#include <stddef.h>
#include <stdio.h>

/* open_memstream-like API portable to MSYS2/MinGW (no GNU open_memstream). */
FILE *flat_open_memstream(char **buf, size_t *len);

/* Use instead of fclose() for streams from flat_open_memstream. */
int flat_close_memstream(FILE *stream);

#endif
