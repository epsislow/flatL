#ifndef FLAT_RUNTIME_EVENT_H
#define FLAT_RUNTIME_EVENT_H

#include "flat_runtime.h"
#include "flat_value.h"
#include <stdint.h>

struct FlatFileTable;

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t FlatEventSubscriptionId;

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
/* Publish one value to every matching subscription. Values are deep-copied;
 * file handles cannot be published. Returns the number of matching subscriptions,
 * 0 when there are no listeners, -1 on invalid/error, or -2 on queue overflow. */
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

#ifdef __cplusplus
}
#endif
#endif
