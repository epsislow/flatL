#ifndef FLAT_RUNTIME_H
#define FLAT_RUNTIME_H

#include "flat_host_config.h"
#include "vm.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlatRuntime FlatRuntime;

/* Runtime admission/shutdown lifecycle. */
typedef enum FlatRuntimeShutdownMode {
    FLAT_RUNTIME_SHUTDOWN_DRAIN = 1,
    FLAT_RUNTIME_SHUTDOWN_CANCEL = 2
} FlatRuntimeShutdownMode;

typedef enum FlatRuntimeLifecycle {
    FLAT_RUNTIME_RUNNING = 1,
    FLAT_RUNTIME_DRAINING = 2,
    FLAT_RUNTIME_CANCELLING = 3,
    FLAT_RUNTIME_STOPPED = 4
} FlatRuntimeLifecycle;

typedef enum FlatRuntimeShutdownStatus {
    FLAT_RUNTIME_SHUTDOWN_IN_PROGRESS = 0,
    FLAT_RUNTIME_SHUTDOWN_BLOCKED = 1,
    FLAT_RUNTIME_SHUTDOWN_COMPLETE = 2
} FlatRuntimeShutdownStatus;

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
 * Multiple runs may use one runtime concurrently while it remains alive, but
 * direct runs are rejected while a worker pool is starting or active. Runtime
 * destruction requires the host to prevent new API calls from starting concurrently.
 */
int flat_runtime_run(FlatRuntime *runtime, const FlatBytecode *bc,
                     const FlatVmOptions *opts);

/* Stop admitting new task submissions. DRAIN lets started tasks finish and
 * leaves CREATED tasks untouched. CANCEL escalates DRAIN and requests cleanup
 * for every nonterminal task. Repeated requests are idempotent; DRAIN cannot
 * downgrade CANCELLING. Returns 0 on acceptance, -1 for invalid arguments. */
int flat_runtime_shutdown_request(FlatRuntime *runtime, FlatRuntimeShutdownMode mode);

/* Start a fixed worker pool. Each worker owns at most one task quantum at a time;
 * flat_vm_step() always runs outside scheduler_mutex. Startup preflights each VM's
 * optional-native manifest, loads permitted plugins, initializes HTTP when declared,
 * rejects unsupported modules, then freezes capability declarations before creating
 * threads. New tasks are preflighted before publication while worker mode is active.
 * Returns -1 on unsupported platforms, invalid count/quantum, duplicate startup,
 * unsupported native declarations, or partial startup failure. */
int flat_runtime_workers_start(FlatRuntime *runtime, size_t worker_count,
                               uint64_t quantum);
/* Join workers after shutdown reaches STOPPED. DRAIN blocked on unresolved WAITING
 * tasks cannot be joined; escalate to CANCEL or resolve those waits first. A worker
 * error also permits joining after its cancellation request; the host may then use
 * flat_runtime_schedule() to finish cleanup. Returns -1 if incomplete or an error
 * occurred (all successfully joined handles are still released). */
int flat_runtime_workers_join(FlatRuntime *runtime);

/* Inspect shutdown progress. IN_PROGRESS means workers/schedule may make progress;
 * BLOCKED means DRAIN has only unresolved WAITING tasks; COMPLETE means no started
 * work (DRAIN) or no nonterminal work (CANCEL) remains. COMPLETE is not a join. */
FlatRuntimeShutdownStatus flat_runtime_shutdown_status(FlatRuntime *runtime);
FlatRuntimeLifecycle flat_runtime_lifecycle(FlatRuntime *runtime);

/*
 * Destroy an idle runtime with no registered tasks or Channels. Returns 0 on
 * success, -1 for invalid/busy runtime. Every task and Channel must be explicitly
 * destroyed first, and any worker pool must have been joined.
 * The host must prevent new API calls from starting concurrently with destroy.
 */
int flat_runtime_destroy(FlatRuntime *runtime);

#ifdef __cplusplus
}
#endif

#endif
