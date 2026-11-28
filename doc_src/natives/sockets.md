# Native module `sockets`

The `sockets` module provides a **runtime-owned WebSocket client** with a shared FlatL API and platform-specific transports: native builds use linked libcurl, while FLEditor WASM uses the browser JavaScript `WebSocket` API. Both transports publish through the same `FlatRuntime` socket registry and event broker. Declare it in the source header:

```flat
use native sockets
use native task
```

The module is available to FlatL code running in a task owned by `FlatRuntime`. A standalone VM invocation (`bin/flvm`, `bin/flatl run`, or `bin/flatl crun`) does not install this task bridge; use a runtime host for actual socket operations. FLEditor WASM uses the browser WebSocket backend and keeps the runtime alive while the root task waits for event-driven work.

## API reference

| Function | Arguments | Return value | Purpose |
|---|---|---|---|
| `sockets.wsSupported()` | none | BOOL | Reports whether the active runtime socket bridge has WebSocket support (`ws` and `wss`). |
| `sockets.wsConnect(url, connectTimeoutMs?, maxFrameBytes?)` | URL STRING; optional timeout and frame limit NUMBERS | NUMBER — `socketId` | Opens a `ws://` or `wss://` connection. The handshake is synchronous. |
| `sockets.wsSend(socketId, message)` | socket ID NUMBER, message STRING | BOOL | Sends one text frame. |
| `sockets.wsSendBinary(socketId, bytes)` | socket ID NUMBER, bytes STRING | BOOL | Sends one binary frame, preserving the stored byte length, including embedded zero bytes. |
| `sockets.wsClose(socketId)` | socket ID NUMBER | BOOL | Closes the connection and stops its reader. The ID is invalid afterwards. |

### `sockets.wsSupported()`

Returns `true` only when the runtime bridge is present and the linked libcurl advertises both WebSocket protocols. The libcurl version number alone is not used as the capability test.

```flat
supported = sockets.wsSupported()
io.outln(supported)
```

A call made outside a runtime-owned task fails because there is no socket bridge to query. Native builds return `true` when the linked libcurl advertises both `ws` and `wss`. Browser WASM returns `true` when the browser provides `WebSocket`, `TextEncoder`, and `TextDecoder`. Browser connection policy (CSP, mixed-content rules, and server origin policy) can still prevent an individual connection.

### `sockets.wsConnect(url, connectTimeoutMs?, maxFrameBytes?)`

| Argument | Type | Default | Valid range / meaning |
|---|---|---:|---|
| `url` | STRING | — | A `ws://` or `wss://` endpoint. |
| `connectTimeoutMs` | NUMBER | `10000` | Integer from `1` to `86400000` milliseconds. |
| `maxFrameBytes` | NUMBER | `1048576` | Integer from `1` to `1073741824` bytes (1 GiB). Maximum accepted inbound frame size. |

Returns a positive numeric `socketId`. Socket IDs share the `socket` event-source namespace used by `task.spawnFor()`.

```flat
wsId = sockets.wsConnect('ws://127.0.0.1:8765', 5000, 65536)
```

On native builds, the connection handshake blocks the calling task until success or failure; a reader thread then receives frames. In browser WASM, the call creates the browser `WebSocket` and returns the ID immediately; the handshake continues asynchronously and `open` or `error` is published later. The browser does not expose a synchronous connect timeout, so the configured timeout is enforced by a timer while the socket remains in CONNECTING state. Both transports publish events to the same runtime broker. The `open` event is retained if it is published before the first matching subscription.

### `sockets.wsSend(socketId, message)`

Sends a WebSocket **text** frame. `message` is passed as a text payload; this function is not the binary-safe API.

```flat
sent = sockets.wsSend(wsId, 'hello from FlatL')
```

### `sockets.wsSendBinary(socketId, bytes)`

Sends a WebSocket **binary** frame. `bytes` is a FlatL STRING value, and the stored byte length is used. Embedded zero bytes are preserved; no base64 conversion or text encoding is performed.

```flat
-- payload is a binary STRING produced by the bytes module.
sent = sockets.wsSendBinary(wsId, payload)
```

For arbitrary binary construction and slicing, see [native `bytes`](bytes.md) and pass its binary string value to this function.

### `sockets.wsClose(socketId)`

Stops the reader and closes the connection. Close the socket when it is no longer needed; the runtime also owns cleanup when it is destroyed.

```flat
closed = sockets.wsClose(wsId)
```

## Events and `task.spawnFor()`

WebSocket events use the common source kind `'socket'`. The event name identifies the event type:

| Event | When it is published | Payload |
|---|---|---|
| `open` | Handshake has completed | Empty payload. |
| `receive` | A WebSocket frame arrives | OBJECT with `opcode` and `data`; `data` contains the frame payload. |
| `close` | Peer close or reader termination | Empty payload. |
| `error` | Transport or reader error occurs | STRING containing the error description. |

Subscribe with `task.spawnFor(sourceId, sourceKind, eventName, #handler, options?, ...)`. The first handler parameter receives the event payload; additional arguments are copied into each handler task. See [native `task`](task.md) for scheduling and queue options.

