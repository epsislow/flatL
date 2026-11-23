# HTTP transport smoke (standalone)

This directory contains a **standalone C transport test**, not the FlatL VM suite.
Run `python3 test/http/run_transport_tests.py` (requires libcurl development headers,
C compiler, Python >= 3.11). It starts a loopback HTTP server using cases from
`mock_cases.toml`, compiles the C probe, and verifies success/404/500 without internet.

## Layout

| Path | Role |
|------|------|
| `include/flat_http_transport.h` | request/response interface |
| `src/http/flat_http_curl.c` | native libcurl transport (+ `FLAT_HTTP_MOCK_FILE` fixtures) |
| `src/http/flat_http_fetch.c` | WASM/browser Fetch + Asyncify |
| `src/lib/mod_http.c` | FlatL `http.*` natives (resource IDs) |
| `doc_src/natives/http.md` | public API docs |
| `test/cases/http/` | VM cases via `bin/run_tests` (mock fixtures) |

## Native vs WASM

- **Native** (`flvm` / `flatl`): links **libcurl** (`pkg-config libcurl` or `-lcurl`). Calls `curl_global_init` once on first real (non-mock) request.
- **WASM / fleditor**: no curl; uses the browser Fetch API. Request bodies are passed as raw heap bytes (binary-safe). Browser CORS and mixed-content rules still apply.

## Mock fixtures

For VM tests, `run_tests` sets `FLAT_HTTP_MOCK_FILE` to a case TOML with `[http.mock]`
(`method`, `path`, `status`, `headers` / `header_*`, `body` / `body_file`, `error` / `error_code`).

## Not covered here

- Live TLS / public network integration
- Browser CORS / fleditor end-to-end HTTP smoke
- Asynchronous FlatL HTTP API (sync only today)
