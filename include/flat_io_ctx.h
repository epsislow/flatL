#ifndef FLAT_IO_CTX_H
#define FLAT_IO_CTX_H

#include "flat_file.h"
#include "flat_native.h"

typedef struct FlatIOContext FlatIOContext;

FlatIOContext *flat_io_ctx_create(FlatFileTable *files);
void flat_io_ctx_destroy(FlatIOContext *io);

void flat_io_ctx_bind_vars(FlatIOContext *io, FlatValue *vars, int var_count);

int flat_io_ctx_set_in(FlatIOContext *io, FlatNativeCtx *ctx, FlatIoStatus *st);
int flat_io_ctx_set_out(FlatIOContext *io, FlatNativeCtx *ctx, FlatIoStatus *st);
int flat_io_ctx_set_err(FlatIOContext *io, FlatNativeCtx *ctx, FlatIoStatus *st);

int flat_io_ctx_write(FlatIOContext *io, FlatNativeCtx *ctx, int stream, const FlatValue *v,
                      int with_nl);
int flat_io_ctx_read_line(FlatIOContext *io, FlatNativeCtx *ctx, char *buf, size_t buf_size,
                          int allow_empty, int input_kind, FlatIoStatus *st);
int flat_io_ctx_read_char(FlatIOContext *io, FlatNativeCtx *ctx, char *ch_out, FlatIoStatus *st);

void flat_io_ctx_var_updated(FlatIOContext *io, int var_index, const FlatValue *new_val);
void flat_io_ctx_file_closed(FlatIOContext *io, uint32_t file_id);

void flat_io_ctx_reset_terminals(FlatIOContext *io);

const char *flat_io_ctx_panic_reason(const FlatIOContext *io);

#endif
