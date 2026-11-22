#include "flat_http_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    FlatHttpRequest req = {0};
    FlatHttpResponse resp;
    int ok;
    if (argc != 4) return 2;
    req.method = "GET";
    req.url = argv[1];
    req.timeout_ms = 3000;
    req.max_body_bytes = 1024 * 1024;
    if (flat_http_perform(&req, &resp)) return 3;
    ok = resp.status == atol(argv[2]) && resp.body && strcmp(resp.body, argv[3]) == 0;
    flat_http_response_free(&resp);
    if (!ok) { fputs("response mismatch\n", stderr); return 4; }
    return 0;
}
