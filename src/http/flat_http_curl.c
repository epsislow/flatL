#include "flat_http_transport.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

typedef struct {
    FlatHttpResponse *response;
    size_t max_body;
    int overflow;
} Transfer;

static size_t write_body(char *data, size_t size, size_t count, void *arg) {
    Transfer *t = arg;
    size_t n, total;
    char *next;
    if (size && count > SIZE_MAX / size) return 0;
    n = size * count;
    if (n > t->max_body - t->response->body_len) { t->overflow = 1; return 0; }
    total = t->response->body_len + n;
    next = realloc(t->response->body, total + 1);
    if (!next) return 0;
    t->response->body = next;
    memcpy(next + t->response->body_len, data, n);
    next[total] = '\0';
    t->response->body_len = total;
    return n;
}

static size_t write_header(char *data, size_t size, size_t count, void *arg) {
    Transfer *t = arg;
    FlatHttpResponse *r = t->response;
    size_t n, i;
    char *line;
    char **new_headers;
    if (size && count > SIZE_MAX / size) return 0;
    n = size * count;
    if (n == 0 || n > 16384) return 0;
    if (n < 2 || data[0] == '\r' || !memchr(data, ':', n)) return n;
    if (r->header_count >= 256) return 0;
    line = malloc(n + 1);
    if (!line) return 0;
    memcpy(line, data, n);
    while (n && (line[n - 1] == '\r' || line[n - 1] == '\n')) --n;
    line[n] = '\0';
    for (i = 0; i < n && line[i] != ':'; ++i) {
        if (line[i] >= 'A' && line[i] <= 'Z') line[i] += 'a' - 'A';
    }
    new_headers = realloc(r->headers, (r->header_count + 1) * sizeof *new_headers);
    if (!new_headers) { free(line); return 0; }
    r->headers = new_headers;
    r->headers[r->header_count++] = line;
    return size * count;
}

void flat_http_response_free(FlatHttpResponse *out) {
    size_t i;
    if (!out) return;
    for (i = 0; i < out->header_count; ++i) free(out->headers[i]);
    free(out->headers);
    free(out->body);
    memset(out, 0, sizeof *out);
}

int flat_http_perform(const FlatHttpRequest *req, FlatHttpResponse *out) {
    CURL *curl;
    CURLcode result;
    struct curl_slist *headers = NULL;
    Transfer transfer;
    size_t i;
    char curl_error[CURL_ERROR_SIZE] = {0};
    if (!req || !out || !req->url || !req->method || !req->max_body_bytes ||
        req->max_body_bytes == SIZE_MAX || req->timeout_ms <= 0) return -1;
    memset(out, 0, sizeof *out);
    curl = curl_easy_init();
    if (!curl) { snprintf(out->error, sizeof out->error, "curl init failed"); return -1; }
    transfer.response = out;
    transfer.max_body = req->max_body_bytes;
    transfer.overflow = 0;
    curl_easy_setopt(curl, CURLOPT_URL, req->url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req->method);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, req->timeout_ms);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &transfer);
    if (req->body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)req->body_len);
    }
    for (i = 0; i < req->header_count; ++i) {
        struct curl_slist *next = curl_slist_append(headers, req->headers[i]);
        if (!next) { curl_slist_free_all(headers); curl_easy_cleanup(curl); return -1; }
        headers = next;
    }
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out->status);
    out->error_code = (int)result;
    if (result != CURLE_OK) snprintf(out->error, sizeof out->error, "%s", transfer.overflow ? "response body exceeds configured limit" : (curl_error[0] ? curl_error : curl_easy_strerror(result)));
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result == CURLE_OK ? 0 : -1;
}
