#ifndef FLAT_HTTP_TRANSPORT_H
#define FLAT_HTTP_TRANSPORT_H
#include <stddef.h>

#define FLAT_HTTP_STATUS_CHAIN_MAX 64u
#define FLAT_HTTP_DEFAULT_MAX_REDIRECTS 10L

typedef struct {
    const char *method;
    const char *url;
    const char *body;
    size_t body_len;
    const char *const *headers; /* "Name: value" lines */
    size_t header_count;
    long timeout_ms;
    size_t max_body_bytes;
    /* -1 = unlimited, 0 = do not follow, >=1 = max hops. Default at create: 10. */
    long max_redirects;
} FlatHttpRequest;

typedef struct {
    long status;
    char *body;
    size_t body_len;
    char **headers; /* received header lines, repeated headers preserved */
    size_t header_count;
    int error_code;
    char error[256];
    long status_chain[FLAT_HTTP_STATUS_CHAIN_MAX];
    size_t status_chain_count;
    int redirect_limit_hit; /* 1 when stopped because max_redirects was exceeded */
} FlatHttpResponse;

/* Transport errors do not turn HTTP 4xx/5xx into failures.
 * redirect_limit_hit may be set with a partial status_chain; caller may panic. */
int flat_http_perform(const FlatHttpRequest *req, FlatHttpResponse *out);
void flat_http_response_free(FlatHttpResponse *out);
#endif
