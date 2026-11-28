# Runtime I/O Service Thread

## Goal

Introduce one reusable owner-thread dispatch layer above `FlatIoReactor`,
without executing VM code on the networking thread.

## Archive 394

- [x] Add `FlatIoService` with a single optional owner thread.
- [x] Dispatch reactor readiness callbacks outside service locks.
- [x] Support registration add/modify/remove and explicit wake.
- [x] Make stop idempotent and join an in-flight callback before destroy.
- [x] Reject registrations after stop begins.
- [x] Add `flvm[83]` coverage for readiness dispatch, callback token, removal,
      stop/join and repeated stop.
- [ ] Replace FlatRuntime's direct reactor ownership with `FlatIoService`.
- [ ] Register listener descriptors and implement nonblocking accept.
- [ ] Add incremental HTTP Upgrade and WebSocket frame state machines.

## Invariants

- The I/O callback never executes FlatL or VM code.
- Registered descriptors remain owned by their caller.
- A callback may perform bounded native I/O and enqueue scheduler work.
- Service destruction must be serialized against all external API calls.
- The service cannot be stopped/destroyed from its own callback.

## Validation

`flvm[83]` checks that one owner thread receives a readiness event, the callback
gets the registered token, removal works, and stop wakes and joins the thread.
