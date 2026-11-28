#ifndef FLAT_SOCKETS_NATIVE_H
#define FLAT_SOCKETS_NATIVE_H
#include "flat_native.h"
int flat_native_sockets_ws_supported(FlatNativeCtx *ctx);
int flat_native_sockets_ws_connect(FlatNativeCtx *ctx);
int flat_native_sockets_ws_send(FlatNativeCtx *ctx);
int flat_native_sockets_ws_send_binary(FlatNativeCtx *ctx);
int flat_native_sockets_ws_close(FlatNativeCtx *ctx);
#endif
