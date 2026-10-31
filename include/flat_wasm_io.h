#ifndef FLAT_WASM_IO_H
#define FLAT_WASM_IO_H

#include "flat_value.h"

#include <stddef.h>

struct FlatStringPool;
struct FlatCollectionPool;

void flat_wasm_io_write_stdout(const char *s, size_t n);

void flat_wasm_io_fprint_io(const FlatValue *v, const struct FlatStringPool *pool,
                            const struct FlatCollectionPool *coll);
void flat_wasm_io_fprint_io_ln(const FlatValue *v, const struct FlatStringPool *pool,
                               const struct FlatCollectionPool *coll);

/* Returns 0 on success; sets *st (FLAT_IO_OK, PANIC_*, etc.). */
int flat_wasm_io_read_line(char *buf, size_t buf_size, int allow_empty, FlatIoStatus *st);

int flat_wasm_io_read_char(char *ch_out, FlatIoStatus *st);

#endif
