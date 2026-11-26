#ifndef FLAT_NATIVE_PLUGIN_H
#define FLAT_NATIVE_PLUGIN_H

#include "flat_native.h"
#include "fgir.h"
#include "flat_native_capability.h"

typedef void (*FlatNativePluginRegisterFn)(uint16_t id, int (*handler)(FlatNativeCtx *));

/* Plugin initializers must register handlers synchronously on the initializer
 * thread. Registrations are staged and committed only after init returns 0. */
typedef int (*FlatNativePluginInitFn)(FlatNativePluginRegisterFn reg);
typedef FlatNativeThreadCapability (*FlatNativePluginCapabilityFn)(void);

#define FLAT_NATIVE_PLUGIN_INIT_SYMBOL "flat_native_plugin_init"
#define FLAT_NATIVE_PLUGIN_CAPABILITY_SYMBOL "flat_native_plugin_thread_capability"

/* Initializes process-wide embedded native handlers once; safe for concurrent callers. */
void flat_native_runtime_init(void);
int flat_native_runtime_manifest_has_module(const FlatNativeRuntimeContext *runtime,
                                            const char *module);
int flat_native_runtime_prepare(const FlatNativeRuntimeContext *runtime, uint16_t native_id,
                                char *err, size_t err_size);
/* Worker preflight: resolves/loads the module, rejects unsupported modules, and
 * returns THREAD_SAFE or SERIALIZED (PENDING is conservatively SERIALIZED). */
int flat_native_runtime_worker_policy(const FlatNativeRuntimeContext *runtime,
                                      uint16_t native_id,
                                      FlatNativeThreadCapability *policy_out,
                                      char *err, size_t err_size);
/* Atomically finish native initialization and freeze plugin/capability configuration. */
int flat_native_runtime_freeze_worker_capabilities(void);
/* Search directory is process-wide; updates are serialized with lazy plugin loading. */
void flat_native_runtime_set_search_dir(const char *dir);
void flat_native_runtime_set_search_dir_from_argv0(const char *argv0);

void flat_native_plugin_register(uint16_t id, int (*handler)(FlatNativeCtx *));
int flat_native_invoke_optional(uint16_t id, FlatNativeCtx *ctx);

#endif
