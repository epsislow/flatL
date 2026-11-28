# Socket event adapter contract

This document describes the C-side event adapter boundary used by socket transports. It does **not** claim that a FlatL `sockets` transport module or a WebSocket/TCP/UDP implementation is available yet.

All socket transports share the event source namespace `socket`. A connection is represented in FlatL by an ordinary positive NUMBER `socketId`; it is not an object and has no methods or properties. The transport adapter owns the mapping between that number and its native connection state.

The host adapter publishes lifecycle/data events through `flat_runtime_socket_event_publish()` (`include/flat_runtime_socket_event.h`). The helper fixes `sourceKind` to `socket` and accepts only these event names:

| Event | Meaning |
|---|---|
| `open` | Connection has completed its transport handshake/open sequence |
| `receive` | One message or datagram has arrived |
| `close` | Connection has closed |
| `error` | A transport error has occurred |

The payload is a single FlatL value. The event handler receives it as its first parameter; arguments bound in `task.spawnFor()` follow it. Payloads are deep-copied by the runtime event broker, so the producer may release its temporary pools after publication returns.

Example subscription (the transport must provide `socketId` and publish the events):

```flat
subscriptionId = task.spawnFor(socketId, 'socket', 'receive', #onReceive)

#onReceive(event)
  -- process the received payload
  OK
```

A transport adapter must treat a `-2` publication result as backpressure and retain/retry the event; `-3` is an explicit overflow error. It must not execute FlatL handlers from its callback or transport thread. Scheduler dispatch remains runtime-owned.

## Current implementation boundary

The shared C event adapter and its lifecycle-event validation are implemented and covered by `flvm[71]`. A synchronous native libcurl WebSocket transport foundation is now available through `include/flat_ws_transport.h` and is covered by `flvm[72]`; see [Native WebSocket transport foundation](WEBSOCKET_TRANSPORT.md). The runtime-owned asynchronous event-loop bridge, FlatL numeric socket ID registry, native `sockets` module, and browser `WebSocket` bridge remain future work. In particular, `sockets.wsConnect()` is not available in this increment.
