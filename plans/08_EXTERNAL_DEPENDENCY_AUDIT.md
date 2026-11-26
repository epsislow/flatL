# FlatL v2.0 — External Dependency Concurrency Audit

**Checkpoint:** archive 293  
**Baseline inspected:** archive 292 source tree  
**Status:** dependency documentation review plus one HTTP transport hardening change. This is not a blanket worker-mode certification.

## 1. Scope

This checkpoint reviews the external libraries used by native VM handlers and the exact way FlatL currently calls them. Upstream statements are treated as library guarantees only within their documented conditions. A library being thread-safe does not make shared handles, FlatL contexts, or the runtime scheduler thread-safe.

## 2. PCRE2

**Bundled version:** PCRE2 10.49, 8-bit, built without JIT (`external/pcre2/README.md`, `external/pcre2/version`, `external/pcre2/config.h`).

Upstream PCRE2 documentation states that the library code is thread-safe and has no static/global variables. A compiled `pcre2_code` object is immutable after compilation and may be shared for concurrent matching; match data and other mutable per-match state must remain separate. FlatL's current `mod_re.c` is more conservative: it compiles a pattern per native call, creates `pcre2_match_data` for that call, and frees both before returning. No compiled pattern or match-data object is shared between VM calls.

**Assessment:** `re` is a strong `THREAD_SAFE` candidate for distinct VM contexts with the bundled no-JIT build and default allocators. It is not yet enabled in worker mode: complete-VM concurrency tests and sanitizer runs remain required. The optional system-PCRE2 build (`re_compile=0`) is a separate deployment variant and must be validated independently.

Upstream reference: https://www.pcre.org/current/doc/html/pcre2api.html (Multithreading).

## 3. libcurl

**Observed local validation environment:** libcurl 8.10.1; `curl-config --features` reports `AsynchDNS` and `threadsafe`. This is a property of the validation host, not a promise about every FlatL deployment.

FlatL's native transport creates one `CURL *` easy handle per request and owns its callback state, response buffers, header list and error buffer within that request. It does not use the libcurl share interface or share an easy handle between calls. `curl_global_init()` is protected against concurrent duplicate FlatL initialization by a process-wide atomic gate; there is no `curl_global_cleanup()` path.

The upstream thread-safety documentation requires that one handle is not used simultaneously by multiple threads. It also documents signal behavior: threaded applications should set `CURLOPT_NOSIGNAL` on every easy handle. When this option is enabled, DNS lookup timeout behavior depends on the resolver backend; asynchronous/threaded resolver support is needed if DNS timeouts must be honored.

Archive 293 therefore: 
- sets `CURLOPT_NOSIGNAL` on every native easy handle;
- adds `flat_http_prepare()` to `include/flat_http_transport.h`; native builds use it to run `curl_global_init()` explicitly, while the WASM Fetch transport returns success as a no-op;
- requires a native host to call `flat_http_prepare()` before creating worker threads, so lazy global initialization does not first occur inside a worker;
- requires deployments to inspect their libcurl build/runtime features and resolver backend before enabling concurrent HTTP calls.

**Assessment:** per-request easy handles are compatible with concurrent use under libcurl's documented model, but FlatL HTTP is not unconditionally certified for all libcurl builds. The worker startup path must pre-initialize transport and the supported build matrix must verify `CURL_VERSION_THREADSAFE` or guarantee initialization before any process threads exist. Keep `CURLOPT_NOSIGNAL=1`; record the resolver timeout tradeoff.

Upstream references: https://curl.se/libcurl/c/threadsafe.html and https://curl.se/libcurl/c/libcurl.html.

## 4. Lexbor

**Bundled version:** Lexbor 3.0.0 amalgamation (`external/lexbor/README.md`, `external/lexbor/version`). FlatL creates a `FlatDomRuntime` per VM and each DOM document is allocated and owned by that runtime. The native module does not share document/parser handles across VM contexts.

The upstream documentation reviewed for this checkpoint describes document creation/parsing and build options, but does not make an explicit general thread-safety guarantee for concurrent independent documents. Source inspection of the bundled amalgamation found process-global allocator function pointers (`lexbor_memory_setup()` can replace them) and immutable/static lookup or sentinel data. FlatL does not call `lexbor_memory_setup()`, so the default C allocator remains in use; the host must not reconfigure Lexbor allocators while workers are active.

**Assessment/status:** `dom` is **`PENDING_THREAD_SAFETY_VALIDATION`**, not `UNSUPPORTED`. Independent document contexts appear structurally isolated, but the reviewed upstream documentation does not provide an explicit general concurrent-use guarantee. First attempt to qualify `THREAD_SAFE` with concurrent complete-VM DOM tests and sanitizer runs for bundled Lexbor 3.0.0 and each supported system-Lexbor variant. If independent concurrent use cannot be certified, use `SERIALIZED` as the preferred fallback: one process-wide DOM invocation gate must cover every Lexbor API call, including document destruction in `flat_dom_runtime_destroy()` during VM teardown. A gate around `dom.*` handlers alone is insufficient. Reserve `UNSUPPORTED` for a demonstrated constraint that cannot be contained by independent contexts or this gate. Any custom allocator setup must happen before worker start and remain frozen.

