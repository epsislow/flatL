# Worker HTTP and DOM Lifecycle Audit — archive 309

## Scope

Archive 309 adds complete-VM worker-mode regressions for HTTP transport and DOM lifecycle. It does not change runtime scheduling, native capability declarations, or synchronization policy.

## HTTP: real concurrent libcurl requests

`flvm[45]` compiles a FlatL program that reads `FLAT_HTTP_TEST_URL`, creates an HTTP GET request, sends it, asserts status 200 and body `worker-http-ok`, and closes the request.

The C harness starts a loopback-only HTTP/1.1 server on an ephemeral port. Thirty-two FlatRuntime tasks execute the same bytecode through four workers. The server handles accepted connections on separate pthreads with a short response delay and records the maximum number of simultaneously active connections. The test requires at least two in-flight requests and, in the validated run, observed four.

The test explicitly clears `FLAT_HTTP_MOCK_FILE`, so it cannot silently pass through the fixture/mock transport. Worker startup performs the existing HTTP manifest preflight, which calls `flat_http_prepare()` before worker threads are created. Each request uses the existing per-request CURL easy handle and request-local transfer state; the test validates concurrent transport use and DRAIN/join.

This is a loopback integration test, not an external-network test. It does not certify every libcurl build/resolver backend or plugin lifecycle. `CURLOPT_NOSIGNAL=1` remains required. No global curl cleanup is introduced while workers or requests can exist.

## DOM: independent VM contexts and cancellation teardown

`flvm[46]` compiles a program that parses a document, selects a node, checks its text, closes the document and completes. Forty-eight independent FlatRuntime tasks execute under four workers. Each task has its own VM and DOM runtime; the program asserts task-local document contents.

The second phase holds the process-wide `dom` invocation gate from the host thread, starts one worker task and waits until it is RUNNING at the DOM native call. It publishes request-only cancellation, releases the gate, requests runtime CANCEL, and requires the task to become CANCELLED, the worker to join, and the task/runtime to be destroyed successfully. This exercises cancellation after a native call blocked at the DOM gate and the subsequent VM teardown path.

The test asserts the effective worker policy for `dom` is `SERIALIZED`. The declared capability remains `PENDING_THREAD_SAFETY_VALIDATION`; the fallback gate is deliberately retained. These tests do not promote Lexbor to `THREAD_SAFE`.

## Build/test integration

- Added `test-worker-http` and `test-worker-dom` Makefile targets.
- Added `test/cases/flvm/045_worker_http_transport/` (`flvm[45]`).
- Added `test/cases/flvm/046_worker_dom_lifecycle/` (`flvm[46]`).
- Both tests use the existing `bin/run_tests` framework.

## Validation

- `make all -j2` — PASS.
- `make docs` — PASS (50 sections).
- `flvm[45]` — PASS; 10 consecutive direct executions PASS.
- `flvm[46]` — PASS; 10 consecutive direct executions PASS.
- HTTP server observed `max-inflight=4` with four runtime workers.
- Final GCC ThreadSanitizer builds: HTTP 10/10 consecutive runs PASS; DOM 10/10 consecutive runs PASS.
- HTTP ASan/UBSan run — PASS. DOM ASan/UBSan build exceeded the execution limit before producing a test binary.
- Full default suite, Windows and actual Emscripten linking are not claimed for this archive.

## Conclusions / next audit

HTTP transport can be exercised concurrently by the runtime's workers with separate easy handles after startup preflight on the tested POSIX host. DOM task contexts remain isolated at the FlatRuntime/VM level, while Lexbor access and teardown continue to use the process-wide serialized gate. The next work should focus on native plugin loading races and deterministic worker-start partial-failure injection; do not change DOM capability to `THREAD_SAFE` based on this archive.
