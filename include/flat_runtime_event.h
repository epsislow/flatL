#ifndef FLAT_RUNTIME_EVENT_H
#define FLAT_RUNTIME_EVENT_H

#include "flat_runtime.h"
#include "flat_value.h"
#include "vm.h"
#include <stdint.h>

struct FlatFileTable;

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t FlatEventSubscriptionId;

typedef enum FlatEventDispatchMode {
    FLAT_EVENT_DISPATCH_PARALLEL = 0,
    FLAT_EVENT_DISPATCH_SERIAL = 1
} FlatEventDispatchMode;

typedef enum FlatEventOverflowPolicy {
    FLAT_EVENT_OVERFLOW_WAIT = 0,
    FLAT_EVENT_OVERFLOW_DROP_OLDEST = 1,
    FLAT_EVENT_OVERFLOW_DROP_NEWEST = 2,
    FLAT_EVENT_OVERFLOW_ERROR = 3
} FlatEventOverflowPolicy;

typedef struct FlatEventSubscriptionOptions {
    FlatEventDispatchMode mode;
    size_t max_active;
    size_t queue_capacity;
    FlatEventOverflowPolicy overflow;
} FlatEventSubscriptionOptions;

#define FLAT_EVENT_DEFAULT_QUEUE_CAPACITY 256

/* The first event-broker increment is deliberately host-driven: source IDs are
 * ordinary numeric values and source_kind identifies their namespace. */
#define FLAT_RUNTIME_EVENT_QUEUE_MAX 256

/* Register interest in one event from a numeric source. source_kind and
 * event_name are stable textual identifiers (for example "socket", "receive"). */
int flat_runtime_event_subscribe(FlatRuntime *runtime, uint64_t source_id,
                                 const char *source_kind, const char *event_name,
                                 FlatEventSubscriptionId *out_subscription_id);
/* Remove a subscription and discard its queued events. A runtime with active
 * subscriptions cannot be destroyed; unsubscribe each one first. */
int flat_runtime_event_unsubscribe(FlatRuntime *runtime,
                                   FlatEventSubscriptionId subscription_id);
/* Register a persistent runtime-owned handler. The handler is dispatched by the
 * scheduler whenever matching events are queued. bound_args are deep-copied at
 * registration and follow the event payload as positional handler parameters.
 * This compatibility entry point selects PARALLEL mode. */
int flat_runtime_event_subscribe_handler(FlatRuntime *runtime, uint64_t source_id,
                                         const char *source_kind, const char *event_name,
                                         const FlatBytecode *bc, const char *handler_label,
                                         const FlatValue *bound_args, size_t bound_arg_count,
                                         FlatCollectionPool *source_coll,
                                         FlatStringPool *source_pool,
                                         struct FlatFileTable *source_files,
                                         FlatEventSubscriptionId *out_subscription_id);
/* Same as subscribe_handler, with explicit dispatch policy. SERIAL keeps at most
 * one non-terminal handler task active per subscription; queued events remain FIFO. */
int flat_runtime_event_subscribe_handler_mode(FlatRuntime *runtime, uint64_t source_id,
                                              const char *source_kind, const char *event_name,
                                              const FlatBytecode *bc, const char *handler_label,
                                              const FlatValue *bound_args, size_t bound_arg_count,
                                              FlatCollectionPool *source_coll,
                                              FlatStringPool *source_pool,
                                              struct FlatFileTable *source_files,
                                              FlatEventDispatchMode mode,
                                              FlatEventSubscriptionId *out_subscription_id);
/* Same registration API with explicit dispatch, concurrency and queue policies.
 * queue_capacity must be positive. WAIT and ERROR reject publication atomically
 * when any matching queue is full; WAIT returns -2 (producer backpressure) and
 * ERROR returns -3 (hard overflow). DROP_OLDEST replaces that queue's oldest
 * item; DROP_NEWEST ignores the new item for the full subscription. */
int flat_runtime_event_subscribe_handler_config(FlatRuntime *runtime, uint64_t source_id,
                                                const char *source_kind, const char *event_name,
                                                const FlatBytecode *bc, const char *handler_label,
                                                const FlatValue *bound_args, size_t bound_arg_count,
                                                FlatCollectionPool *source_coll,
                                                FlatStringPool *source_pool,
                                                struct FlatFileTable *source_files,
                                                const FlatEventSubscriptionOptions *options,
                                                FlatEventSubscriptionId *out_subscription_id);
/* Compatibility API retained for existing C callers. */
int flat_runtime_event_subscribe_handler_options(FlatRuntime *runtime, uint64_t source_id,
                                                 const char *source_kind, const char *event_name,
                                                 const FlatBytecode *bc, const char *handler_label,
                                                 const FlatValue *bound_args, size_t bound_arg_count,
                                                 FlatCollectionPool *source_coll,
                                                 FlatStringPool *source_pool,
                                                 struct FlatFileTable *source_files,
                                                 FlatEventDispatchMode mode, size_t max_active,
                                                 FlatEventSubscriptionId *out_subscription_id);
/* Publish one value to every matching subscription. Values are deep-copied;
 * file handles cannot be published. Returns the number of matching subscriptions,
 * 0 when there are no listeners, -1 on invalid/internal error, -2 when a WAIT
 * subscription applies backpressure, or -3 when an ERROR subscription overflows. */
int flat_runtime_event_publish(FlatRuntime *runtime, uint64_t source_id,
                               const char *source_kind, const char *event_name,
                               const FlatValue *value,
                               FlatCollectionPool *source_coll,
                               FlatStringPool *source_pool,
                               struct FlatFileTable *source_files);
/* Read the next event for a subscription into caller-owned pools. Returns 1
 * when an event was copied, 0 when the queue is empty, and -1 on error. The
 * queued event is retained if copying fails. */
/* Return the number of queued events for one subscription, or -1 if invalid. */
int flat_runtime_event_pending(FlatRuntime *runtime,
                               FlatEventSubscriptionId subscription_id);
/* Consume the next event for one subscription. */
int flat_runtime_event_next(FlatRuntime *runtime,
                            FlatEventSubscriptionId subscription_id,
                            FlatCollectionPool *destination_coll,
                            FlatStringPool *destination_pool,
                            struct FlatFileTable *destination_files,
                            FlatValue *out_value);
/* Consume one queued event and create a normal runnable task for handler_label.
 * The event is bound as parameter 0; bound_args follow it positionally. The
 * event remains queued if validation, copying or task creation fails. Returns
 * 1 when a task was created, 0 when no event is queued, -1 on runtime/dispatch
 * error, and -2 when the handler parameter count does not match. Only one
 * dispatcher may consume a given subscription at a time. */
int flat_runtime_event_spawn_next(FlatRuntime *runtime,
                                  FlatEventSubscriptionId subscription_id,
                                  const FlatBytecode *bc, const char *handler_label,
                                  const FlatValue *bound_args, size_t bound_arg_count,
                                  FlatCollectionPool *source_coll,
                                  FlatStringPool *source_pool,
                                  struct FlatFileTable *source_files,
                                  uint64_t *out_task_id);

#ifdef __cplusplus
}
#endif
#endif
