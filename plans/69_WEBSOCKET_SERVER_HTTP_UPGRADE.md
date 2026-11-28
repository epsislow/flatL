# WebSocket server HTTP Upgrade — archive 389

## Scope

Archive 389 implemented the HTTP/1.1 Upgrade boundary. Archive 390 extends the
caller-owned peer with bounded RFC 6455 frame send/receive operations. Runtime
event publication and FlatL listener APIs remain out of scope.

## Contract

`flat_ws_server_peer_accept_upgrade(peer, timeout_ms, error, error_capacity)`:

- reads one bounded header block ending in `CRLFCRLF`;
- requires `GET ... HTTP/1.1`;
- validates `Upgrade: websocket`, a `Connection` token containing `Upgrade`,
  `Sec-WebSocket-Version: 13`, and a canonical Base64 key decoding to 16 bytes;
- computes `Base64(SHA1(key + RFC6455_GUID))`;
- writes `101 Switching Protocols` with `Upgrade`, `Connection`, and
  `Sec-WebSocket-Accept` headers;
- does not close the peer on failure or success; caller retains ownership.

The reader consumes exactly one byte at a time until the header terminator so
that a frame coalesced with the request in the TCP stream is not swallowed by
the HTTP layer.

## Tests

`flvm[77]` uses the RFC 6455 sample key and expects
`Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=`. `flvm[76]` remains the
TCP listener lifecycle test.

## Archive 390 — frame layer

Added `flat_ws_server_peer_send()` and `flat_ws_server_peer_receive()`.
Incoming client frames must be masked; RSV bits, opcodes, control-frame FIN and
length restrictions, canonical extended lengths and the 64-bit length high bit
are validated. Text/binary fragmentation is reassembled, ping receives an
automatic pong, pong is ignored, and close frames are validated and echoed.
Text messages and close reasons receive UTF-8 validation. Message allocation is
bounded by the caller's `max_message_bytes`. Server frames are unmasked and
support short, 16-bit and 64-bit lengths. `flvm[78]` covers fragmented text,
interleaved ping/pong, a 16-bit binary payload and server writing.

## Remaining work

- Add an absolute handshake/frame deadline (current timeout is applied to each wait).
- Send suitable HTTP error responses for rejected upgrade requests.
- Integrate accepted peers with runtime ownership, event broker and FlatL API.
