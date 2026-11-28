#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    FlatBytecode bc;
    FlatRuntime *runtime;
    FlatTaskId root = 0;
    uint64_t quanta = 0;
    int rc = 0;
    if (argc != 2) return 2;
    memset(&bc, 0, sizeof bc);
    if (flat_fb_load_file(argv[1], &bc) != 0) return 3;
    runtime = flat_runtime_create(NULL);
    if (!runtime) { flat_fb_free(&bc); return 4; }
    if (flat_runtime_task_create(runtime, &bc, NULL, &root) != 0 ||
        flat_runtime_task_start(runtime, root) != 0) rc = 5;
    if (!rc && flat_runtime_schedule(runtime, 1, 0, &quanta) != 0) rc = 6;
    if (!rc && flat_runtime_task_state(runtime, root) != FLAT_TASK_COMPLETED) rc = 7;
    if (!rc && flat_runtime_task_state(runtime, root + 1) != FLAT_TASK_COMPLETED) rc = 8;
    if (!rc && (flat_runtime_task_destroy(runtime, root + 1) != 0 ||
                flat_runtime_task_destroy(runtime, root) != 0)) rc = 9;
    if (!rc && flat_runtime_destroy(runtime) != 0) rc = 10;
    flat_fb_free(&bc);
    if (rc) { fprintf(stderr, "sockets native bridge failed: %d\n", rc); return rc; }
    puts("sockets-native-bridge-harness-ok");
    return 0;
}
