#ifndef FLAT_RUNTIME_WS_SERVER_H
#define FLAT_RUNTIME_WS_SERVER_H

#include "flat_runtime.h"
#include "flat_ws_server_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime-owned WebSocket server operations. The runtime owns all descriptors
 * accepted through this API and releases them during explicit peer/listener
 * close or runtime destruction. The current API is synchronous; reactor-driven
 * nonblocking accept/read/write is introduced by the next integration layer. */
int flat_runtime_ws_server_listen(FlatRuntime *runtime, const char *host,
                                  uint16_t port, int backlog,
                                  uint64_t *out_listener_id,
                                  uint16_t *out_bound_port,
                                  char *error, unsigned long error_capacity);
int flat_runtime_ws_server_accept(FlatRuntime *runtime, uint64_t listener_id,
                                  long timeout_ms, uint64_t *out_peer_id,
                                  char *error, unsigned long error_capacity);
int flat_runtime_ws_server_upgrade(FlatRuntime *runtime, uint64_t peer_id,
                                   long timeout_ms, char *error,
                                   unsigned long error_capacity);
int flat_runtime_ws_server_send(FlatRuntime *runtime, uint64_t peer_id,
                                FlatWsServerOpcode opcode, const void *data,
                                unsigned long length, char *error,
                                unsigned long error_capacity);
int flat_runtime_ws_server_receive(FlatRuntime *runtime, uint64_t peer_id,
                                   long timeout_ms, unsigned long max_message_bytes,
                                   FlatWsServerMessage *out_message, char *error,
                                   unsigned long error_capacity);
int flat_runtime_ws_server_close_peer(FlatRuntime *runtime, uint64_t peer_id);
int flat_runtime_ws_server_close_listener(FlatRuntime *runtime, uint64_t listener_id);

#ifdef __cplusplus
}
#endif
#endif
