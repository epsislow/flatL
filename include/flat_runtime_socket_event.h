#ifndef FLAT_RUNTIME_SOCKET_EVENT_H
#define FLAT_RUNTIME_SOCKET_EVENT_H

#include "flat_runtime_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Socket transports share one source namespace regardless of whether the
 * underlying transport is WebSocket, TCP or UDP. Source IDs remain ordinary
 * numeric values allocated by the transport adapter. */
typedef enum FlatSocketEvent {
    FLAT_SOCKET_EVENT_OPEN = 1,
    FLAT_SOCKET_EVENT_RECEIVE = 2,
    FLAT_SOCKET_EVENT_CLOSE = 3,
    FLAT_SOCKET_EVENT_ERROR = 4
} FlatSocketEvent;

/* Publish one socket lifecycle/data event through the generic runtime broker.
 * Payload ownership follows flat_runtime_event_publish(): the value is
 * deep-copied into every matching subscription before this call returns.
 * Returns the number of matching subscriptions, or the broker's error code. */
int flat_runtime_socket_event_publish(FlatRuntime *runtime, uint64_t socket_id,
                                      FlatSocketEvent event,
                                      const FlatValue *payload,
                                      FlatCollectionPool *source_coll,
                                      FlatStringPool *source_pool,
                                      struct FlatFileTable *source_files);

#ifdef __cplusplus
}
#endif
#endif
