#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    FlatBytecode bc;
    FlatRuntime *runtime;
    FlatTaskId parent = 0;
    uint64_t quanta = 0;
    int rc = 0;

    if (argc != 2) return 2;
    memset(&bc, 0, sizeof bc);
    if (flat_fb_load_file(argv[1], &bc) != 0) return 3;
    runtime = flat_runtime_create(NULL);
    if (!runtime) { flat_fb_free(&bc); return 4; }

    if (flat_runtime_task_create(runtime, &bc, NULL, &parent) != 0 ||
        flat_runtime_task_start(runtime, parent) != 0) rc = 5;
    /* A one-instruction quantum forces transaction bodies to yield between every
     * native and math instruction. The scheduler must keep the lock owner running. */
    if (!rc && (flat_runtime_schedule(runtime, 1, 0, &quanta) != 0 || quanta == 0)) rc = 6;
    if (!rc && flat_runtime_task_state(runtime, parent) != FLAT_TASK_COMPLETED) {
        fprintf(stderr, "channel parent state=%d\n", (int)flat_runtime_task_state(runtime, parent));
        rc = 7;
    }
    if (!rc && (flat_runtime_task_state(runtime, parent + 1) != FLAT_TASK_COMPLETED ||
                flat_runtime_task_state(runtime, parent + 2) != FLAT_TASK_COMPLETED ||
                flat_runtime_task_state(runtime, parent + 3) != FLAT_TASK_FAILED)) {
        fprintf(stderr, "channel workers states=%d,%d,%d\n",
                (int)flat_runtime_task_state(runtime, parent + 1),
                (int)flat_runtime_task_state(runtime, parent + 2),
                (int)flat_runtime_task_state(runtime, parent + 3));
        rc = 8;
    }
    if (!rc && (flat_runtime_task_destroy(runtime, parent + 3) != 0 ||
                flat_runtime_task_destroy(runtime, parent + 2) != 0 ||
                flat_runtime_task_destroy(runtime, parent + 1) != 0 ||
                flat_runtime_task_destroy(runtime, parent) != 0)) rc = 9;
    if (!rc && flat_runtime_destroy(runtime) != 0) rc = 10;
    if (rc) {
        fprintf(stderr, "channel native integration failed: %d\n", rc);
        return rc;
    }
    flat_fb_free(&bc);
    puts("channel-native-ok");
    return 0;
}
