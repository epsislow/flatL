# Runtime I/O reactor and event wakeup foundation

## Scope

Archive 392 introduces the first reusable native I/O reactor abstraction and
closes a scheduler integration gap found during the event-source audit. This is
an infrastructure increment only: it does not yet attach WebSocket descriptors
to the reactor or create a FlatRuntime-owned WebSocket server.

## Architecture decision

- One reactor monitors many descriptors; there is no thread per peer.
- The reactor reports readiness only. It never calls FlatL code and never
  publishes VM values itself.
- Callers associate opaque, nonzero 64-bit tokens with descriptors. Tokens are
  not descriptors and are validated by the owning registry before use.
- The first backend is portable `poll()` on POSIX and `WSAPoll()` on Windows.
  This is the compatibility baseline. Linux `epoll`, BSD/macOS `kqueue`, and a
  Windows IOCP backend remain future optimized backends behind the same API.
- A private nonblocking pipe (POSIX) or loopback UDP socket pair (Windows)
  provides cross-thread wakeup so add/modify/remove and shutdown can interrupt
  a blocked wait.
- A remove racing with an already-started wait may yield one stale token in
  that wait's result. The consumer must validate the token against its live
  registry before dereferencing peer state. Tokens must not be reused while a
  stale result could exist.
- The reactor owner must stop and join its wait thread before destroying the
  reactor. Descriptor ownership remains with the caller/registry.

## API

`include/flat_io_reactor.h` exposes:

- `flat_io_reactor_create()` / `flat_io_reactor_destroy()`;
- `flat_io_reactor_add()` / `flat_io_reactor_modify()` / `flat_io_reactor_remove()`;
- `flat_io_reactor_wait()` for READ, WRITE, ERROR and HANGUP readiness;
- `flat_io_reactor_wake()` for cross-thread interruption.

The API uses `intptr_t` descriptors to preserve native Windows `SOCKET` width.
The implementation snapshots registrations before waiting and does not hold its
mutex during the OS wait.

## Runtime event broker audit

The existing generic event broker already deep-copies published values and
serializes its subscription queues under the runtime lock. The scheduler already
converts queued handler events into ordinary FlatRuntime tasks. However, the
successful `flat_runtime_event_publish()` path did not wake worker threads that
were asleep on the ready-queue condition variable. A network reactor publishing
an event could therefore leave it queued until an unrelated task or shutdown
wakeup occurred.

Archive 392 wakes the runtime ready queue after a successful event publication,
after releasing the runtime lock. Handler subscription also wakes workers after
registration because an `open` or `receive` event may have been replayed into the
new subscription. The event queue mutation is visible before the wake is sent.
The scheduler remains the only component that executes handler bytecode.

## Validation

- `flvm[80]`: descriptor readiness, modify/remove, timeout, and cross-thread wake.
- `flvm[81]`: an idle real FlatRuntime worker receives a host-published event,
  dispatches the handler, and completes without an unrelated task wake.
- Existing WebSocket server transport/handshake/frame/registry tests remain
  `flvm[76-79]`.

## Not implemented yet

- No listener or peer is registered with this reactor yet.
- No background accept/read/write loop exists.
- No FlatRuntime-owned WebSocket server registry exists.
- No event payload contract for `connection`, `open`, `receive`, `close`, or
  `error` has been finalized for the server side.
- No `sockets.wsListen()` / `sockets.wsStop()` API exists.
- `poll`/`WSAPoll` is the initial backend, not a claim of epoll/kqueue/IOCP
  scalability. Optimized backends should be added only after the common contract
  and lifecycle tests are stable.
