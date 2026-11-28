# Native WebSocket transport foundation

This document describes the native C transport layer introduced for WebSocket connections. It is a transport primitive, not yet a FlatL `sockets` module.

## Requirements and build

- Native builds with HTTP support enabled (`http=1`) compile `src/http/flat_ws_transport.c`.
- The implementation uses libcurl's WebSocket API (`curl_ws_*`) and therefore requires libcurl 7.86.0 or newer **and** a build that advertises both `ws` and `wss` in `curl_version_info_data.protocols`. Version alone is not sufficient; `flat_ws_supported()` checks the linked build and `flat_ws_connect()` fails early with a capability error when WebSocket protocols are absent.
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

The synchronous transport is now wrapped by a background C event client. It publishes into `FlatRuntime` without executing FlatL code on the reader thread. It still does not expose `sockets.wsConnect()`, `sockets.wsSend()` or `sockets.wsClose()` native functions; the host supplies and owns the numeric socket ID.

The shared event namespace remains `sourceKind = 'socket'`, with `open`, `receive`, `close` and `error` event names as documented in [Socket event adapter contract](SOCKET_EVENT_ADAPTER.md).

## Runtime event bridge

`include/flat_ws_event_client.h` adds a native background-reader adapter:

- `flat_ws_event_client_start(runtime, socket_id, url, ...)` connects, publishes `socket/open`, then starts a reader thread;
- each text/binary frame is published as `socket/receive` with an object payload `{ opcode: NUMBER, data: STRING }`;
- ping frames are answered with pong internally; pong frames are consumed internally;
- peer close, reader termination, and explicit stop publish `socket/close`; transport failures publish `socket/error` before close;
- `flat_ws_event_client_send()` sends text or binary messages without entering the VM;
- `flat_ws_event_client_stop()` joins the reader and releases the connection.

The reader publishes through `flat_runtime_socket_event_publish()` and never touches VM or task state. For `overflow = 'wait'`, it retains the current event and retries publication until capacity is available or the client is stopping. The runtime must outlive every client: stop all clients before destroying the runtime. The host owns the numeric `socket_id`; the current bridge is a C host API and does not yet expose FlatL `sockets.wsConnect()` natives.

Lifecycle payloads are intentionally small and stable: `open` and `close` carry `NULL`; `error` carries a STRING diagnostic; `receive` carries `{ opcode: NUMBER, data: STRING }`. The `data` string is byte-preserving for both text and binary frames; inspect `opcode` before interpreting it as text. Individual continuation frames are delivered separately as opcode `3`; this bridge does not reassemble fragmented WebSocket messages.


## Browser WebSocket backend (FLEditor WASM)

FLEditor WASM uses `src/fleditor_wasm/flat_ws_event_client_browser.c` rather than the native libcurl transport. The adapter creates a browser `WebSocket`, sets `binaryType = 'arraybuffer'`, and forwards `open`, `message`, `error`, and `close` callbacks into `flat_runtime_socket_event_publish()`. Binary messages are copied as raw bytes; text messages are encoded as UTF-8. Incoming payloads are checked against `maxFrameBytes` before publication.

The browser backend intentionally shares the native runtime socket registry and event broker. It does not create a reader thread. `wsConnect()` allocates the socket ID and returns immediately; the browser handshake completes later and emits `open` or `error`. The configured connect timeout is enforced with a browser timer while the WebSocket is CONNECTING. `wsSend` and `wsSendBinary` require the socket to be OPEN. Browser WebSocket does not expose ping/pong frames, arbitrary handshake headers, or a send timeout.

The FLEditor WASM runner uses Asyncify to yield while the root task remains non-terminal, then resumes the cooperative scheduler after browser callbacks publish events. This keeps the runtime and socket registry alive while a FlatL task is waiting for WebSocket events. Programs must keep the root task alive (for example by awaiting their event-driven work); once the root task terminates, the one-shot runner cancels remaining tasks and closes its runtime.
