#ifndef FLAT_NATIVE_PLUGIN_H
#define FLAT_NATIVE_PLUGIN_H

#include "flat_native.h"
#include "fgir.h"

typedef void (*FlatNativePluginRegisterFn)(uint16_t id, int (*handler)(FlatNativeCtx *));

typedef int (*FlatNativePluginInitFn)(FlatNativePluginRegisterFn reg);

#define FLAT_NATIVE_PLUGIN_INIT_SYMBOL "flat_native_plugin_init"

void flat_native_runtime_reset(void);
void flat_native_runtime_set_manifest(const FlatOptionalNativeDecl *decls, int count);
int flat_native_runtime_prepare(uint16_t native_id, char *err, size_t err_size);
void flat_native_runtime_set_search_dir(const char *dir);
void flat_native_runtime_set_search_dir_from_argv0(const char *argv0);

void flat_native_plugin_register(uint16_t id, int (*handler)(FlatNativeCtx *));
int flat_native_invoke_optional(uint16_t id, FlatNativeCtx *ctx);

#endif
