#ifndef FLAT_RUNTIME_H
#define FLAT_RUNTIME_H

#include "flat_host_config.h"
#include "vm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlatRuntime FlatRuntime;

/*
 * Create an independent runtime instance. If config is NULL, the legacy
 * process staging configuration is captured. Otherwise config is deep-copied.
 * The caller may destroy or modify its source config after this call returns.
 */
FlatRuntime *flat_runtime_create(const FlatHostConfig *config);

/*
 * Execute bytecode using this runtime's immutable host configuration.
 * opts may be NULL. opts->host_config is ignored: runtime configuration is
 * authoritative. All other options, including output pointers, are forwarded.
 * Multiple runs may use one runtime concurrently while it remains alive. Runtime
 * destruction requires the host to prevent new API calls from starting concurrently.
 */
int flat_runtime_run(FlatRuntime *runtime, const FlatBytecode *bc,
                     const FlatVmOptions *opts);

/*
 * Destroy an idle runtime with no registered tasks or Channels. Returns 0 on
 * success, -1 for invalid/busy runtime. Every task and Channel must be explicitly
 * destroyed first.
 * The host must prevent new API calls from starting concurrently with destroy.
 */
int flat_runtime_destroy(FlatRuntime *runtime);

#ifdef __cplusplus
}
#endif

#endif
