#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    FlatBytecode bc;
    FlatRuntime *runtime;
    FlatTaskId parent = 0;
    FlatVmResult result;
    uint64_t quanta = 0;
    int rc = 0;

    if (argc != 2) return 2;
    memset(&bc, 0, sizeof bc);
    if (flat_fb_load_file(argv[1], &bc) != 0) return 3;
    runtime = flat_runtime_create(NULL);
    if (!runtime) { flat_fb_free(&bc); return 4; }

    if (flat_runtime_task_create(runtime, &bc, NULL, &parent) != 0 ||
        flat_runtime_task_start(runtime, parent) != 0) rc = 5;
    if (!rc && (flat_runtime_schedule(runtime, 1, 0, &quanta) != 0 || quanta == 0)) rc = 6;
    if (!rc && flat_runtime_task_state(runtime, parent) != FLAT_TASK_COMPLETED) {
        fprintf(stderr, "parent task did not complete: state=%d\n",
                (int)flat_runtime_task_state(runtime, parent));
        rc = 7;
    }
    /* Parent receives ID 1; the native spawn allocates the next runtime ID. */
    if (!rc && flat_runtime_task_state(runtime, parent + 1) != FLAT_TASK_COMPLETED) {
        fprintf(stderr, "spawned task did not complete: state=%d\n",
                (int)flat_runtime_task_state(runtime, parent + 1));
        rc = 8;
    }
    if (!rc && flat_runtime_task_state(runtime, parent + 2) != FLAT_TASK_FAILED) {
        fprintf(stderr, "failed spawned task state mismatch: state=%d\n",
                (int)flat_runtime_task_state(runtime, parent + 2));
        rc = 9;
    }
    if (!rc && (flat_runtime_task_result(runtime, parent, &result) != 0 ||
                result.termination != FLAT_TERM_OK)) rc = 10;
    if (!rc && (flat_runtime_task_result(runtime, parent + 2, &result) != 0 ||
                result.termination != FLAT_TERM_FAIL)) rc = 11;
    if (!rc && (flat_runtime_task_destroy(runtime, parent + 2) != 0 ||
                flat_runtime_task_destroy(runtime, parent + 1) != 0 ||
                flat_runtime_task_destroy(runtime, parent) != 0)) rc = 12;
    if (!rc && flat_runtime_destroy(runtime) != 0) rc = 13;
    if (rc) {
        fprintf(stderr, "task native integration failed: %d\n", rc);
        return rc;
    }
    flat_fb_free(&bc);
    puts("task-native-integration-ok");
    return 0;
}
