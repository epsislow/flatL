# Runtime WebSocket registry

The native runtime owns WebSocket event clients and assigns each connection an ordinary positive numeric `socketId`. The ID is in the shared `sourceKind = "socket"` namespace used by `task.spawnFor()`; it is not an object or an OO resource.

The host C API is declared in `include/flat_runtime_socket.h`. Include `flat_ws_transport.h` as well when using the `FLAT_WS_TEXT` / `FLAT_WS_BINARY` opcode constants:

- `flat_runtime_ws_supported()` reports whether the linked libcurl build advertises both `ws` and `wss`.
- `flat_runtime_ws_connect(runtime, url, connectTimeoutMs, maxFrameBytes, &socketId, error, errorCapacity)` creates a connection, publishes `socket/open`, starts its background reader and returns the numeric ID.
- `flat_runtime_ws_send(runtime, socketId, opcode, data, length, timeoutMs, error, errorCapacity)` sends a text or binary message through the registered client.
- `flat_runtime_ws_close(runtime, socketId)` removes the ID from the runtime registry, stops its reader and releases the client.
- `flat_runtime_socket_count(runtime)` reports the number of registered native socket clients.

IDs start at 1 per runtime, increase monotonically and are not reused. A failed connect does not publish an ID. Unknown or already-closed IDs are rejected by send/close. `flat_runtime_destroy()` refuses to destroy a runtime while any socket client remains registered; callers must close sockets and unsubscribe their event handlers first.

The registry serializes lookup/send/close so a client cannot be freed while a send is in progress. The reader thread continues to publish events only through the generic runtime socket event adapter; it never enters a FlatL VM. This is a C host/runtime API increment: it does not yet expose the `sockets` native module to FlatL.

## Example host flow

```c
uint64_t socket_id = 0;
char error[256] = {0};
if (flat_runtime_ws_connect(runtime, "wss://example.invalid/ws", 5000,
                            1024 * 1024, &socket_id, error, sizeof error) != 0) {
    /* inspect error */
}
/* Subscribe using sourceId=socket_id, sourceKind="socket". */
/* Send text using opcode FLAT_WS_TEXT (1). */
flat_runtime_ws_send(runtime, socket_id, FLAT_WS_TEXT, "hello", 5,
                     5000, error, sizeof error);
/* Unsubscribe event handlers before closing/destroying the runtime. */
flat_runtime_ws_close(runtime, socket_id);
```
