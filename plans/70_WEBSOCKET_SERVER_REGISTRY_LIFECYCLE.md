# Native WebSocket server registry and lifecycle

## Scope

Archive 391 adds a transport-level ownership registry. It is deliberately not
FlatRuntime's event-source registry and does not publish Task events. The goal
is to make listener/peer ownership and concurrent teardown deterministic before
connecting the transport to the runtime scheduler.

## Contract

- `FlatWsServerRegistry` owns every listener and accepted peer inserted into it.
- IDs are monotonically allocated, nonzero `uint64_t` values and are not reused
  during one registry lifetime.
- Listener and peer operations acquire an active-operation reference while the
  registry lock protects lookup and admission.
- Closing an entry marks it unavailable before interrupting its socket. It then
  waits for active operations to leave before closing the descriptor.
- Registry destruction rejects new registrations, shuts down all remaining
  descriptors, waits for in-flight calls, and releases every record.
- Peer receive calls are serialized per peer. Explicit sends and automatic
  PONG/CLOSE replies share a per-peer send lock.
- The host must prevent new API calls from starting concurrently with registry
  destruction. A registry must not be destroyed from inside one of its own
  active operations.
- The low-level transport and registry are native-only. They do not add a
  browser/WASM TCP listener implementation.

## API

See `include/flat_ws_server_registry.h`:

- create/destroy registry;
- open listener and obtain its numeric ID and bound port;
- accept a TCP peer and obtain its numeric ID;
- perform HTTP Upgrade;
- send and receive RFC 6455 messages;
- close an individual peer or listener.

The registry does not currently run a background accept loop or I/O reactor.
The host drives `accept()` and receive operations. It also does not integrate
with `FlatRuntime`, the generic `socket` event-source adapter, or expose
`sockets.wsListen()` / `sockets.wsStop()`.

## Validation

`flvm[79]` starts a blocking accept operation on a worker thread, closes the
listener through the registry, and verifies that the blocked operation exits
before the listener is released. It also verifies duplicate close rejection
and full registry destruction. Run:

```sh
bin/run_tests 'flvm[76-79]' -v -e
```

## Next

Audit the runtime's generic event-source adapter and scheduler wakeup contract.
Then design the bridge that turns accepted peer IDs into `socket` source events
without running FlatL VM code on an I/O thread. Do not add a sockets-only
scheduler path or create one thread per peer.
