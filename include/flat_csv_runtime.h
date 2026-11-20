#ifndef FLAT_CSV_RUNTIME_H
#define FLAT_CSV_RUNTIME_H

#include "flat_csv.h"
#include "flat_file.h"
#include "flat_value.h"

#include <stdint.h>

#define FLAT_CSV_MAX_SESSIONS 32

typedef struct FlatCsvRuntime FlatCsvRuntime;

typedef enum {
    FLAT_CSV_SESS_READER = 1,
    FLAT_CSV_SESS_WRITER = 2
} FlatCsvSessionKind;

FlatCsvRuntime *flat_csv_runtime_create(void);
void flat_csv_runtime_destroy(FlatCsvRuntime *rt);

int flat_csv_fp_busy(const FlatCsvRuntime *rt, uint32_t fp_id);
int flat_csv_fp_session_kind(const FlatCsvRuntime *rt, uint32_t fp_id);
int flat_csv_session_live(const FlatCsvRuntime *rt, uint32_t id, int *out_kind);

uint32_t flat_csv_reader_open(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t fp_id,
                              char *panic, size_t panic_sz);
uint32_t flat_csv_writer_open(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t fp_id,
                              char *panic, size_t panic_sz);
int flat_csv_close(FlatCsvRuntime *rt, uint32_t sess_id, FlatFileTable *files, char *panic,
                   size_t panic_sz);
int flat_csv_view_create(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t parent_id,
                         int x, int y, int width, int height, uint32_t *out_id,
                         char *panic, size_t panic_sz);
int flat_csv_view_close(FlatCsvRuntime *rt, uint32_t view_id, char *panic, size_t panic_sz);
int flat_csv_view_resolve(FlatCsvRuntime *rt, uint32_t id, uint32_t *out_root,
                          int *out_x, int *out_y, int *out_width, int *out_height,
                          int *out_kind, int *out_cursor_row);
int flat_csv_view_set_cursor(FlatCsvRuntime *rt, uint32_t id, int cursor_row);

int flat_csv_reader_reset(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t rid);
int flat_csv_reader_is_eof(const FlatCsvRuntime *rt, uint32_t rid, int *out_eof);
int flat_csv_reader_rows(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t rid, int *out_rows);
int flat_csv_reader_cols(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t rid, int *out_cols);
int flat_csv_reader_get_row(const FlatCsvRuntime *rt, uint32_t rid, int *out_row);
int flat_csv_reader_get_col(const FlatCsvRuntime *rt, uint32_t rid, int *out_col);
int flat_csv_reader_set_row(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t rid, int row,
                            char *panic, size_t panic_sz);
int flat_csv_reader_next(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t rid,
                         FlatCollectionPool *coll, FlatStringPool *pool, uint32_t *out_list_id,
                         char *panic, size_t panic_sz);
int flat_csv_reader_next_cells(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t rid, int len,
                               FlatCollectionPool *coll, FlatStringPool *pool, uint32_t *out_list_id,
                               char *panic, size_t panic_sz);
int flat_csv_reader_get_cells(FlatCsvRuntime *rt, FlatFileTable *files, uint32_t rid, int x0,
                                int y0, int xLen, int yLen, int as_object,
                                FlatCollectionPool *coll, FlatStringPool *pool,
                                uint32_t *out_coll_id, int *out_is_object, char *panic,
                                size_t panic_sz);

int flat_csv_format_set_separator(FlatCsvRuntime *rt, uint32_t sess_id, const char *val,
                                  char *panic, size_t panic_sz);
int flat_csv_format_set_line_endings(FlatCsvRuntime *rt, uint32_t sess_id, const char *val,
                                     char *panic, size_t panic_sz);
int flat_csv_format_set_encoding(FlatCsvRuntime *rt, uint32_t sess_id, const char *val,
                                 char *panic, size_t panic_sz);
int flat_csv_format_set_quote(FlatCsvRuntime *rt, uint32_t sess_id, const char *val, char *panic,
                              size_t panic_sz);

int flat_csv_writer_set_cell(FlatCsvRuntime *rt, uint32_t view_id, int x, int y,
                              const char *value, char *panic, size_t panic_sz);
int flat_csv_writer_write_rows(FlatCsvRuntime *rt, uint32_t wid, const FlatValue *rows_val,
                               FlatCollectionPool *coll, FlatStringPool *pool, char *panic,
                               size_t panic_sz);
int flat_csv_writer_write_cols(FlatCsvRuntime *rt, uint32_t wid, const FlatValue *cols_val,
                               FlatCollectionPool *coll, FlatStringPool *pool, char *panic,
                               size_t panic_sz);
int flat_csv_writer_flush(FlatCsvRuntime *rt, uint32_t wid, FlatFileTable *files, char *panic,
                          size_t panic_sz);

#endif