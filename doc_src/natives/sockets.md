# sockets

The `sockets` native module currently exposes the first FlatL-facing WebSocket operations. It is available when the HTTP/WebSocket native support is compiled in and when the program runs inside a `FlatRuntime` task (the runtime supplies the socket bridge to task VMs).

## `sockets.wsSupported()`

Returns a boolean indicating whether the linked libcurl build advertises WebSocket (`ws`/`wss`) protocol support. This checks the actual runtime library capability; the libcurl version number alone is not sufficient.

## `sockets.wsConnect(url, connectTimeoutMs?, maxFrameBytes?)`

Connects to a `ws://` or `wss://` endpoint and returns a positive numeric `socketId`. The ID is an ordinary NUMBER in the shared `socket` source namespace. Both configuration arguments are optional: `connectTimeoutMs` defaults to 10000 (10 seconds), and `maxFrameBytes` defaults to 1048576 (1 MiB). The timeout must be an integer from 1 through 86400000; the frame limit must be an integer from 1 through 1073741824 (1 GiB).

The connection handshake is synchronous in this increment. Incoming frames are read by the existing event-client thread and published to the runtime event broker as `open`, `receive`, `close`, and `error` events.

## `sockets.wsSend(socketId, message)`

Sends a text WebSocket message and returns `true` on success. The message is sent as a UTF-8/text payload; embedded zero bytes are not preserved by this text API.

## `sockets.wsSendBinary(socketId, bytes)`

Sends a binary WebSocket frame and returns `true` on success. `bytes` must be a FlatL string value; its stored byte length is used, so embedded zero bytes in a string reference are preserved. The API does not perform text encoding or base64 conversion.

## `sockets.wsClose(socketId)`

Closes the connection, stops its reader and returns `true` on success. The socket ID becomes invalid after close.

## Event subscription

Use the generic task event API with the common socket kind:

```flat
subId = task.spawnFor(wsId, 'socket', 'receive', #onMessage, {
  mode: 'parallel',
  maxActive: 4,
  queueCapacity: 100,
  overflow: 'dropOldest'
})

#onMessage(event)
  io.outln(event.data)
  OK
```

The event payload is an object containing `opcode` and `data`. The transport thread never executes FlatL code; handler tasks are created by the runtime scheduler.

## Current limitations

- `wsConnect` performs its handshake synchronously.
- `wsSend` sends text frames; `wsSendBinary` sends binary frames.
- The send timeout remains fixed at 10 seconds; only the connection handshake timeout and inbound maximum frame size are configurable.
- TCP and UDP are not implemented by this module yet.
- The current `FlatTaskNativeBridge` exposes sockets to runtime-owned task VMs; standalone VM execution without a runtime task bridge cannot use these operations.
