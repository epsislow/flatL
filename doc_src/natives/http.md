# Native module `http`

**Synchronous HTTP and HTTPS requests.** **Optional** — declare before use:

```text
use native http
```

Native builds use libcurl; the browser/WASM build uses the browser Fetch API through the Emscripten Asyncify bridge.

**CORS (fleditor / WASM only):** `http.send` uses the page’s `fetch()`. The browser compares the fleditor origin (e.g. `http://127.0.0.1:8765`) with the request URL. Cross-origin calls need the target server to allow that origin (`Access-Control-Allow-Origin`). The same URL usually works with `bin/flvm` (libcurl has no CORS). Demo examples use CORS-friendly public APIs. There is no `http.allowCORS` on the native API — optional fleditor **Settings → HTTP → Cross-origin requests** only gates whether Fetch may *attempt* cross-origin URLs (it never bypasses browser CORS).

Requests are VM-owned resources identified by numeric IDs. Close each request with `http.close(id)` when it is no longer needed. `mem.infoResId(id, 'http')` returns `request` while the resource is live and `unknown` after it is closed.

## API reference

**Optional** module — requires `use native http` (or `use native http with Alias`) in each `.fl` file that calls `http.*`. Implementation: `src/lib/mod_http.c`, transport `src/http/flat_http_curl.c` (native) / Fetch+Asyncify (WASM).

