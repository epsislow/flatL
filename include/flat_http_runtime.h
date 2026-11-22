#ifndef FLAT_HTTP_RUNTIME_H
#define FLAT_HTTP_RUNTIME_H
#include <stddef.h>
#define FLAT_HTTP_MAX_REQUESTS 32
#define FLAT_HTTP_MAX_BODY (16u * 1024u * 1024u)
typedef struct {
 int live;
 char *method, *url, *body, *error;
 size_t body_len;
 long status, error_code, timeout_ms;
} FlatHttpRequest;
typedef struct FlatHttpRuntime { FlatHttpRequest requests[FLAT_HTTP_MAX_REQUESTS + 1]; } FlatHttpRuntime;
void flat_http_runtime_close(FlatHttpRuntime *rt);
int flat_http_request_live(const FlatHttpRuntime *rt, unsigned id);
#endif
