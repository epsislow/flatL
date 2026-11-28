# Flat I/O Service

`FlatIoService` is the single-threaded dispatch layer above `FlatIoReactor`.
It owns a reactor and one optional owner thread. The owner thread blocks in
`flat_io_reactor_wait()` and invokes a caller-supplied callback for each
readiness event.

## Contract

- `flat_io_service_create(callback, context)` creates the service but does not
  start a thread.
- `flat_io_service_start()` starts exactly one owner thread. A stopped service
  cannot be restarted.
- `add`, `modify`, and `remove` update reactor registrations. The service does
  not own or close registered descriptors.
- `wake` interrupts the wait loop.
- `stop` marks the service stopping, wakes the reactor, and joins the owner
  thread. Repeated stop is safe.
- `destroy` stops the service, destroys the reactor, and releases the service.

Callbacks run on the I/O thread. They must not execute FlatL/VM code, block on
application work, or call `stop`/`destroy` on the same service. They should do
bounded native I/O and enqueue immutable work/events for the FlatRuntime
scheduler. The owner must serialize `destroy` against all other service API
calls. The service does not close registered descriptors.

The initial implementation uses the existing portable `poll`/`WSAPoll`
reactor. Optimized `epoll`, `kqueue`, and IOCP backends remain future work.

## Current integration boundary

This archive adds and tests the reusable owner-thread service. The existing
FlatRuntime still owns its `FlatIoReactor` directly; replacing that field with
`FlatIoService` and registering WebSocket listener descriptors is the next
integration step. No WebSocket listener is automatically accepted by this
service yet, and no server events are published from its callback.
