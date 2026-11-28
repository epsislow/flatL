#include "flat_runtime.h"
#include "flat_runtime_ws_server.h"
#include <stdio.h>
#include <stdint.h>
int main(void) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    uint64_t listener_id = 0; uint16_t bound_port = 0;
    char error[256] = {0}; int i;
    if (!runtime) return 2;
    if (flat_runtime_ws_server_listen(runtime, "127.0.0.1", 0, 8,
            &listener_id, &bound_port, error, sizeof error) != 0 || !listener_id || !bound_port) {
        fprintf(stderr, "listen failed: %s\n", error); (void)flat_runtime_destroy(runtime); return 3;
    }
    if (flat_runtime_ws_server_close_listener(runtime, listener_id) != 0) {
        (void)flat_runtime_destroy(runtime); return 4;
    }
    for (i = 0; i < 12; ++i) {
        listener_id = 0; bound_port = 0;
        if (flat_runtime_ws_server_listen(runtime, "127.0.0.1", 0, 8,
                &listener_id, &bound_port, error, sizeof error) != 0 || !listener_id || !bound_port) {
            fprintf(stderr, "repeated listen failed: %s\n", error); (void)flat_runtime_destroy(runtime); return 5;
        }
        if (i != 11 && flat_runtime_ws_server_close_listener(runtime, listener_id) != 0) {
            (void)flat_runtime_destroy(runtime); return 6;
        }
    }
    /* Leave the last listener open: runtime teardown owns its cleanup. */
    if (flat_runtime_destroy(runtime) != 0) return 7;
    puts("runtime-websocket-ownership-ok"); return 0;
}