```flat
subId = task.spawnFor(wsId, 'socket', 'receive', #onMessage, {
  mode: 'serial',
  queueCapacity: 64,
  overflow: 'dropOldest'
})

#onMessage(event)
  io.outln(event.opcode)
  io.outln(event.data)
  OK
```

`mode: 'serial'` processes one handler at a time in event order. Use `mode: 'parallel'` and `maxActive` when independent messages may be processed concurrently. Queue overflow policies are `wait`, `dropOldest`, `dropNewest`, and `error`; see the `task.spawnFor()` options table in [native `task`](task.md).

### Events arriving before subscription

The runtime protects the usual connect-then-subscribe sequence:

- The one-shot `open` notification is retained until the first matching `socket/open` subscription. It is consumed once. If `close` occurs before the first subscription, the retained `open` notification is discarded.
- Incoming `receive` frames are buffered FIFO until the first matching `socket/receive` subscription.
- The pre-subscription receive buffer is bounded to 256 frames per socket and 4096 frames per runtime.
- If either transport buffer limit is reached, the reader publishes an `error` event and terminates the connection rather than silently dropping data or growing memory without limit.
- If the retained backlog exceeds the new subscription's queue capacity, the default `wait` policy rejects that subscription. Use `dropOldest` or `dropNewest` to accept a smaller queue and apply its stated policy.

Neither transport executes FlatL code from its I/O callback. The native reader thread or browser JavaScript callback publishes to the runtime event broker; the cooperative scheduler creates handler tasks. In FLEditor WASM, the runner uses Asyncify to yield to the browser and resume scheduling while the root task is waiting, allowing WebSocket callbacks to enqueue and run event handlers.

## Complete example: connect, subscribe, send, close

This example illustrates the normal event-driven structure. It must be run by a host that creates a `FlatRuntime` root task and drives its scheduler; it is not runnable as a standalone `flatl crun` program.

```flat
use native sockets
use native task
#main
  supported = sockets.wsSupported()
  io.outln(supported)
  wsId = sockets.wsConnect('ws://127.0.0.1:8765', 5000, 65536)

  openSub = task.spawnFor(wsId, 'socket', 'open', #onOpen)
  receiveSub = task.spawnFor(wsId, 'socket', 'receive', #onReceive, {
    mode: 'serial',
    queueCapacity: 64,
    overflow: 'dropOldest'
  })
  closeSub = task.spawnFor(wsId, 'socket', 'close', #onClose)
  errorSub = task.spawnFor(wsId, 'socket', 'error', #onError)

  -- Wait for open before sending: browser WebSocket connects asynchronously.
  OK

#onOpen(event)
  io.outln('WebSocket opened')
  sockets.wsSend(wsId, 'hello')
  -- For binary data, construct a byte string and use wsSendBinary.
  OK

#onReceive(event)
  io.outln(event.opcode)
  io.outln(event.data)
  OK

#onClose(event)
  io.outln('WebSocket closed')
  OK

#onError(event)
  io.outln('WebSocket error')
  OK
```

A production program should also define its application-level completion condition, stop subscriptions with `task.stopFor(subscriptionId)` when they are no longer needed, and close the socket. In browser WASM, send only after the `open` handler runs; `wsSend`/`wsSendBinary` fail while the browser socket is still CONNECTING. `task.stopFor()` refuses removal while a handler dispatch is in progress.

## Platform support and build notes

- Native support requires `http=1` and a libcurl build that advertises both `ws` and `wss`.
- `http*` in the build recipe means FlatL links the system libcurl; it does not compile or bundle curl itself. `pkg-config libcurl` supplies the headers and linker flags.
- FLEditor WASM enables `FLAT_WITH_WEBSOCKET=1` with `FLAT_WASM_BROWSER_WEBSOCKET=1` and compiles the browser adapter instead of the libcurl transport. It uses the browser `WebSocket` API; it does not link libcurl or require pthreads. `wsConnect()` is asynchronous at the transport level, even though it returns a socket ID synchronously. The one-shot runner keeps the runtime alive after root completion while event subscriptions, sockets, or handler tasks remain; applications must explicitly stop subscriptions and close sockets to finish the run.
- The current `FlatTaskNativeBridge` exposes sockets to runtime-owned task VMs. Standalone VM execution without a runtime task bridge cannot use these operations.
- `wsConnect` has platform-specific timing: synchronous handshake on native and asynchronous handshake in browser WASM. The send timeout is fixed at 10 seconds on native; browser `WebSocket.send()` queues data synchronously and has no configurable send timeout. Connection timeout and inbound maximum frame size are configurable.
- TCP and UDP are not implemented by this module yet.

## Testing

The repository's runtime-owned integration probe is:

```bash
bin/run_tests 'flvm[75]' -v -e
```

It compiles the FlatL probe, creates a `FlatRuntime`, starts a root task and a child task, and prints the value returned by `sockets.wsSupported()`. This is the appropriate support check; `bin/flatl crun` and `bin/flvm` run standalone VMs and do not install the socket task bridge.
