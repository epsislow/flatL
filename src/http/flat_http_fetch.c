#include "flat_http_transport.h"
#include <emscripten.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Body is passed as heap bytes + length (binary-safe). URL/method/headers stay UTF-8 text. */
EM_ASYNC_JS(int, flat_http_fetch_sync_js,
    (const char *urlp, const char *methodp, const unsigned char *bodyp, size_t body_len,
     const char *headerp,
     unsigned char *body_out, size_t body_cap, size_t *body_len_out,
     long *status_out, char *headers_out, size_t headers_cap,
     char *error_out, size_t error_cap, int *error_code_out, long timeout_ms), {
    const url = UTF8ToString(urlp);
    const method = UTF8ToString(methodp);
    const headerText = UTF8ToString(headerp);
    const headers = new Headers();
    for (const line of headerText.split("\n")) {
      if (!line) continue;
      const at = line.indexOf(":");
      if (at > 0) headers.append(line.slice(0, at).trim(), line.slice(at + 1).trim());
    }
    let body;
    if (method !== "GET" && method !== "HEAD" && body_len > 0) {
      body = HEAPU8.slice(bodyp, bodyp + body_len);
    }
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeout_ms);
    try {
      const response = await fetch(url, {
        method, headers, body,
        redirect: "follow", signal: controller.signal
      });
      const data = new Uint8Array(await response.arrayBuffer());
      if (data.byteLength > body_cap) {
        stringToUTF8("response body exceeds configured limit", error_out, error_cap);
        setValue(error_code_out, 23, "i32");
        setValue(status_out, response.status, "i32");
        return -1;
      }
      HEAPU8.set(data, body_out);
      setValue(body_len_out, data.byteLength, "i32");
      setValue(status_out, response.status, "i32");
      let h = "";
      response.headers.forEach((v, k) => { h += k.toLowerCase() + ": " + v + "\n"; });
      stringToUTF8(h, headers_out, headers_cap);
      setValue(error_code_out, 0, "i32");
      return 0;
    } catch (e) {
      const timeout = e && (e.name === "AbortError" || e.name === "TimeoutError");
      stringToUTF8(String(e && e.message ? e.message : e), error_out, error_cap);
      setValue(error_code_out, timeout ? 28 : 7, "i32");
      setValue(status_out, 0, "i32");
      return -1;
    } finally {
      clearTimeout(timer);
    }
});

static int append_response_header(FlatHttpResponse *out, const char *line, size_t n) {
    char **next; char *copy; size_t i;
    while (n && (line[n-1] == '\r' || line[n-1] == '\n')) --n;
    if (!n || n > 4096 || !memchr(line, ':', n)) return 0;
    copy = malloc(n + 1); if (!copy) return -1;
    memcpy(copy, line, n); copy[n] = '\0';
    for (i=0; copy[i] && copy[i] != ':'; ++i) if(copy[i]>='A'&&copy[i]<='Z') copy[i]=(char)(copy[i]+'a'-'A');
    next = realloc(out->headers, (out->header_count+1)*sizeof *next);
    if (!next) { free(copy); return -1; }
    out->headers = next; out->headers[out->header_count++] = copy; return 0;
}

int flat_http_perform(const FlatHttpRequest *req, FlatHttpResponse *out) {
    char *headers = NULL; size_t headers_cap = 16384, i, used = 0, body_len = 0;
    const unsigned char *bodyp;
    int rc;
    if (!req || !out || !req->url || !req->method || !req->max_body_bytes || req->timeout_ms <= 0) return -1;
    memset(out, 0, sizeof *out);
    headers = calloc(headers_cap, 1);
    out->body = malloc(req->max_body_bytes + 1);
    if (!headers || !out->body) { free(headers); flat_http_response_free(out); return -1; }
    for (i=0; i<req->header_count; ++i) {
        size_t n = strlen(req->headers[i]);
        if (used + n + 2 >= headers_cap) { out->error_code=23; snprintf(out->error,sizeof out->error,"request headers exceed limit"); rc=-1; goto done; }
        memcpy(headers+used, req->headers[i], n); used += n; headers[used++]='\n';
    }
    bodyp = (const unsigned char *)(req->body ? req->body : "");
    rc = flat_http_fetch_sync_js(req->url, req->method, bodyp, req->body ? req->body_len : 0,
        headers,
        (unsigned char *)out->body, req->max_body_bytes, &body_len, &out->status,
        headers, headers_cap, out->error, sizeof out->error, &out->error_code, req->timeout_ms);
    out->body_len = body_len; out->body[body_len] = '\0';
    if (rc == 0 && headers[0]) {
        char *p = headers, *nl;
        while ((nl = strchr(p, '\n')) != NULL) { if (append_response_header(out, p, (size_t)(nl-p)) != 0) break; p = nl + 1; }
    }
done:
    free(headers); return rc;
}

void flat_http_response_free(FlatHttpResponse *out) {
    size_t i; if (!out) return;
    for (i=0; i<out->header_count; ++i) free(out->headers[i]);
    free(out->headers); free(out->body); memset(out, 0, sizeof *out);
}
