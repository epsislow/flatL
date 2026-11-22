#include "flat_http_transport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    FlatHttpRequest q = {0};
    FlatHttpResponse r;
    if (argc != 2) return 2;
    q.method = "GET";
    q.url = argv[1];
    q.timeout_ms = 3000;
    q.max_body_bytes = 1024 * 1024;
    assert(flat_http_perform(&q, &r) == 0);
    assert(r.status == 200);
    assert(r.body && strstr(r.body, "mock-ok"));
    flat_http_response_free(&r);
    puts("http transport smoke ok");
    return 0;
}
