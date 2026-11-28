#ifndef FLAT_WS_EVENT_CLIENT_H
#define FLAT_WS_EVENT_CLIENT_H

#include "flat_runtime_socket_event.h"
#include "flat_ws_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlatWsEventClient FlatWsEventClient;

/* Connect and start a background reader. socket_id is an ordinary numeric ID
 * chosen by the host. The reader publishes socket/open, socket/receive,
 * socket/close and socket/error events; it never enters a FlatL VM. The caller
 * must stop/destroy the client before destroying runtime. */
int flat_ws_event_client_start(FlatRuntime *runtime, uint64_t socket_id,
                               const char *url, long connect_timeout_ms,
                               size_t max_frame_bytes,
                               FlatWsEventClient **out_client,
                               char *error, size_t error_capacity);

/* Thread-safe send of one text or binary message. */
int flat_ws_event_client_send(FlatWsEventClient *client, FlatWsOpcode opcode,
                              const void *data, size_t length, long timeout_ms,
                              char *error, size_t error_capacity);

/* Stop the reader, publish close when appropriate, and release the client. */
void flat_ws_event_client_stop(FlatWsEventClient *client);

#ifdef __cplusplus
}
#endif
#endif
