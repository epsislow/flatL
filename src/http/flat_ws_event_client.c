#include "flat_ws_event_client.h"
#include "flat_sync.h"
#include "flat_value.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

struct FlatWsEventClient {
    FlatRuntime *runtime;
    uint64_t socket_id;
    FlatWsConnection *connection;
    FlatThread reader;
    FlatMutex mutex;
    atomic_int stopping;
    atomic_int terminal_published;
    size_t max_frame_bytes;
    int reader_started;
};

static void ws_client_error(char *dst, size_t cap, const char *message) {
    if (dst && cap) snprintf(dst, cap, "%s", message ? message : "WebSocket event client error");
}

static void ws_client_pause(void) {
#ifdef _WIN32
    Sleep(10);
#else
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 10000000L;
    (void)nanosleep(&ts, NULL);
#endif
}

static int ws_publish_value(FlatWsEventClient *client, FlatSocketEvent event,
                            FlatValue *value, FlatCollectionPool *coll,
                            FlatStringPool *pool) {
    int rc;
    do {
        rc = flat_runtime_socket_event_publish(client->runtime, client->socket_id,
                                               event, value, coll, pool, NULL);
        if (rc != -2 || atomic_load_explicit(&client->stopping, memory_order_acquire))
            break;
        ws_client_pause();
    } while (1);
    return rc;
}

static int ws_publish_empty(FlatWsEventClient *client, FlatSocketEvent event) {
    FlatCollectionPool *coll = flat_coll_create();
    FlatStringPool *pool = flat_pool_create();
    FlatValue value;
    int rc = -1;
    memset(&value, 0, sizeof value);
    if (!coll || !pool || flat_value_set_null(&value) != 0) goto done;
    rc = ws_publish_value(client, event, &value, coll, pool);
done:
    flat_value_release(coll, pool, &value);
    if (coll) flat_coll_destroy(coll);
    if (pool) flat_pool_destroy(pool);
    return rc;
}

static int ws_publish_string(FlatWsEventClient *client, FlatSocketEvent event,
                             const void *bytes, size_t length) {
    FlatCollectionPool *coll = flat_coll_create();
    FlatStringPool *pool = flat_pool_create();
    FlatValue value;
    uint32_t id = 0;
    int rc = -1;
    memset(&value, 0, sizeof value);
    if (!coll || !pool || flat_pool_alloc_copy(pool, bytes, length, &id) != 0 ||
        flat_value_set_str_ref(&value, id) != 0) goto done;
    flat_value_retain(coll, pool, &value);
    rc = ws_publish_value(client, event, &value, coll, pool);
done:
    flat_value_release(coll, pool, &value);
    if (coll) flat_coll_destroy(coll);
    if (pool) flat_pool_destroy(pool);
    return rc;
}

static int ws_publish_message(FlatWsEventClient *client, const FlatWsMessage *message) {
    FlatCollectionPool *coll = flat_coll_create();
    FlatStringPool *pool = flat_pool_create();
    FlatValue payload, values[2];
    char keys[2][FLAT_OBJ_KEY_MAX + 1] = {{0}};
    uint32_t data_id = 0, object_id = 0;
    int rc = -1;
    memset(&payload, 0, sizeof payload);
    memset(values, 0, sizeof values);
    memcpy(keys[0], "opcode", sizeof "opcode");
    memcpy(keys[1], "data", sizeof "data");
    if (!coll || !pool ||
        flat_value_set_number(&values[0], (double)message->opcode) != 0 ||
        flat_pool_alloc_copy(pool, message->data, message->length, &data_id) != 0 ||
        flat_value_set_str_ref(&values[1], data_id) != 0) goto done;
    flat_value_retain(coll, pool, &values[1]);
    if (flat_obj_build(coll, pool, (const char (*)[FLAT_OBJ_KEY_MAX + 1])keys,
                       values, 2, &object_id) != 0 ||
        flat_value_set_object(&payload, object_id) != 0) goto done;
    flat_value_retain(coll, pool, &payload);
    rc = ws_publish_value(client, FLAT_SOCKET_EVENT_RECEIVE, &payload, coll, pool);
done:
    flat_value_release(coll, pool, &payload);
    flat_value_release(coll, pool, &values[1]);
    if (coll) flat_coll_destroy(coll);
    if (pool) flat_pool_destroy(pool);
    return rc;
}

