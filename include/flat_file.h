#ifndef FLAT_FILE_H
#define FLAT_FILE_H

#include "flat_value.h"

#include <stdio.h>

#define FLAT_FILE_MAX_HANDLES 64

typedef enum {
    FLAT_FILE_MODE_READ,
    FLAT_FILE_MODE_CREATE,
    FLAT_FILE_MODE_WRITE_AT0,
    FLAT_FILE_MODE_APPEND,
    FLAT_FILE_MODE_RDWR
} FlatFileMode;

typedef struct {
    FILE *fp;
    unsigned char in_use;
    unsigned char closed;
    FlatFileMode mode;
    int refcnt;
} FlatFileSlot;

typedef struct FlatFileTable {
    FlatFileSlot slots[FLAT_FILE_MAX_HANDLES];
} FlatFileTable;

void flat_file_table_init(FlatFileTable *tbl);
void flat_file_table_close_all(FlatFileTable *tbl);

int flat_file_open(FlatFileTable *tbl, const char *path, FlatFileMode mode, uint32_t *out_id);
int flat_file_close(FlatFileTable *tbl, uint32_t id);
int flat_file_is_live(const FlatFileTable *tbl, uint32_t id);
FILE *flat_file_fp(FlatFileTable *tbl, uint32_t id);
/* Set file size to len bytes (write-capable handle; not read-only). */
int flat_file_truncate(FlatFileTable *tbl, uint32_t id, uint64_t len);
/* Seek 0 and truncate to empty (write-capable handle). */
int flat_file_truncate_zero(FlatFileTable *tbl, uint32_t id);

void flat_file_retain(FlatFileTable *tbl, uint32_t id);
void flat_file_release(FlatFileTable *tbl, uint32_t id);

int flat_value_set_file_handle(FlatValue *v, uint32_t file_id);

/* fp is a whole NUMBER; live slot id in [0, FLAT_FILE_MAX_HANDLES). */
int flat_value_get_file_handle(const FlatValue *v, const FlatFileTable *tbl, uint32_t *out_id);

#endif
