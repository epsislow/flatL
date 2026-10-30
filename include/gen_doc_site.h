#ifndef GEN_DOC_SITE_H
#define GEN_DOC_SITE_H

#include "gen_doc_index.h"

#include <stddef.h>

int gen_doc_remove_tree(const char *path, char *err, size_t err_size);
int gen_doc_copy_templates(const char *templates_dir, const char *out_dir, char *err,
                           size_t err_size);
int gen_doc_emit_js(const char *out_path, const GenDocIndex *idx, char *err, size_t err_size);

#endif
