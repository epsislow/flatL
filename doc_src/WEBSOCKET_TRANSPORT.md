# Native WebSocket transport foundation

This document describes the native C transport layer introduced for WebSocket connections. It is a transport primitive, not yet a FlatL `sockets` module.

## Requirements and build

- Native builds with HTTP support enabled (`http=1`) compile `src/http/flat_ws_transport.c`.
- The implementation uses libcurl's WebSocket API (`curl_ws_*`) and therefore requires libcurl 7.86.0 or newer.
- `ws://` and `wss://` are accepted. TLS certificate verification remains under libcurl's secure defaults.
- The Emscripten Fetch transport does not compile this native implementation. Browser WebSocket support requires a separate JavaScript host bridge.

## C API

`include/flat_ws_transport.h` exposes:

- `flat_ws_prepare()` — idempotent process-wide libcurl initialization;
- `flat_ws_connect(url, timeout_ms, &connection, error, error_capacity)` — performs the WebSocket Upgrade handshake;
- `flat_ws_send(connection, opcode, data, length, timeout_ms, error, error_capacity)` — sends a text or binary frame;
- `flat_ws_receive(connection, timeout_ms, max_frame_bytes, &message, error, error_capacity)` — receives one frame, returning `1` on timeout, `0` on success and `-1` on failure;
- `flat_ws_message_free()` — releases the heap-owned received payload;
- `flat_ws_close()` — attempts a normal close frame and releases the connection.

Received payload memory belongs to the caller and must be released with `flat_ws_message_free()`. `max_frame_bytes` is enforced before the receive buffer grows beyond the configured bound. A received continuation frame is reported as `FLAT_WS_CONTINUATION`; callers that need complete fragmented messages must assemble the frames.

## Current boundary

This increment provides a synchronous C transport API. It does not yet create a background reader, publish directly into `FlatRuntime`, allocate a FlatL numeric `socketId`, or expose `sockets.wsConnect()`, `sockets.wsSend()` and `sockets.wsClose()` native functions. It is not yet integrated with `task.spawnFor()` event delivery. Those pieces require a runtime-owned asynchronous transport/event-loop bridge so transport callbacks never execute FlatL code or mutate VM pools directly.

The shared event namespace remains `sourceKind = 'socket'`, with `open`, `receive`, `close` and `error` event names as documented in [Socket event adapter contract](SOCKET_EVENT_ADAPTER.md).
