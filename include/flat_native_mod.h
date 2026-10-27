#ifndef FLAT_NATIVE_MOD_H
#define FLAT_NATIVE_MOD_H

#include "flat_native.h"

int flat_native_io_outln(FlatNativeCtx *ctx);
int flat_native_io_out(FlatNativeCtx *ctx);
int flat_native_io_outvar(FlatNativeCtx *ctx);
int flat_native_io_in(FlatNativeCtx *ctx);
int flat_native_io_inln(FlatNativeCtx *ctx);
int flat_native_io_in_num(FlatNativeCtx *ctx);
int flat_native_io_in_bool(FlatNativeCtx *ctx);
int flat_native_io_in_list(FlatNativeCtx *ctx);
int flat_native_io_in_object(FlatNativeCtx *ctx);
int flat_native_io_in_bools(FlatNativeCtx *ctx);
int flat_native_str_to_num(FlatNativeCtx *ctx);
int flat_native_str_to_bool(FlatNativeCtx *ctx);
int flat_native_json_parse(FlatNativeCtx *ctx);
int flat_native_json_parse_list(FlatNativeCtx *ctx);
int flat_native_json_parse_object(FlatNativeCtx *ctx);
int flat_native_json_parse_bools(FlatNativeCtx *ctx);
int flat_native_json_stringify(FlatNativeCtx *ctx);

#endif
