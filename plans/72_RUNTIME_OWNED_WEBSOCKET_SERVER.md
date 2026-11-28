# Runtime-owned WebSocket server resources

## Goal

Establish FlatRuntime as the lifecycle owner of the native WebSocket server registry and the generic I/O reactor. Expose a narrow C host API that routes listener/peer operations through the owning runtime rather than requiring callers to manage independent registries.

## Ownership model

- Each native FlatRuntime owns exactly one `FlatWsServerRegistry` and one `FlatIoReactor` when native WebSocket support is enabled.
- The registry owns listener and accepted-peer descriptors inserted through its API.
- The reactor owns only its internal wake channel and registration table; it does not own registered descriptors.
- Runtime teardown destroys the WebSocket registry before the reactor, so transport descriptors are closed before reactor resources are released.
- The host must still prevent new runtime API calls from starting concurrently with `flat_runtime_destroy()`, as required by the existing runtime contract.
- Runtime listener/accept/upgrade/send/receive operations are currently synchronous wrappers. This archive establishes ownership and API boundaries; it does not yet register listener/peer descriptors with the reactor or create a background I/O thread.

## Public API

`include/flat_runtime_ws_server.h` exposes runtime-scoped operations:

- `flat_runtime_ws_server_listen()`
- `flat_runtime_ws_server_accept()`
- `flat_runtime_ws_server_upgrade()`
- `flat_runtime_ws_server_send()`
- `flat_runtime_ws_server_receive()`
- `flat_runtime_ws_server_close_peer()`
- `flat_runtime_ws_server_close_listener()`

New listener/accept and peer I/O operations are rejected once runtime shutdown has started. Close operations remain available during shutdown so hosts can release resources.

## Validation

`flvm[82]` verifies runtime-owned listener creation, explicit close, repeated ephemeral binds, and implicit cleanup of a listener intentionally left open when the runtime is destroyed.

## Deliberate non-goals

- No per-client thread.
- No reactor-driven accept/read/write loop yet.
- No event broker publication from native server peers yet.
- No FlatL `sockets.wsListen()` API yet.
- No claim of asynchronous or nonblocking behavior: registry operations remain synchronous in this milestone.

## Next step

Add reactor registration and a single runtime-owned I/O service thread. Begin with listener readiness and incremental nonblocking accept. Then add per-peer handshake/frame state machines, queued writes/backpressure, and event publication into the existing socket event broker. The I/O service must enqueue events only; it must never execute FlatL bytecode.
