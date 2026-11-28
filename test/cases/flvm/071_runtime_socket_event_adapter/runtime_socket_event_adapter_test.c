#include "flat_runtime.h"
#include "flat_runtime_event.h"
#include "flat_runtime_socket_event.h"
#include "flat_number.h"

#include <stdio.h>
#include <string.h>

static int expect_number(FlatRuntime *runtime, FlatEventSubscriptionId sub,
                         FlatCollectionPool *coll, FlatStringPool *pool,
                         double expected) {
    FlatValue out;
    int rc;
    memset(&out, 0, sizeof out);
    rc = flat_runtime_event_next(runtime, sub, coll, pool, NULL, &out);
    if (rc != 1 || out.tag != VAL_NUMBER || flat_number_as_f64(&out) != expected) {
        flat_value_release(coll, pool, &out);
        return -1;
    }
    flat_value_release(coll, pool, &out);
    return 0;
}

int main(void) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    FlatCollectionPool *source_coll = flat_coll_create();
    FlatCollectionPool *dest_coll = flat_coll_create();
    FlatStringPool *source_pool = flat_pool_create();
    FlatStringPool *dest_pool = flat_pool_create();
    FlatEventSubscriptionId open_sub = 0, receive_sub = 0, close_sub = 0, error_sub = 0, late_open_sub = 0;
    FlatValue payload;
    int rc = 1;

    memset(&payload, 0, sizeof payload);
    if (!runtime || !source_coll || !dest_coll || !source_pool || !dest_pool) goto done;
    if (flat_value_set_number(&payload, 1) != 0) goto done;
    /* A WebSocket's open event can arrive before FlatL executes spawnFor.
     * A later subscription must receive the retained one-shot notification. */
    if (flat_runtime_socket_event_publish(runtime, 42, FLAT_SOCKET_EVENT_OPEN, &payload,
                                          source_coll, source_pool, NULL) != 0 ||
        flat_runtime_event_subscribe(runtime, 42, "socket", "open", &late_open_sub) != 0) goto done;
    {
        FlatValue replay;
        memset(&replay, 0, sizeof replay);
        if (flat_runtime_event_next(runtime, late_open_sub, dest_coll, dest_pool, NULL, &replay) != 1 ||
            replay.tag != VAL_NULL) {
            flat_value_release(dest_coll, dest_pool, &replay);
            goto done;
        }
        flat_value_release(dest_coll, dest_pool, &replay);
    }
    if (flat_runtime_event_unsubscribe(runtime, late_open_sub) != 0) goto done;
    late_open_sub = 0;
    /* Receive frames that arrive before task.spawnFor are retained in a
     * bounded per-socket FIFO and replayed to the first later subscriber. */
    if (flat_value_set_number(&payload, 10) != 0) goto done;
    if (flat_runtime_socket_event_publish(runtime, 44, FLAT_SOCKET_EVENT_RECEIVE, &payload, source_coll, source_pool, NULL) != 0) goto done;
    if (flat_value_set_number(&payload, 11) != 0) goto done;
    if (flat_runtime_socket_event_publish(runtime, 44, FLAT_SOCKET_EVENT_RECEIVE, &payload, source_coll, source_pool, NULL) != 0) goto done;
    if (flat_runtime_event_subscribe(runtime, 44, "socket", "receive", &late_open_sub) != 0) goto done;
    if (flat_runtime_event_pending(runtime, late_open_sub) != 2) goto done;
    if (expect_number(runtime, late_open_sub, dest_coll, dest_pool, 10) != 0) goto done;
    if (expect_number(runtime, late_open_sub, dest_coll, dest_pool, 11) != 0) goto done;
    if (flat_runtime_event_unsubscribe(runtime, late_open_sub) != 0) goto done;
    late_open_sub = 0;
    /* The pre-subscription buffer is bounded at 256 events. It rejects the
     * next frame rather than silently dropping data or growing without limit. */
    {
        int i;
        for (i = 0; i < 256; ++i) {
            if (flat_value_set_number(&payload, (double)i) != 0 ||
                flat_runtime_socket_event_publish(runtime, 45, FLAT_SOCKET_EVENT_RECEIVE, &payload,
                                                  source_coll, source_pool, NULL) != 0) goto done;
        }
        if (flat_runtime_socket_event_publish(runtime,45,FLAT_SOCKET_EVENT_RECEIVE,&payload,source_coll,source_pool,NULL) != -3) goto done;
        if (flat_runtime_event_subscribe(runtime, 45, "socket", "receive", &late_open_sub) != 0) goto done;
        if (flat_runtime_event_pending(runtime, late_open_sub) != 256) goto done;
        for (i = 0; i < 256; ++i)
            if (expect_number(runtime, late_open_sub, dest_coll, dest_pool, (double)i) != 0) goto done;
    }
    if (flat_runtime_event_unsubscribe(runtime, late_open_sub) != 0) goto done;
    late_open_sub = 0;
    /* The retained notification is one-shot: a second late subscriber does
     * not receive a duplicate. */
    if (flat_runtime_event_subscribe(runtime, 42, "socket", "open", &late_open_sub) != 0 ||
        flat_runtime_event_pending(runtime, late_open_sub) != 0) goto done;
    if (flat_runtime_event_unsubscribe(runtime, late_open_sub) != 0) goto done;
    late_open_sub = 0;
    /* Closing before the first subscription also discards the stale open. */
    if (flat_runtime_socket_event_publish(runtime, 43, FLAT_SOCKET_EVENT_OPEN, &payload,
                                          source_coll, source_pool, NULL) != 0 ||
        flat_runtime_socket_event_publish(runtime, 43, FLAT_SOCKET_EVENT_CLOSE, &payload,
                                          source_coll, source_pool, NULL) != 0 ||
        flat_runtime_event_subscribe(runtime, 43, "socket", "open", &late_open_sub) != 0 ||
        flat_runtime_event_pending(runtime, late_open_sub) != 0) goto done;
    if (flat_runtime_event_unsubscribe(runtime, late_open_sub) != 0) goto done;
    late_open_sub = 0;
    if (flat_value_set_number(&payload, 1) != 0 ||
        flat_runtime_event_subscribe(runtime, 41, "socket", "open", &open_sub) != 0 ||
        flat_runtime_event_subscribe(runtime, 41, "socket", "receive", &receive_sub) != 0 ||
        flat_runtime_event_subscribe(runtime, 41, "socket", "close", &close_sub) != 0 ||
        flat_runtime_event_subscribe(runtime, 41, "socket", "error", &error_sub) != 0) goto done;

    if (flat_runtime_socket_event_publish(runtime, 41, FLAT_SOCKET_EVENT_OPEN, &payload,
                                          source_coll, source_pool, NULL) != 1) goto done;
    if (flat_value_set_number(&payload, 2) != 0 ||
        flat_runtime_socket_event_publish(runtime, 41, FLAT_SOCKET_EVENT_RECEIVE, &payload,
                                          source_coll, source_pool, NULL) != 1) goto done;
    if (flat_value_set_number(&payload, 3) != 0 ||
        flat_runtime_socket_event_publish(runtime, 41, FLAT_SOCKET_EVENT_CLOSE, &payload,
                                          source_coll, source_pool, NULL) != 1) goto done;
    if (flat_value_set_number(&payload, 4) != 0 ||
        flat_runtime_socket_event_publish(runtime, 41, FLAT_SOCKET_EVENT_ERROR, &payload,
                                          source_coll, source_pool, NULL) != 1) goto done;

    if (expect_number(runtime, open_sub, dest_coll, dest_pool, 1) != 0 ||
        expect_number(runtime, receive_sub, dest_coll, dest_pool, 2) != 0 ||
        expect_number(runtime, close_sub, dest_coll, dest_pool, 3) != 0 ||
        expect_number(runtime, error_sub, dest_coll, dest_pool, 4) != 0) goto done;

    if (flat_runtime_socket_event_publish(runtime, 0, FLAT_SOCKET_EVENT_RECEIVE, &payload,
                                          source_coll, source_pool, NULL) != -1 ||
        flat_runtime_socket_event_publish(runtime, 41, (FlatSocketEvent)99, &payload,
                                          source_coll, source_pool, NULL) != -1) goto done;

    if (flat_runtime_event_unsubscribe(runtime, open_sub) != 0 ||
        flat_runtime_event_unsubscribe(runtime, receive_sub) != 0 ||
        flat_runtime_event_unsubscribe(runtime, close_sub) != 0 ||
        flat_runtime_event_unsubscribe(runtime, error_sub) != 0) goto done;
    open_sub = receive_sub = close_sub = error_sub = 0;
    if (flat_runtime_destroy(runtime) != 0) goto done;
    runtime = NULL;
    puts("runtime-socket-event-adapter-ok");
    rc = 0;

done:
    if (runtime) {
        if (open_sub) flat_runtime_event_unsubscribe(runtime, open_sub);
        if (receive_sub) flat_runtime_event_unsubscribe(runtime, receive_sub);
        if (close_sub) flat_runtime_event_unsubscribe(runtime, close_sub);
        if (error_sub) flat_runtime_event_unsubscribe(runtime, error_sub);
        if (late_open_sub) flat_runtime_event_unsubscribe(runtime, late_open_sub);
        flat_runtime_destroy(runtime);
    }
    if (source_coll) flat_coll_destroy(source_coll);
    if (dest_coll) flat_coll_destroy(dest_coll);
    if (source_pool) flat_pool_destroy(source_pool);
    if (dest_pool) flat_pool_destroy(dest_pool);
    if (rc) fprintf(stderr, "runtime socket event adapter failed\n");
    return rc;
}
