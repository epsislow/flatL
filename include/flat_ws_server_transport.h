#ifndef FLAT_WS_SERVER_TRANSPORT_H
#define FLAT_WS_SERVER_TRANSPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlatWsServerListener FlatWsServerListener;

typedef enum FlatWsServerOpcode {
    FLAT_WS_SERVER_TEXT = 1, FLAT_WS_SERVER_BINARY = 2,
    FLAT_WS_SERVER_CLOSE = 8, FLAT_WS_SERVER_PING = 9, FLAT_WS_SERVER_PONG = 10
} FlatWsServerOpcode;
typedef struct FlatWsServerMessage {
    FlatWsServerOpcode opcode;
    unsigned char *data;
    unsigned long length;
} FlatWsServerMessage;

/* Native TCP listening socket used by the WebSocket server transport.
 * This low-level layer only owns the listening endpoint; accepted peers are
 * upgraded and managed by the WebSocket protocol layer. */
int flat_ws_server_listener_open(const char *host, uint16_t port, int backlog,
                                 FlatWsServerListener **out_listener,
                                 uint16_t *out_bound_port,
                                 char *error, unsigned long error_capacity);
/* Accept one TCP peer. timeout_ms: 0 = non-blocking, -1 = wait indefinitely. */
int flat_ws_server_listener_accept(FlatWsServerListener *listener,
                                   long timeout_ms, intptr_t *out_peer,
                                   char *error, unsigned long error_capacity);
/* Read and validate one RFC 6455 HTTP/1.1 upgrade request and write the
 * 101 Switching Protocols response. Reads exactly through CRLFCRLF so any
 * following WebSocket frame bytes remain available to the protocol layer.
 * timeout_ms: 0 = non-blocking, -1 = wait indefinitely. Returns 0 on success. */
int flat_ws_server_peer_accept_upgrade(intptr_t peer, long timeout_ms,
                                       char *error, unsigned long error_capacity);
/* Send one unfragmented, unmasked server frame. Control payloads <= 125 bytes. */
int flat_ws_server_peer_send(intptr_t peer, FlatWsServerOpcode opcode,
                             const void *data, unsigned long length,
                             char *error, unsigned long error_capacity);
/* Receive/reassemble one client text or binary message. Client frames must be
 * masked. Ping is answered automatically; pong is ignored; close is echoed
 * and returns 2. Returns 0=message, 1=timeout, -1=error. */
int flat_ws_server_peer_receive(intptr_t peer, long timeout_ms,
                                unsigned long max_message_bytes,
                                FlatWsServerMessage *out_message,
                                char *error, unsigned long error_capacity);
void flat_ws_server_message_free(FlatWsServerMessage *message);
void flat_ws_server_peer_shutdown(intptr_t peer);
void flat_ws_server_peer_close(intptr_t peer);
void flat_ws_server_listener_shutdown(FlatWsServerListener *listener);
void flat_ws_server_listener_close(FlatWsServerListener *listener);

#ifdef __cplusplus
}
#endif
#endif