Upstream references: https://lexbor.com/documentation/ and https://github.com/lexbor/lexbor.

## 5. C runtime and platform services

This checkpoint does not certify all C runtime/platform functions. FlatL uses standard allocation, string conversion, filesystem, time, locale and environment APIs across modules. The project-specific audit already documents the constraints on process `environ`, stdio streams, locale selection, path staging and logger configuration. These remain subject to the supported OS/runtime matrix and host contract. In particular, caller-owned `FILE *`, callbacks, environment mutation and process-global locale changes must not be shared or mutated concurrently without an explicit synchronization boundary.

## 6. Changes in archive 293

- `src/http/flat_http_curl.c` sets `CURLOPT_NOSIGNAL=1L` for each easy handle.
- `include/flat_http_transport.h` exposes `flat_http_prepare()` as the host pre-start initialization hook.
- `src/http/flat_http_curl.c` implements the hook by calling the existing once-only global initializer.
- `src/http/flat_http_fetch.c` implements the same hook as a no-op for WASM.
- No worker pool, task scheduler mutex, or pthread worker lifecycle was introduced.

## 7. Acceptance status

- [x] Review upstream PCRE2 multithreading contract and compare it with FlatL's per-call objects.
- [x] Review libcurl handle, global initialization and signal requirements.
- [x] Add explicit transport pre-initialization API and set `CURLOPT_NOSIGNAL`.
- [x] Review Lexbor ownership and identify allocator reconfiguration as setup-only global state.
- [x] Set DOM audit status to `PENDING_THREAD_SAFETY_VALIDATION`; specify `SERIALIZED` as the fallback if independent contexts cannot be certified.
- [x] Include Lexbor-backed VM/DOM teardown in the required serialized boundary.
- [ ] Run concurrent independent FlatL DOM/PCRE2/HTTP VM tests after worker scheduler implementation.
- [ ] Validate system-library build variants and supported OS/C runtime matrix.
- [ ] Confirm libcurl runtime thread-safety/resolver features on each supported deployment target.
- [ ] Run complete worker test matrix under ThreadSanitizer and AddressSanitizer/UndefinedBehaviorSanitizer.

**Exit rule:** PCRE2 is a strong candidate under the bundled configuration; libcurl is conditionally compatible when initialized and configured as documented; Lexbor remains `PENDING_THREAD_SAFETY_VALIDATION`. Try `THREAD_SAFE` only after concurrent complete-VM tests and sanitizers; otherwise prefer `SERIALIZED` with a process-wide gate covering handlers and teardown. `UNSUPPORTED` requires a demonstrated uncontainable constraint. None of these findings removes the runtime scheduler/task/Channel synchronization blockers.


## 8. Archive 294 — DOM capability status and fallback

The DOM audit status is `PENDING_THREAD_SAFETY_VALIDATION`. This is deliberately not the runtime capability `UNSUPPORTED`. FlatL's per-VM `FlatDomRuntime` and document ownership make independent-context concurrency plausible, but do not prove the behavior of all Lexbor internals or supported build variants.

Acceptance sequence:

1. Exercise multiple complete VMs concurrently, each creating, parsing, querying, mutating, serializing, destroying and recreating only its own documents.
2. Run the test against bundled Lexbor 3.0.0 and each supported system-Lexbor configuration.
3. Run ThreadSanitizer and AddressSanitizer/UndefinedBehaviorSanitizer where supported.
4. Review any reported shared state and confirm the upstream contract for the exact deployed version/build.

If the independent-context path cannot be certified, classify DOM as `SERIALIZED`: a single process-wide DOM gate must surround every Lexbor entry point. This includes `flat_native_dom_*` handler calls and the Lexbor document-destruction path in `flat_dom_runtime_destroy()`, which is called during VM teardown outside the native handler. VM teardown must acquire the same gate before destroying DOM resources. The gate is held only for a bounded Lexbor operation/teardown; it is not held across a VM quantum, task wait, blocking I/O, or arbitrary host callback, and it is never nested with scheduler or Channel locks.

`lexbor_memory_setup()` and any future process-global Lexbor configuration remain setup-only and immutable while workers or DOM runtimes are active. `UNSUPPORTED` is reserved for a demonstrated constraint that cannot be contained by independent contexts or the process-wide gate. Archive 294 changes this policy/documentation only: no gate is implemented and DOM is not yet enabled in worker mode.
