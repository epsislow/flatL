#ifndef FLAT_HTTP_RUNTIME_H
#define FLAT_HTTP_RUNTIME_H
#include "flat_http_transport.h"
#include <stdint.h>
#define FLAT_HTTP_MAX_REQUESTS 16u
#define FLAT_HTTP_DEFAULT_TIMEOUT_MS 10000L
#define FLAT_HTTP_DEFAULT_MAX_BODY (8u * 1024u * 1024u)
typedef struct {
    int live;
    char *method;
    char *url;
    char *body;
    size_t body_len;
    char **headers;
    size_t header_count;
    long timeout_ms;
    size_t max_body_bytes;
    int sent;
    FlatHttpResponse response;
} FlatHttpRequestSlot;
typedef struct FlatHttpRuntime {
    FlatHttpRequestSlot requests[FLAT_HTTP_MAX_REQUESTS + 1u];
} FlatHttpRuntime;
int flat_http_request_live(const FlatHttpRuntime *rt, uint32_t id);
void flat_http_runtime_destroy(FlatHttpRuntime *rt);
#endif
