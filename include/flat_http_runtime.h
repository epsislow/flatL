#ifndef FLAT_HTTP_RUNTIME_H
#define FLAT_HTTP_RUNTIME_H
#include "flat_http_transport.h"
#include <stdint.h>
#define FLAT_HTTP_MAX_REQUESTS 16u
#define FLAT_HTTP_DEFAULT_TIMEOUT_MS 10000L
#define FLAT_HTTP_DEFAULT_MAX_BODY (8u * 1024u * 1024u)
#define FLAT_HTTP_DEFAULT_PARTS_MAX 32u
#define FLAT_HTTP_HARD_PARTS_MAX 256u

typedef enum {
    FLAT_HTTP_PART_TEXT = 0,
    FLAT_HTTP_PART_FILE = 1
} FlatHttpPartKind;

typedef struct {
    char *name;
    FlatHttpPartKind kind;
    char *data;
    size_t data_len;
    char *filename; /* file parts only; always set when kind=file */
} FlatHttpPart;

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
    long max_redirects; /* -1 / 0 / n; default FLAT_HTTP_DEFAULT_MAX_REDIRECTS */
    int sent;
    FlatHttpResponse response;
    /* multipart builder */
    int multipart_mode;
    int parts_dirty; /* need partsLoad before send */
    FlatHttpPart *parts;
    size_t part_count;
    size_t parts_max; /* default FLAT_HTTP_DEFAULT_PARTS_MAX */
    char *multipart_boundary;
} FlatHttpRequestSlot;
typedef struct FlatHttpRuntime {
    FlatHttpRequestSlot requests[FLAT_HTTP_MAX_REQUESTS + 1u];
} FlatHttpRuntime;
int flat_http_request_live(const FlatHttpRuntime *rt, uint32_t id);
void flat_http_runtime_destroy(FlatHttpRuntime *rt);
#endif
