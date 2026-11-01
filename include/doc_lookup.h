#ifndef FLAT_DOC_LOOKUP_H
#define FLAT_DOC_LOOKUP_H

#include "fb_format.h"

#include <stdio.h>

#define FLAT_DOC_LIST_CAP 25

typedef void (*flat_doc_line_cb)(void *userdata, const char *line);

int flat_doc_emit_cb(void *userdata, flat_doc_line_cb cb, const FlatBytecode *bc,
                     const char *query);
int flat_doc_emit(FILE *out, const FlatBytecode *bc, const char *query);

#endif
