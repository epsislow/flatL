# Native WebSocket server — implementation notes

## Current foundation

The native build now contains a small TCP listening-endpoint primitive in
`flat_ws_server_transport.h` / `flat_ws_server_transport.c`:

- bind an IPv4/IPv6 address and listen on a requested port;
- request port `0` and retrieve the actual ephemeral port selected by the OS;
- accept a peer with a finite timeout or wait indefinitely;
- close accepted peer descriptors and release the listener.

The transport now also provides `flat_ws_server_peer_accept_upgrade()`. It reads
one bounded HTTP request header block, validates the WebSocket HTTP/1.1 upgrade
(`Upgrade`, `Connection`, version `13`, and a canonical 16-byte Base64 key),
computes `Sec-WebSocket-Accept` using the existing SHA-1 implementation, and
returns the RFC 6455 `101 Switching Protocols` response. It reads through
`CRLFCRLF` one byte at a time, so bytes belonging to the first WebSocket frame
are not consumed by the handshake layer. The accepted peer remains owned by the
caller whether the upgrade succeeds or fails.

The native protocol layer now also exposes frame operations through
`flat_ws_server_peer_send()` and `flat_ws_server_peer_receive()`:

- server text/binary and control frames are written without masking;
- client frames are required to be masked and RSV bits/opcodes are validated;
- 7-bit, 16-bit and 64-bit payload lengths are decoded, including canonical
  length checks and rejection of the reserved high bit;
- fragmented text/binary messages are reassembled, with control frames allowed
  between fragments;
- ping is answered with pong, pong is ignored, and close is validated and
  echoed before the receive call returns the close status;
- text payloads and close reasons are checked for valid UTF-8;
- receive allocation is bounded by the caller-provided maximum message size.

`flat_ws_server_peer_receive()` returns `0` for a complete message, `1` for a
read timeout, `2` after acknowledging a close frame, and `-1` for protocol or
I/O errors. The caller releases returned data with
`flat_ws_server_message_free()`. The accepted peer remains caller-owned.

This remains a transport/protocol foundation rather than a runtime-managed
server: event publication, client registry, and FlatL listener operations are
not yet implemented. Timeouts currently apply to each individual readiness
wait, not as one absolute deadline for the complete frame/message operation.

The listener is native-only. Browser WASM cannot bind a TCP listening socket;
Node.js-hosted WASM support is not part of this implementation increment.

## Validation

`flvm[78]` validates masked client-frame parsing, fragmented text with an
interleaved ping/pong, 16-bit payload lengths, and unmasked server-frame writing.
`flvm[76]` opens a loopback listener on an OS-assigned ephemeral port, verifies
a non-blocking accept timeout, connects a local TCP peer, accepts it, and closes
both sides. It validates the listening socket lifecycle. `flvm[77]` runs a loopback HTTP
Upgrade using the RFC 6455 sample key and checks the exact expected
`Sec-WebSocket-Accept` value.

Run it with:

```sh
bin/run_tests 'flvm[76-78]' -v -e
```

## Planned next layers

- Per-client lifecycle and server registry in `FlatRuntime`.
- Publish `connection` on the listener's shared `socket` event source; accepted
  peers receive their own numeric socket IDs and use `open`, `receive`,
  `close`, and `error` events.
- FlatL `sockets.wsListen()` / `sockets.wsStop()` and server/client E2E tests.


## Native server registry (archive 391)

`FlatWsServerRegistry` in `flat_ws_server_registry.h` owns native listener and
accepted-peer handles. The host opens listeners and accepts peers through the
registry, then performs HTTP Upgrade and RFC 6455 message I/O using numeric IDs.
IDs are monotonic within one registry lifetime.

Close first rejects new operations, shuts down the underlying socket to
interrupt blocking I/O, waits for active operations, and only then releases the
descriptor. Registry destruction applies the same sequence to all remaining
listeners and peers. Receive operations are serialized per peer; explicit sends
and automatic PONG/CLOSE replies share the peer's send lock. The host must stop
new calls before destroying the registry.

This registry is a transport ownership layer, not a FlatRuntime registry. It
does not yet publish `socket` event-source events, run a background accept/I/O
reactor, or provide `sockets.wsListen()` / `sockets.wsStop()`. The host currently
drives accept and receive. See `plans/70_WEBSOCKET_SERVER_REGISTRY_LIFECYCLE.md`.

Run the focused native server tests with `bin/run_tests 'flvm[76-79]' -v -e`.

## Portable I/O reactor and runtime event wakeup (archive 392)

`flat_io_reactor.h` introduces a reusable readiness abstraction for native
networking. The initial backend uses `poll()` on POSIX and `WSAPoll()` on
Windows. It supports add/modify/remove, READ/WRITE/ERROR/HANGUP notifications,
and a private wake channel (nonblocking pipe on POSIX, loopback UDP socket pair
on Windows). Registrations carry opaque 64-bit tokens rather than exposing
file descriptors as application identities. A caller must validate returned
tokens against its live registry because a removal racing with an in-progress
wait may produce one already-snapshotted stale event. The owner must join the
wait thread before destroying the reactor.

The reactor is deliberately readiness-only: it does not execute FlatL code or
publish runtime values. The future networking loop will translate readiness
into transport work, then publish through the generic runtime event broker.
The scheduler remains responsible for creating and running handler tasks.

The event-source audit also found that publishing a runtime event did not wake
worker-mode schedulers blocked on the ready queue. `flat_runtime_event_publish()`
now wakes workers after successful queue publication and after releasing the
runtime lock. Registering a persistent handler also wakes workers because the
subscription may receive replayed `open`/`receive` events. `flvm[81]` verifies
that a worker waiting idle is woken by an external producer and dispatches the
handler without an unrelated task wake.

The `poll`/`WSAPoll` backend is a portable baseline. Linux `epoll`, BSD/macOS
`kqueue`, and Windows IOCP remain possible optimized implementations behind
the same API; none is claimed by this increment.

Run the focused checks with:

```sh
bin/run_tests 'flvm[76-81]' -v -e
```

## FlatRuntime ownership boundary (archive 393)

When native WebSocket support is enabled, each `FlatRuntime` now owns one
`FlatWsServerRegistry` and one `FlatIoReactor`. Runtime-scoped host operations
are declared in `flat_runtime_ws_server.h`: listen, accept, upgrade, send,
receive, close peer and close listener. The runtime destroys the registry
before destroying the reactor, so any descriptors still owned by the registry
are closed before the reactor's wake channel and registration storage are
released. The host must continue to prevent new API calls from racing with
`flat_runtime_destroy()`, consistent with the general runtime lifetime contract.

This increment is intentionally an ownership/API-boundary step, not the
asynchronous server loop. These runtime wrappers still call the synchronous
registry operations; the reactor does not yet monitor the listener or peers.
There is no background I/O thread, no server-generated socket event publication,
and no FlatL `sockets.wsListen()` / `sockets.wsStop()` implementation yet.
New listener/accept and peer I/O operations are rejected once runtime shutdown
starts, while close operations remain available to release resources.

`flvm[82]` verifies runtime-owned listener creation, explicit close, repeated
ephemeral binds, and implicit cleanup of a listener left open at runtime
destruction. Run focused checks with:

```sh
bin/run_tests 'flvm[76-82]' -v -e
```
