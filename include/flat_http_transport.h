#ifndef FLAT_HTTP_TRANSPORT_H
#define FLAT_HTTP_TRANSPORT_H
#include <stddef.h>

typedef struct {
    const char *method;
    const char *url;
    const char *body;
    size_t body_len;
    const char *const *headers; /* "Name: value" lines */
    size_t header_count;
    long timeout_ms;
    size_t max_body_bytes;
} FlatHttpRequest;

typedef struct {
    long status;
    char *body;
    size_t body_len;
    char **headers; /* received header lines, repeated headers preserved */
    size_t header_count;
    int error_code;
    char error[256];
} FlatHttpResponse;

/* Transport errors do not turn HTTP 4xx/5xx into failures. */
int flat_http_perform(const FlatHttpRequest *req, FlatHttpResponse *out);
void flat_http_response_free(FlatHttpResponse *out);
#endif
