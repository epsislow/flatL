#ifndef FLAT_RUNTIME_SOCKET_H
#define FLAT_RUNTIME_SOCKET_H

#include "flat_runtime.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime-owned native WebSocket connections. IDs are ordinary positive
 * numeric values in the shared "socket" namespace. Connections must be closed
 * before runtime destruction. */
int flat_runtime_ws_supported(void);
int flat_runtime_ws_connect(FlatRuntime *runtime, const char *url,
                            long connect_timeout_ms, size_t max_frame_bytes,
                            uint64_t *out_socket_id, char *error,
                            size_t error_capacity);
int flat_runtime_ws_send(FlatRuntime *runtime, uint64_t socket_id,
                         int opcode, const void *data, size_t length,
                         long timeout_ms, char *error, size_t error_capacity);
int flat_runtime_ws_close(FlatRuntime *runtime, uint64_t socket_id);
size_t flat_runtime_socket_count(FlatRuntime *runtime);

#ifdef __cplusplus
}
#endif
#endif