static void *ws_reader_main(void *opaque) {
    FlatWsEventClient *client = (FlatWsEventClient *)opaque;
    int terminal = 0;
    while (!atomic_load_explicit(&client->stopping, memory_order_acquire)) {
        FlatWsMessage message;
        char error[256] = {0};
        int rc;
        memset(&message, 0, sizeof message);
        rc = flat_ws_receive(client->connection, 100, client->max_frame_bytes,
                             &message, error, sizeof error);
        if (rc == 1) continue;
        if (rc != 0) {
            (void)ws_publish_string(client, FLAT_SOCKET_EVENT_ERROR,
                                    error, strlen(error));
            terminal = 1;
            break;
        }
        if (message.opcode == FLAT_WS_CLOSE) {
            static const unsigned char close_payload[2] = {0x03, 0xE8};
            char close_error[128] = {0};
            (void)flat_ws_send(client->connection, FLAT_WS_CLOSE, close_payload,
                               sizeof close_payload, 500, close_error, sizeof close_error);
            flat_ws_message_free(&message);
            terminal = 1;
            break;
        }
        if (message.opcode == FLAT_WS_PING) {
            char send_error[128] = {0};
            (void)flat_ws_send(client->connection, FLAT_WS_PONG,
                               message.data, message.length, 500, send_error, sizeof send_error);
            flat_ws_message_free(&message);
            continue;
        }
        if (message.opcode == FLAT_WS_PONG) {
            flat_ws_message_free(&message);
            continue;
        }
        {
            int publish_rc = ws_publish_message(client, &message);
            if (publish_rc < 0) {
                const char *reason = publish_rc == -2
                    ? "WebSocket receive subscriber queue is full"
                    : publish_rc == -3
                        ? "WebSocket pre-subscription receive buffer is full"
                        : "WebSocket receive event publication failed";
                (void)ws_publish_string(client, FLAT_SOCKET_EVENT_ERROR,
                                        reason, strlen(reason));
                flat_ws_message_free(&message);
                terminal = 1;
                break;
            }
        }
        flat_ws_message_free(&message);
    }
    if (!atomic_exchange_explicit(&client->terminal_published, 1, memory_order_acq_rel))
        (void)ws_publish_empty(client, FLAT_SOCKET_EVENT_CLOSE);
    (void)terminal;
    return NULL;
}

int flat_ws_event_client_start(FlatRuntime *runtime, uint64_t socket_id,
                               const char *url, long connect_timeout_ms,
                               size_t max_frame_bytes,
                               FlatWsEventClient **out_client,
                               char *error, size_t error_capacity) {
    FlatWsEventClient *client;
    if (out_client) *out_client = NULL;
    if (!runtime || socket_id == 0 || !url || !out_client ||
        connect_timeout_ms <= 0 || max_frame_bytes == 0 || !flat_threads_supported()) {
        ws_client_error(error, error_capacity, "invalid WebSocket event client arguments or threads unavailable");
        return -1;
    }
    client = (FlatWsEventClient *)calloc(1, sizeof *client);
    if (!client) { ws_client_error(error, error_capacity, "out of memory"); return -1; }
    client->runtime = runtime;
    client->socket_id = socket_id;
    client->max_frame_bytes = max_frame_bytes;
    atomic_init(&client->stopping, 0);
    atomic_init(&client->terminal_published, 0);
    if (flat_mutex_init(&client->mutex) != 0) {
        free(client);
        ws_client_error(error, error_capacity, "WebSocket event client mutex initialization failed");
        return -1;
    }
    if (flat_ws_connect(url, connect_timeout_ms, &client->connection, error, error_capacity) != 0) {
        flat_mutex_destroy(&client->mutex);
        free(client);
        return -1;
    }
    if (ws_publish_empty(client, FLAT_SOCKET_EVENT_OPEN) < 0) {
        atomic_store_explicit(&client->stopping, 1, memory_order_release);
        flat_ws_close(client->connection, 100);
        flat_mutex_destroy(&client->mutex);
        free(client);
        ws_client_error(error, error_capacity, "unable to publish WebSocket open event");
        return -1;
    }
    if (flat_thread_create(&client->reader, ws_reader_main, client) != 0) {
        atomic_store_explicit(&client->stopping, 1, memory_order_release);
        (void)ws_publish_empty(client, FLAT_SOCKET_EVENT_CLOSE);
        flat_ws_close(client->connection, 100);
        flat_mutex_destroy(&client->mutex);
        free(client);
        ws_client_error(error, error_capacity, "unable to start WebSocket event reader");
        return -1;
    }
    client->reader_started = 1;
    *out_client = client;
    if (error && error_capacity) error[0] = '\0';
    return 0;
}

int flat_ws_event_client_send(FlatWsEventClient *client, FlatWsOpcode opcode,
                              const void *data, size_t length, long timeout_ms,
                              char *error, size_t error_capacity) {
    int rc;
    if (!client) { ws_client_error(error, error_capacity, "invalid WebSocket event client"); return -1; }
    flat_mutex_lock(&client->mutex);
    if (atomic_load_explicit(&client->stopping, memory_order_acquire)) {
        flat_mutex_unlock(&client->mutex);
        ws_client_error(error, error_capacity, "WebSocket event client is stopping");
        return -1;
    }
    rc = flat_ws_send(client->connection, opcode, data, length, timeout_ms, error, error_capacity);
    flat_mutex_unlock(&client->mutex);
    return rc;
}

void flat_ws_event_client_stop(FlatWsEventClient *client) {
    if (!client) return;
    flat_mutex_lock(&client->mutex);
    atomic_store_explicit(&client->stopping, 1, memory_order_release);
    flat_mutex_unlock(&client->mutex);
    if (client->reader_started) (void)flat_thread_join(&client->reader);
    flat_ws_close(client->connection, 100);
    if (!atomic_exchange_explicit(&client->terminal_published, 1, memory_order_acq_rel))
        (void)ws_publish_empty(client, FLAT_SOCKET_EVENT_CLOSE);
    flat_mutex_destroy(&client->mutex);
    free(client);
}
