#ifndef GEN_DOC_INDEX_H
#define GEN_DOC_INDEX_H

#include <stddef.h>

#define GEN_DOC_MAX_SECTIONS 64
#define GEN_DOC_ID_MAX 64
#define GEN_DOC_LABEL_MAX 128
#define GEN_DOC_FILE_MAX 256
#define GEN_DOC_TITLE_MAX 128

typedef struct {
    char id[GEN_DOC_ID_MAX];
    char label[GEN_DOC_LABEL_MAX];
    char file[GEN_DOC_FILE_MAX];
    char *markdown;
} GenDocSection;

typedef struct {
    char title[GEN_DOC_TITLE_MAX];
    GenDocSection sections[GEN_DOC_MAX_SECTIONS];
    int section_count;
} GenDocIndex;

int gen_doc_index_load(const char *docs_dir, const char *index_path, GenDocIndex *out,
                       char *err, size_t err_size);
void gen_doc_index_free(GenDocIndex *idx);

#endif
