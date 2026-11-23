# Native HTTP

The core `http` module performs synchronous HTTP and HTTPS requests. Native builds use libcurl; the browser/WASM build uses the browser Fetch API through the Emscripten Asyncify bridge. Browser requests remain subject to browser security rules, including CORS.

Requests are VM-owned resources identified by numeric IDs. Close each request with `http.close(id)` when it is no longer needed. `mem.infoResId(id, 'http')` returns `request` while the resource is live and `unknown` after it is closed.

## Create and send a request

```flat
req = http.create("GET", "https://api.example.com/users")
http.timeout(req, 10000)
http.send(req)

status = http.status(req)
body = http.body(req)
http.close(req)
```

Supported methods include `GET`, `HEAD`, `POST`, `PUT`, `PATCH`, and `DELETE`. Only `http://` and `https://` URLs are accepted by the native transport. Redirect following is enabled with a bounded redirect count. HTTPS peer and host certificate verification are enabled by default.

`http.send()` is synchronous from the FlatL program's point of view. The internal transport boundary is kept separate so a future asynchronous executor can reuse the same request and response representation; asynchronous calls are not part of this API yet.

## Request headers

Header names are normalized to lowercase internally. Header values are preserved. Repeated header values are supported.

`http.header(id, name, value)` appends one value:

```flat
http.header(req, "Accept", "application/json")
http.header(req, "X-Trace", "first")
http.header(req, "X-Trace", "second")
```

`http.headers(id, object)` replaces all request headers. The object maps each header name to a LIST of STRING values:

```flat
headers = {
    "Accept": ["application/json"],
    "Cache-Control": ["no-cache", "no-store"],
    "X-Trace": ["first", "second"]
}
http.headers(req, headers)
```

For convenience, a STRING value is also accepted for a header entry and is treated as a one-element list.

## Request body

`http.body(id, string)` sets a raw STRING request body:

```flat
req = http.create("POST", url)
http.header(req, "Content-Type", "text/plain")
http.body(req, "hello")
http.send(req)
```

`http.json(id, value)` serializes a FlatL value using the existing JSON serializer and adds `content-type: application/json` unless that header was already set:

```flat
payload = {"name": "FlatL", "active": true}
http.json(req, payload)
```

## Response

After `http.send()`:

- `http.status(id)` returns the HTTP status code. It returns `0` when no HTTP response was received.
- `http.body(id)` returns the response body as STRING. Larger bodies are returned as STRING REF, independent of the request resource lifetime.
- `http.responseHeaders(id)` returns an OBJECT mapping lowercase header names to LIST values. Repeated response headers are preserved when exposed by the transport.
- `http.errorCode(id)` returns the transport error code (`0` means no transport error).
- `http.error(id)` returns the transport error message, or an empty STRING when no transport error occurred.

HTTP statuses such as 404 and 500 are valid HTTP responses, not transport errors. A timeout, DNS failure, connection failure, TLS failure, or body-size overflow is reported through `errorCode()` and `error()` separately from the HTTP status.

Example:

```flat
http.send(req)
status = http.status(req)
response_headers = http.responseHeaders(req)
response_body = http.body(req)
transport_error = http.error(req)
transport_code = http.errorCode(req)
```

## Assert helpers: `transportOk` / `statusOk`

`http.send` never panics on transport failure or on HTTP 4xx/5xx. Use these helpers when you want a hard stop. Panic location is the helper call site (not `send`).

### `http.transportOk(id)`

After a successful `send`, panics if a **transport** error occurred (`errorCode != 0`). Maps codes to a small set of stable labels (libcurl-like numbers on native / WASM aliases):

| Panic label | Typical codes |
|-------------|---------------|
| `http.transport.timeout` | 28 |
| `http.transport.overflow` | 23 (body/headers limit) |
| `http.transport.memory` | 27 |
| `http.transport.connect` | 6, 7 (resolve / connect) |
| `http.transport.tls` | common TLS/SSL curl codes |
| `http.transport.other` | everything else ≠ 0 |

Calling before `send` panics with `http.transportOk.not_sent`.

### `http.statusOk(id)`

After a successful `send`, requires an HTTP **2xx** final status. Non-2xx panics with:

| Panic label | Status |
|-------------|--------|
| `http.status.not_found` | 404 |
| `http.status.unauthorized` | 401 |
| `http.status.forbidden` | 403 |
| `http.status.conflict` | 409 |
| `http.status.server_error` | any 5xx |
| `http.status.redirect` | any 3xx **final** status |
| `http.status.client_error` | other 4xx |
| `http.status.other` | anything else (including `0` when no HTTP response) |

Calling before `send` panics with `http.statusOk.not_sent`.

**Redirects:** native curl and WASM Fetch **follow** redirects (bounded). After follow, `http.status` is usually the **final** 2xx, so `statusOk` passes. `http.status.redirect` applies only when the status that remains after the transport step is still 3xx (follow exhausted / blocked), not for intermediate hops.

Typical happy path:

```flat
http.send(req)
http.transportOk(req)
http.statusOk(req)
body = http.body(req)
```

When 404 is expected, call `transportOk` only and branch on `http.status(req)` — do not call `statusOk`.

## Limits and lifecycle

- Default timeout: 10 seconds.
- Maximum configurable timeout: 10 minutes.
- Default response body limit: 8 MiB.
- `http.maxBody(id, bytes)` changes the response body limit up to the FlatL string-pool chunk limit.
- At most 16 request resources can be live in one VM run. They are automatically released when the VM run ends.
- `http.close(id)` releases a request and its transport buffers. A STRING/STRING REF already returned by `http.body(id)` remains valid according to normal FlatL value ownership.

## Deterministic HTTP mocks in tests

The standard test runner supports HTTP fixtures declared in each test's `case.toml`. Add an `[http.mock]` section. The runner exposes the current case TOML to the program as `FLAT_HTTP_MOCK_FILE`; the native transport then returns the fixture response without opening a network connection.

```toml
[http.mock]
method = "GET"
path = "/users"
status = 200
body = "{\"ok\":true}"
headers = ["Content-Type: application/json", "X-Trace: one", "X-Trace: two"]
```

For a larger response body, use `body_file` with a file next to the case TOML; this is useful for testing STRING REF responses without embedding large literals:

```toml
[http.mock]
method = "GET"
path = "/large"
status = 200
body_file = "response.txt"
```

A transport error can be simulated without an HTTP response:

```toml
[http.mock]
method = "GET"
path = "/slow"
error_code = 28
error = "mock timeout"
```

The fixture can also set `status = 404`, `status = 500`, or a final `status = 302` to exercise `statusOk` panic labels. Transport failures use `error_code` / `error` (for example `28` → `http.transport.timeout`). Run these tests through the regular suite:

```sh
make all
bin/run_tests -t http -v -e --categ=all
```

The mock is test-runner scoped and does not change normal HTTP behavior when `FLAT_HTTP_MOCK_FILE` is not set.
