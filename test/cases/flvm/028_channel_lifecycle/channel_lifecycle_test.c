#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"
#include "flat_channel.h"
#include "flat_runtime_log.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    FlatBytecode bc;
    FlatRuntime *runtime = NULL;
    FlatTaskId parent = 0;
    FlatTaskId child = 0;
    uint64_t turns = 0;
    unsigned i;
    int rc = 0;

    if (argc != 2) return 2;
    memset(&bc, 0, sizeof bc);
    if (flat_fb_load_file(argv[1], &bc) != 0) return 3;
    if (flat_runtime_log_configure(&(FlatRuntimeLogConfig){
            .categories = FLAT_RUNTIME_LOG_CHANNELS,
            .buffer_size = 65536u,
        }) != 0) {
        flat_fb_free(&bc);
        return 4;
    }
    runtime = flat_runtime_create(NULL);
    if (!runtime || flat_runtime_task_create(runtime, &bc, NULL, &parent) != 0 ||
        flat_runtime_task_start(runtime, parent) != 0) {
        rc = 5;
        goto done;
    }
    child = parent + 1;

    /* Advance only until spawn has registered the child. Its direct numeric
     * Channel argument must pin Channel #1 before the child executes. */
    for (i = 0; i < 256 && flat_runtime_task_state(runtime, child) == 0; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) {
            rc = 6;
            goto done;
        }
    }
    if (flat_runtime_task_state(runtime, child) == 0 ||
        flat_runtime_channel_destroy(runtime, 1) == 0) {
        fprintf(stderr, "Channel was destroyed while a spawned task still referenced it\n");
        rc = 7;
        goto done;
    }

    if (flat_runtime_schedule(runtime, 1, 0, &turns) != 0 || turns == 0 ||
        flat_runtime_task_state(runtime, parent) != FLAT_TASK_COMPLETED ||
        flat_runtime_task_state(runtime, child) != FLAT_TASK_COMPLETED) {
        fprintf(stderr, "Channel lifecycle tasks did not complete\n");
        rc = 8;
        goto done;
    }
    /* The parent destroys Channel #1 after task.wait returns. */
    if (flat_runtime_channel_destroy(runtime, 1) == 0) {
        fprintf(stderr, "Channel unexpectedly remained registered after destroy\n");
        rc = 9;
        goto done;
    }
    if (flat_runtime_task_destroy(runtime, child) != 0 ||
        flat_runtime_task_destroy(runtime, parent) != 0) {
        rc = 10;
        goto done;
    }
    if (flat_runtime_destroy(runtime) != 0) {
        rc = 11;
        runtime = NULL;
        goto done;
    }
    runtime = NULL;

done:
    if (runtime) {
        /* Best-effort cleanup for test failures. */
        if (flat_runtime_task_state(runtime, child) == FLAT_TASK_COMPLETED ||
            flat_runtime_task_state(runtime, child) == FLAT_TASK_FAILED ||
            flat_runtime_task_state(runtime, child) == FLAT_TASK_CANCELLED)
            (void)flat_runtime_task_destroy(runtime, child);
        if (flat_runtime_task_state(runtime, parent) == FLAT_TASK_COMPLETED ||
            flat_runtime_task_state(runtime, parent) == FLAT_TASK_FAILED ||
            flat_runtime_task_state(runtime, parent) == FLAT_TASK_CANCELLED)
            (void)flat_runtime_task_destroy(runtime, parent);
        (void)flat_runtime_channel_destroy(runtime, 1);
        (void)flat_runtime_destroy(runtime);
    }
    flat_fb_free(&bc);
    flat_runtime_log_shutdown();
    if (rc) return rc;
    puts("channel-lifecycle-ok");
    return 0;
}