Build without HTTP (no libcurl / no Fetch glue): `make rebuild http=0`. Then `flc|flvm|flatl --mods` and WASM `flat_wasm_mods()` omit `http`; `run_tests` skips cases with `mods = "http"`. See [TOOLCHAIN.md](../TOOLCHAIN.md#optional-modules-http0).

### Summary

| Call | Returns | Panic / fail labels |
|------|---------|---------------------|
| `create(method, url)` | NUMBER `id` | `http.create.args`, `http.create.limit` |
| `header(id, name, value)` | — | `http.header.args`, `http.header.value` |
| `headers(id, object)` | — | `http.headers.args`, `http.headers.invalid` |
| `requestBody(id)` | STRING | `http.requestBody.id`, `http.requestBody.memory` |
| `requestBody(id, body)` | — | `http.requestBody.args` |
| `json(id, value)` | — | `http.json.args`, `http.json.memory`, `http.json.serialize`, `http.json.content_type` |
| `timeout(id, milliseconds)` | — | `http.timeout.args`, `http.timeout.range` |
| `maxBody(id, bytes)` | — | `http.maxBody.args`, `http.maxBody.range` |
| `maxRedirects(id)` | NUMBER | `http.maxRedirects.id` |
| `maxRedirects(id, n)` | — | `http.maxRedirects.args`, `http.maxRedirects.range` |
| `bearer(id, token)` | — | `http.bearer.args`, `http.bearer.value`, `http.bearer.memory` |
| `basic(id, user, pass)` | — | `http.basic.args`, `http.basic.value`, `http.basic.memory` |
| `form(id, object)` | — | `http.form.args`, `http.form.invalid` |
| `addPart(id, name, value)` | — | `http.addPart.args`, `http.addPart.value`, `http.addPart.limit`, `http.addPart.memory` |
| `addFilePart(id, name, dataOrFile, filename?)` | — | `http.addFilePart.args`, `http.addFilePart.data`, `http.addFilePart.file`, `http.addFilePart.filename`, `http.addFilePart.limit`, `http.addFilePart.memory` |
| `removePart(id, name)` | — | `http.removePart.args` |
| `resetParts(id)` | — | `http.resetParts.id` |
| `partsLoad(id)` | — | `http.partsLoad.id`, `http.partsLoad.memory` |
| `partsMax(id)` | NUMBER | `http.partsMax.id` |
| `partsMax(id, n)` | — | `http.partsMax.args`, `http.partsMax.range`, `http.partsMax.below_count` |
| `partInfo(id)` | LIST | `http.partInfo.id`, `http.partInfo.args`, `http.partInfo.memory` |
| `send(id)` | — | `http.send.id`, `http.redirect.limit`, `http.partsLoad.memory` |
| `status(id)` | NUMBER | `http.status.id`, `http.status.not_sent` |
| `responseStatuses(id)` | LIST | `http.responseStatuses.id`, `http.responseStatuses.not_sent`, `http.responseStatuses.memory` |
| `responseBody(id)` | STRING | `http.responseBody.id`, `http.responseBody.not_sent`, `http.responseBody.memory` |
| `responseHeaders(id)` | OBJECT | `http.responseHeaders.id`, `http.responseHeaders.not_sent`, `http.responseHeaders.memory` |
| `error(id)` | STRING | `http.error.id`, `http.error.not_sent`, `http.error.memory` |
| `errorCode(id)` | NUMBER | `http.errorCode.id`, `http.errorCode.not_sent` |
| `transportOk(id)` | — | `http.transportOk.id`, `http.transportOk.not_sent`, `http.transport.*` |
| `statusOk(id)` | — | `http.statusOk.id`, `http.statusOk.not_sent`, `http.status.*` |
| `requestInfo(id)` | OBJECT | `http.requestInfo.id`, `http.requestInfo.args`, `http.requestInfo.memory` |
| `responseInfo(id)` | OBJECT | `http.responseInfo.id`, `http.responseInfo.args`, `http.responseInfo.not_sent`, `http.responseInfo.memory` |
| `close(id)` | — | `http.close.id` |

`*` labels for `transportOk` / `statusOk` are listed under [Assert helpers](#assert-helpers-transportok--statusok).

---

### `http.create(method, url)`

Allocates a request resource and returns its numeric **id**. Supported methods include `GET`, `HEAD`, `POST`, `PUT`, `PATCH`, and `DELETE`. Only `http://` and `https://` URLs are accepted by the transport. Defaults: timeout **10s**, max response body **8 MiB**, **maxRedirects 10**, **partsMax 32**. At most **16** live requests per VM run (`http.create.limit`).

```flat
get = http.create("GET", "https://api.example.com/users/1")
head = http.create("HEAD", "https://api.example.com/users/1")
post = http.create("POST", "https://api.example.com/users")
put = http.create("PUT", "https://api.example.com/users/1")
patch = http.create("PATCH", "https://api.example.com/users/1")
del = http.create("DELETE", "https://api.example.com/users/1")
```

### `http.header(id, name, value)`

Appends one request header. Names are normalized to lowercase; values are preserved. Repeated names are allowed (multi-value headers).

```flat
http.header(req, "Accept", "application/json")
http.header(req, "X-Trace", "first")
http.header(req, "X-Trace", "second")
```

### `http.headers(id, object)`

Replaces **all** request headers. The object maps each name to a LIST of STRING values. A STRING value for an entry is treated as a one-element list.

```flat
headers = {
  "Accept": ["application/json"],
  "Cache-Control": ["no-cache", "no-store"]
}
http.headers(req, headers)
```

### `http.requestBody(id)` / `http.requestBody(id, body)`

With one argument: returns the current **request** body as STRING (empty when none). With two arguments: sets a raw STRING request body and clears the `sent` flag so the request can be sent again. Prefer `http.json` when the payload should be JSON. Typical for `POST` / `PUT` / `PATCH`.

```flat
req = http.create("POST", "https://api.example.com/notes")
http.header(req, "Content-Type", "text/plain")
http.requestBody(req, "hello")
raw = http.requestBody(req)
```

### `http.json(id, value)`

Serializes `value` with the FlatL JSON serializer into the request body and adds `content-type: application/json` unless that header was already set. Typical for `POST` / `PUT` / `PATCH`.

```flat
req = http.create("PUT", "https://api.example.com/users/1")
payload = {"name": "FlatL", "active": true}
http.json(req, payload)
```

### `http.timeout(id, milliseconds)`

Sets the request timeout. Valid range: **1 … 600000** ms (10 minutes).

```flat
http.timeout(req, 15000)
```

### `http.maxBody(id, bytes)`

Sets the maximum response body size for this request (up to the FlatL string-pool chunk limit). Overflow is reported as a transport error (typically code `23` → `http.transport.overflow` via `transportOk`).

```flat
http.maxBody(req, 1048576)
```

### `http.maxRedirects(id)` / `http.maxRedirects(id, n)`

Getter/setter for redirect hop limit (default **10** at `create`).

| `n` | Meaning |
|-----|---------|
| `0` | Do not follow redirects; `status` stays on the first response (often 3xx). |
| `>= 1` | Follow at most **n** hops. |
| `-1` | Unlimited (no FlatL ceiling; transport may still stop). |

If the hop limit is exceeded while following, `send` panics with **`http.redirect.limit`**. The last status seen remains available via `status` / `responseStatuses` after a recovered panic (or before exit).

```flat
http.maxRedirects(req, 0)
limit = http.maxRedirects(req)
```

### `http.bearer(id, token)` / `http.basic(id, user, pass)`

Set `Authorization`. Both **replace** any existing `Authorization` header.

- `bearer` → `Authorization: Bearer <token>`
- `basic` → `Authorization: Basic <base64(user:pass)>` (shared Base64 helper; **no** extra `use native` required)

```flat
http.bearer(req, token)
http.basic(req, "user", "pass")
```

### `http.form(id, object)`

Builds an `application/x-www-form-urlencoded` body from an OBJECT (STRING / NUMBER / BOOL / NULL values; keys and values percent-encoded) and sets `Content-Type` accordingly. Replaces any multipart parts on this request.

```flat
http.form(req, {"name": "Ada Lovelace", "n": 1})
```

### Multipart builder (`addPart` / `addFilePart`)

Build a `multipart/form-data` body incrementally (no one-shot `http.multipart` in v1). First `addPart` / `addFilePart` switches the request to multipart mode (replaces a previous `json` / `form` / `requestBody`). `http.close` frees all part storage.

| Call | Role |
|------|------|
| `http.addPart(id, name, value)` | Text part. `value`: STRING / NUMBER / BOOL / NULL (same rules as `form`). |
| `http.addFilePart(id, name, dataOrFile, filename?)` | File part. Arg 3 = STRING / STRING REF **or** open `file` handle (bytes read now). `filename` optional → defaults to `name`. |
| `http.removePart(id, name)` | Removes **all** parts with that `name`. |
| `http.resetParts(id)` | Clears all parts; request stays live. |
| `http.partsLoad(id)` | Materializes the wire body + `Content-Type: multipart/form-data; boundary=…`. |
| `http.partsMax(id)` / `http.partsMax(id, n)` | Getter/setter for part limit (**default 32**, hard max **256**). Cannot set below current `partCount`. |
| `http.partInfo(id)` | LIST of OBJECT metadata (**no** values/bytes): `name`, `kind` (`"text"`\|`"file"`), `size`, and `filename` (file only, always set). |

`send` calls `partsLoad` automatically when parts are dirty. Wire layout: text parts use `Content-Disposition` only; file parts also get `Content-Type: application/octet-stream`.

```flat
use native http
use native file
use native list
use native obj

req = http.create("POST", "https://example.com/upload")
http.addPart(req, "note", "hello")
fp = file.readCtx("a.bin")
http.addFilePart(req, "file", fp, "a.bin")
http.addFilePart(req, "extra", blobBytes)   -- filename defaults to "extra"
http.removePart(req, "extra")

info = http.requestInfo(req)
obj.get(info, '"partCount"') -> n          -- e.g. 2

parts = http.partInfo(req)                 -- metadata only
http.partsLoad(req)                        -- optional; send does this if needed
http.send(req)
http.transportOk(req)
http.close(req)                            -- frees parts + body
```

### `http.send(id)`

Performs the request synchronously. Never panics on HTTP 4xx/5xx or on ordinary transport failure — inspect `status` / `errorCode` / `error`, or call `transportOk` / `statusOk` afterward. Redirects follow `maxRedirects` (see above). Exceeding the redirect limit → **`http.redirect.limit`**. If multipart parts are dirty, **`partsLoad` runs automatically** before the transport call.

```flat
http.send(req)
```

### `http.status(id)`

Returns the **final** HTTP status (or the last status seen if redirects stopped early). Returns `0` when no HTTP response was received (e.g. transport failure). Requires `send` first (`http.status.not_sent`).

```flat
code = http.status(req)
```

### `http.responseStatuses(id)`

Returns a LIST of NUMBER: ordered status codes from the first response through the last (e.g. `[301, 302, 200]`). With no redirects: a single-element list equal to `status`. Requires `send`.

**WASM / Fetch:** intermediate 3xx codes are usually **not** available to JavaScript; the list may be only `[final]`. Native libcurl can report the full chain when the transport exposes it. DevTools Network ≠ this API.

```flat
use native list
chain = http.responseStatuses(req)
```

### `http.responseBody(id)`

Returns the **response** body as STRING (STRING REF for larger bodies). Independent of request resource lifetime after the value is returned. Requires `send` (`http.responseBody.not_sent`).

```flat
body = http.responseBody(req)
```

### `http.responseHeaders(id)`

Returns an OBJECT mapping lowercase header names to LIST of STRING values. Repeated headers are preserved when exposed by the transport. Requires `send`.

```flat
use native obj
rh = http.responseHeaders(req)
obj.get(rh, '"content-type"') -> cts
```

### `http.error(id)` / `http.errorCode(id)`

Transport diagnostics after `send`. `errorCode` is `0` when there was no transport error; `error` is then an empty STRING. HTTP 404/500 are **not** transport errors.

```flat
msg = http.error(req)
code = http.errorCode(req)
```

### `http.transportOk(id)`

After `send`, panics if `errorCode != 0`. Panic labels: `http.transport.timeout`, `.overflow`, `.memory`, `.connect`, `.tls`, `.other`. Before `send`: `http.transportOk.not_sent`.

```flat
http.send(req)
http.transportOk(req)
```

### `http.statusOk(id)`

After `send`, requires a final HTTP **2xx** status. Panic labels include `http.status.not_found` (404), `.unauthorized` (401), `.forbidden` (403), `.conflict` (409), `.server_error` (5xx), `.redirect` (final 3xx), `.client_error` (other 4xx), `.other`. Before `send`: `http.statusOk.not_sent`.

```flat
http.send(req)
http.transportOk(req)
http.statusOk(req)
```

### `http.requestInfo(id)`

Returns an OBJECT snapshot of the live request (no body bytes). Keys: `method`, `url`, `headers`, `timeoutMs`, `maxBody`, `maxRedirects`, `sent`, `bodyLen`, **`partCount`**. Available before or after `send`. Part details → `http.partInfo` (not embedded here).

```flat
use native obj
info = http.requestInfo(req)
obj.get(info, '"method"') -> method
obj.get(info, '"bodyLen"') -> n
```

### `http.responseInfo(id)`

Returns an OBJECT snapshot of the response (no body bytes). Keys: `status`, `headers`, `error`, `errorCode`, `bodyLen`. Requires `send` (`http.responseInfo.not_sent`).

```flat
use native obj
info = http.responseInfo(req)
obj.get(info, '"status"') -> status
obj.get(info, '"bodyLen"') -> n
if n > 0
  body = http.responseBody(req)
```

### `http.close(id)`

Releases the request slot and its transport buffers. A STRING already returned by `requestBody` / `responseBody` remains valid under normal FlatL value ownership. `mem.infoResId(id, 'http')` becomes `unknown` after close.

```flat
http.close(req)
```

### End-to-end examples by method

Each sample assumes `use native http` at the top of the file.

**GET** — fetch a resource:

```flat
use native http
req = http.create("GET", "https://jsonplaceholder.typicode.com/todos/1")
http.timeout(req, 15000)
http.send(req)
http.transportOk(req)
http.statusOk(req)
io.outln(http.status(req))
io.outln(http.responseBody(req))
http.close(req)
```

**POST** — create with JSON body:

```flat
use native http
req = http.create("POST", "https://jsonplaceholder.typicode.com/posts")
http.json(req, {"title": "flatL", "body": "http post example", "userId": 1})
http.timeout(req, 15000)
http.send(req)
http.transportOk(req)
http.statusOk(req)
io.outln(http.status(req))
io.outln(http.responseBody(req))
http.close(req)
```

**PUT** — replace a resource:

```flat
use native http
req = http.create("PUT", "https://jsonplaceholder.typicode.com/posts/1")
http.json(req, {"id": 1, "title": "updated", "body": "full replace", "userId": 1})
http.send(req)
http.transportOk(req)
http.statusOk(req)
io.outln(http.responseBody(req))
http.close(req)
```

**PATCH** — partial update:

```flat
use native http
req = http.create("PATCH", "https://jsonplaceholder.typicode.com/posts/1")
http.json(req, {"title": "patched title"})
http.send(req)
http.transportOk(req)
http.statusOk(req)
io.outln(http.responseBody(req))
http.close(req)
```

**DELETE** — remove a resource:

```flat
use native http
req = http.create("DELETE", "https://jsonplaceholder.typicode.com/posts/1")
http.send(req)
http.transportOk(req)
http.statusOk(req)
io.outln(http.status(req))
http.close(req)
```

**HEAD** — headers only (empty or unused body):

```flat
use native http
req = http.create("HEAD", "https://jsonplaceholder.typicode.com/posts/1")
http.send(req)
http.transportOk(req)
http.statusOk(req)
io.outln(http.status(req))
rh = http.responseHeaders(req)
http.close(req)
```

Runnable samples: [`examples/http_get.fl`](../../examples/http_get.fl), [`examples/http_post.fl`](../../examples/http_post.fl).

## Create and send a request

```flat
use native http
req = http.create("GET", "https://api.example.com/users")
http.timeout(req, 10000)
http.send(req)

status = http.status(req)
body = http.responseBody(req)
http.close(req)
```

Same flow with other methods — set a body when the method carries one:

```flat
use native http

# POST + raw body
req = http.create("POST", "https://api.example.com/notes")
http.header(req, "Content-Type", "text/plain")
http.requestBody(req, "hello")
http.send(req)

# PUT + JSON
req = http.create("PUT", "https://api.example.com/users/1")
http.json(req, {"name": "Ada", "active": true})
http.send(req)

# PATCH + JSON
req = http.create("PATCH", "https://api.example.com/users/1")
http.json(req, {"active": false})
http.send(req)

# DELETE
req = http.create("DELETE", "https://api.example.com/users/1")
http.send(req)
```

Runnable samples (CLI + fleditor):
- GET: [`examples/http_get.fl`](../../examples/http_get.fl) / fleditor `examples/http_get.fl`
- POST + JSON: [`examples/http_post.fl`](../../examples/http_post.fl) / fleditor `examples/http_post.fl`

They use a public HTTPS JSON API that allows browser CORS so **Run** works in the WASM playground as well as with native `flvm`/`flatl`.

Supported methods include `GET`, `HEAD`, `POST`, `PUT`, `PATCH`, and `DELETE`. Only `http://` and `https://` URLs are accepted by the native transport. Redirect following is controlled by `http.maxRedirects` (default **10**). HTTPS peer and host certificate verification are enabled by default.

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

`http.requestBody(id, string)` sets a raw STRING request body. `http.requestBody(id)` returns the current request body (empty STRING when none):

```flat
req = http.create("POST", url)
http.header(req, "Content-Type", "text/plain")
http.requestBody(req, "hello")
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
- `http.responseBody(id)` returns the response body as STRING. Larger bodies are returned as STRING REF, independent of the request resource lifetime. Calling before `send` fails with `http.responseBody.not_sent`.
- `http.responseHeaders(id)` returns an OBJECT mapping lowercase header names to LIST values. Repeated response headers are preserved when exposed by the transport.
- `http.errorCode(id)` returns the transport error code (`0` means no transport error).
- `http.error(id)` returns the transport error message, or an empty STRING when no transport error occurred.

HTTP statuses such as 404 and 500 are valid HTTP responses, not transport errors. A timeout, DNS failure, connection failure, TLS failure, or body-size overflow is reported through `errorCode()` and `error()` separately from the HTTP status.

Example:

```flat
http.send(req)
status = http.status(req)
response_headers = http.responseHeaders(req)
response_body = http.responseBody(req)
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
body = http.responseBody(req)
```

When 404 is expected, call `transportOk` only and branch on `http.status(req)` — do not call `statusOk`.

## Snapshots: `requestInfo` / `responseInfo`

These return an **OBJECT** built on demand from the live request slot (no second transport buffer). They do **not** include body bytes — use `http.requestBody(id)` / `http.responseBody(id)` for the actual bytes. `bodyLen = 0` means there is no useful body.

### `http.requestInfo(id)`

Available while the request resource is live (before or after `send`):

| Key | Type | Meaning |
|-----|------|---------|
| `method` | STRING | HTTP method |
| `url` | STRING | request URL |
| `headers` | OBJECT | request headers → LIST of STRING (same shape as `http.headers` input) |
| `timeoutMs` | NUMBER | timeout in milliseconds |
| `maxBody` | NUMBER | configured max response body bytes |
| `maxRedirects` | NUMBER | redirect hop limit (`10` default, `0`, `n`, or `-1`) |
| `sent` | BOOL | whether `http.send` has been called |
| `bodyLen` | NUMBER | request body length (`0` = none / empty) |
| `partCount` | NUMBER | number of multipart parts (`0` if none) |

### `http.responseInfo(id)`

Requires `send` first (`http.responseInfo.not_sent` otherwise):

| Key | Type | Meaning |
|-----|------|---------|
| `status` | NUMBER | HTTP status (`0` if no HTTP response) |
| `headers` | OBJECT | response headers → LIST of STRING (same as `responseHeaders`) |
| `error` | STRING | transport error message (empty if none) |
| `errorCode` | NUMBER | transport error code (`0` = none) |
| `bodyLen` | NUMBER | response body length (`0` = none / empty) |

```flat
http.send(req)
info = http.responseInfo(req)
obj.get(info, '"status"') -> status
obj.get(info, '"bodyLen"') -> n
if n > 0
  body = http.responseBody(req)
```

## Limits and lifecycle

- Default timeout: 10 seconds.
- Maximum configurable timeout: 10 minutes.
- Default response body limit: 8 MiB.
- Default `maxRedirects`: **10** (`0` = no follow, `-1` = unlimited).
- Default multipart `partsMax`: **32** (hard ceiling **256** via `http.partsMax`).
- `http.maxBody(id, bytes)` changes the response body limit up to the FlatL string-pool chunk limit.
- At most 16 request resources can be live in one VM run. They are automatically released when the VM run ends.
- `http.close(id)` releases the request, transport buffers, **and all multipart part storage**. A STRING/STRING REF already returned by `http.responseBody(id)` remains valid according to normal FlatL value ownership.

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

To assert **what was sent**, set `show_request_body = true` — the mock response body becomes the request body bytes received by the transport (useful for multipart / form / json):

```toml
[http.mock]
method = "POST"
path = "/upload"
status = 200
show_request_body = true
```

Then inspect with `http.responseBody(req)` (prefer `str.contains` for large bodies — `io.outln` may preview-truncate long STRING REFs).

The fixture can also set `status = 404`, `status = 500`, or a final `status = 302` to exercise `statusOk` panic labels. Transport failures use `error_code` / `error` (for example `28` → `http.transport.timeout`). Run these tests through the regular suite:

```sh
make all
bin/run_tests -t http -v -e --categ=all
```

The mock is test-runner scoped and does not change normal HTTP behavior when `FLAT_HTTP_MOCK_FILE` is not set.
