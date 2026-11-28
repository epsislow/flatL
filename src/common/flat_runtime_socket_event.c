#include "flat_runtime_socket_event.h"

static const char *socket_event_name(FlatSocketEvent event) {
    switch (event) {
        case FLAT_SOCKET_EVENT_OPEN: return "open";
        case FLAT_SOCKET_EVENT_RECEIVE: return "receive";
        case FLAT_SOCKET_EVENT_CLOSE: return "close";
        case FLAT_SOCKET_EVENT_ERROR: return "error";
        default: return NULL;
    }
}

int flat_runtime_socket_event_publish(FlatRuntime *runtime, uint64_t socket_id,
                                      FlatSocketEvent event,
                                      const FlatValue *payload,
                                      FlatCollectionPool *source_coll,
                                      FlatStringPool *source_pool,
                                      struct FlatFileTable *source_files) {
    const char *name = socket_event_name(event);
    if (!runtime || socket_id == 0 || !name || !payload || !source_coll || !source_pool)
        return -1;
    return flat_runtime_event_publish(runtime, socket_id, "socket", name, payload,
                                      source_coll, source_pool, source_files);
}
