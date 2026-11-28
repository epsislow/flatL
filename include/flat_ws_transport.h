#ifndef FLAT_WS_TRANSPORT_H
#define FLAT_WS_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

typedef struct FlatWsConnection FlatWsConnection;

typedef enum FlatWsOpcode {
    FLAT_WS_TEXT = 1,
    FLAT_WS_BINARY = 2,
    FLAT_WS_CONTINUATION = 3,
    FLAT_WS_CLOSE = 8,
    FLAT_WS_PING = 9,
    FLAT_WS_PONG = 10
} FlatWsOpcode;

typedef struct FlatWsMessage {
    unsigned char *data;
    size_t length;
    FlatWsOpcode opcode;
} FlatWsMessage;

/* Initialize libcurl's process-wide state. Safe to call more than once. */
int flat_ws_prepare(void);
/* Returns 1 only when the linked libcurl build advertises ws and wss protocols. */
int flat_ws_supported(void);

/* Connect to ws:// or wss://. This performs the HTTP Upgrade handshake and
 * returns a connected transport. timeout_ms must be positive. */
int flat_ws_connect(const char *url, long timeout_ms,
                    FlatWsConnection **out_connection,
                    char *error, size_t error_capacity);

/* Send one complete text or binary WebSocket frame. Returns 0 on success. */
int flat_ws_send(FlatWsConnection *connection, FlatWsOpcode opcode,
                 const void *data, size_t length,
                 long timeout_ms, char *error, size_t error_capacity);

/* Receive one WebSocket frame. The returned payload is heap-owned and must be
 * released with flat_ws_message_free(). A timeout returns 1, an error -1. */
int flat_ws_receive(FlatWsConnection *connection, long timeout_ms,
                    size_t max_frame_bytes, FlatWsMessage *out_message,
                    char *error, size_t error_capacity);
void flat_ws_message_free(FlatWsMessage *message);

/* Send a close frame (when possible) and release the transport. */
void flat_ws_close(FlatWsConnection *connection, long timeout_ms);

#endif
