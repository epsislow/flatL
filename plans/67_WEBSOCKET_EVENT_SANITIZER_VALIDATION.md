# WebSocket event pipeline ASan/UBSan validation — archive 378

## Scope

This is a sanitizer-validation checkpoint on top of archive 377. It does not change runtime behavior. The target is the ownership/lifecycle path introduced by the socket event adapter, native WebSocket transport, background event bridge, runtime-owned socket registry, pre-subscription event replay, and FlatL native bridge.

## AddressSanitizer

Built the harnesses from the clean source tree with Clang 17, `-O0 -g -fno-omit-frame-pointer -fsanitize=address`, and ran with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`.

- `test-runtime-socket-event-adapter`: 3/3 consecutive executions PASS.
- `test-runtime-websocket-registry`: 3/3 PASS.
- `test-websocket-transport`: 3/3 PASS.
- `test-websocket-runtime-event-bridge`: 3/3 PASS.
- FlatL `sockets` native bridge harness: 1/1 PASS, including spawned child task and runtime teardown.

No AddressSanitizer or leak-detection diagnostics were emitted.

## UndefinedBehaviorSanitizer

Built with Clang 17, `-O0 -g -fno-omit-frame-pointer -fsanitize=undefined`, and ran with `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.

- Socket event adapter: 3/3 PASS.
- Runtime WebSocket registry: 3/3 PASS.
- WebSocket transport contract: 3/3 PASS.
- WebSocket runtime event bridge: 3/3 PASS.
- FlatL `sockets` native bridge harness: 3/3 PASS.

No UBSan diagnostics were emitted.

## Functional regression and build

- `make -j2 all`: PASS.
- `make docs`: PASS.
- `bin/run_tests 'flvm[71-75]' -v -e`: 5/5 PASS.
- The FlatL native bridge reports `sockets.wsSupported() == false` on this host and completes the unsupported-capability path cleanly.

The user confirmed that the corrected FLEditor WASM build from archive 377 works on Termux. This closes the actual Emscripten link check for that archive.

## Coverage boundary

The linked libcurl on the sanitizer host does not advertise `ws`/`wss`. Consequently, `flvm[73]` and the registry/bridge harnesses exercised their unsupported-capability path, not a live WebSocket handshake, background-reader receive, or concurrent close race. The transport harness validates the API/argument/error contract, but does not replace a real loopback handshake. ASan/UBSan results must not be interpreted as certification of the live reader-thread path on a WebSocket-capable libcurl build.

The complete default suite and the full FLVM suite were not rerun in this checkpoint. ThreadSanitizer, live WebSocket round-trip on a capable libcurl build, Windows, and actual browser WebSocket support remain separate gates.

## Conclusion

The exercised event broker, pre-subscription buffering, registry cleanup, transport boundary, and FlatL native bridge paths are clean under ASan/UBSan. Continue next with deterministic race/lifecycle stress around publish/subscribe/close/runtime teardown, then repeat sanitizer validation on a host whose libcurl advertises `ws` and `wss`.
