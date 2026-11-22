# HTTP implementation — stage 1 (native transport)

This directory contains a **standalone C transport test**, not a FlatL VM test.
Run `python3 test/http/run_transport_tests.py` (requires libcurl development headers,
C compiler, Python >= 3.11). It starts a loopback HTTP server using cases from
`mock_cases.toml`, compiles the C probe, and verifies success/404/500 without internet.

## Implemented
- `include/flat_http_transport.h`: request/response interface, ownership and cleanup.
- `src/http/flat_http_curl.c`: libcurl synchronous HTTP(S) transport, certificate
  verification, timeout, redirects, repeated response headers, body limit.
- Test-only TOML fixtures: `[[http.mock]]` with method/path/status/headers/body.

## Not yet implemented
- FlatL `http.*` native functions, resource IDs, `mem.infoResId`, OBJECT/LIST
  conversion, STRING REF integration, documentation of public API.
- Wiring these TOML fixtures into `bin/run_tests` (current harness is standalone).
- Browser/WASM Fetch backend; no browser build or browser tests were run.
- Mocked network failures/timeouts and TLS integration tests.

No Makefile or VM code has been modified in this stage. This is an incremental
transport foundation, **not** a complete HTTP module.
