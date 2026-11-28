#include "flat_runtime.h"
#include "flat_runtime_event.h"
#include "flat_number.h"
#include "flat_vcx.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    FlatRuntime *runtime = NULL;
    FlatCollectionPool *source_coll = NULL, *dest_coll = NULL;
    FlatStringPool *source_pool = NULL, *dest_pool = NULL;
    FlatEventSubscriptionId first = 0, second = 0, other = 0;
    FlatValue payload, received;
    int rc;
    int i;
    memset(&payload, 0, sizeof payload);
    memset(&received, 0, sizeof received);

    runtime = flat_runtime_create(NULL);
    source_coll = flat_coll_create();
    source_pool = flat_pool_create();
    dest_coll = flat_coll_create();
    dest_pool = flat_pool_create();
    if (!runtime || !source_coll || !source_pool || !dest_coll || !dest_pool) return 2;
    if (flat_value_set_number(&payload, 42.5) != 0) return 3;

    if (flat_runtime_event_subscribe(runtime, 7, "socket", "receive", &first) != 0 ||
        flat_runtime_event_subscribe(runtime, 7, "socket", "receive", &second) != 0 ||
        flat_runtime_event_subscribe(runtime, 7, "timer", "receive", &other) != 0) return 4;
    if (flat_runtime_destroy(runtime) == 0) return 15;

    rc = flat_runtime_event_publish(runtime, 7, "socket", "receive", &payload,
                                    source_coll, source_pool, NULL);
    if (rc != 2) return 5;
    if (flat_runtime_event_pending(runtime, first) != 1 ||
        flat_runtime_event_pending(runtime, second) != 1 ||
        flat_runtime_event_pending(runtime, other) != 0) return 16;
    if (flat_runtime_event_next(runtime, first, dest_coll, dest_pool, NULL, &received) != 1 ||
        received.tag != VAL_NUMBER || flat_number_as_f64(&received) != 42.5) return 6;
    flat_value_release(dest_coll, dest_pool, &received);
    memset(&received, 0, sizeof received);
    if (flat_runtime_event_pending(runtime, first) != 0 ||
        flat_runtime_event_pending(runtime, second) != 1) return 17;
    if (flat_runtime_event_next(runtime, second, dest_coll, dest_pool, NULL, &received) != 1 ||
        received.tag != VAL_NUMBER || flat_number_as_f64(&received) != 42.5) return 7;
    flat_value_release(dest_coll, dest_pool, &received);
    memset(&received, 0, sizeof received);
    if (flat_runtime_event_next(runtime, first, dest_coll, dest_pool, NULL, &received) != 0 ||
        flat_runtime_event_next(runtime, other, dest_coll, dest_pool, NULL, &received) != 0) return 8;

    for (i = 0; i < FLAT_RUNTIME_EVENT_QUEUE_MAX; ++i) {
        if (flat_runtime_event_publish(runtime, 7, "socket", "receive", &payload,
                                       source_coll, source_pool, NULL) != 2) return 9;
    }
    if (flat_runtime_event_publish(runtime, 7, "socket", "receive", &payload,
                                   source_coll, source_pool, NULL) != -2) return 10;
    for (i = 0; i < FLAT_RUNTIME_EVENT_QUEUE_MAX; ++i) {
        if (flat_runtime_event_next(runtime, first, dest_coll, dest_pool, NULL, &received) != 1 ||
            received.tag != VAL_NUMBER || flat_number_as_f64(&received) != 42.5) return 11;
        flat_value_release(dest_coll, dest_pool, &received);
        memset(&received, 0, sizeof received);
    }
    if (flat_runtime_event_pending(runtime, first) != 0 ||
        flat_runtime_event_pending(runtime, second) != FLAT_RUNTIME_EVENT_QUEUE_MAX ||
        flat_runtime_event_pending(runtime, other) != 0) return 18;
    if (flat_runtime_event_unsubscribe(runtime, first) != 0 ||
        flat_runtime_event_unsubscribe(runtime, second) != 0 ||
        flat_runtime_event_unsubscribe(runtime, other) != 0) return 12;
    if (flat_runtime_event_publish(runtime, 7, "socket", "receive", &payload,
                                   source_coll, source_pool, NULL) != 0) return 13;
    if (flat_runtime_destroy(runtime) != 0) return 14;

    flat_coll_destroy(source_coll);
    flat_pool_destroy(source_pool);
    flat_coll_destroy(dest_coll);
    flat_pool_destroy(dest_pool);
    puts("runtime-event-source-broker-ok");
    return 0;
}
