#ifndef FLAT_WASM_IO_H
#define FLAT_WASM_IO_H

#include "flat_value.h"

#include <stddef.h>

struct FlatStringPool;
struct FlatCollectionPool;

/** Passed to fleditor for modal titles (suffix labels). */
enum FlatWasmIoInputKind {
    FLAT_WASM_IO_KIND_LN = 0,
    FLAT_WASM_IO_KIND_NUM = 1,
    FLAT_WASM_IO_KIND_BOOL = 2,
    FLAT_WASM_IO_KIND_LIST = 3,
    FLAT_WASM_IO_KIND_OBJECT = 4,
    FLAT_WASM_IO_KIND_BOOLS = 5,
};

void flat_wasm_io_write_stdout(const char *s, size_t n);
void flat_wasm_io_write_stderr(const char *s, size_t n);

void flat_wasm_io_fprint_io(const FlatValue *v, const struct FlatStringPool *pool,
                            const struct FlatCollectionPool *coll);
void flat_wasm_io_fprint_io_ln(const FlatValue *v, const struct FlatStringPool *pool,
                               const struct FlatCollectionPool *coll);

/* prompt may be NULL or ""; input_kind is FlatWasmIoInputKind. */
int flat_wasm_io_read_line(char *buf, size_t buf_size, int allow_empty, const char *prompt,
                           int input_kind, FlatIoStatus *st);

int flat_wasm_io_read_char(char *ch_out, const char *prompt, FlatIoStatus *st);

#endif
