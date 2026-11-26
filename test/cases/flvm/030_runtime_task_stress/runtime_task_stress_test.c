#include "fb_format.h"
#include "flat_channel.h"
#include "flat_task.h"
#include "flat_runtime.h"
#include "flat_value.h"

#include <stdio.h>
#include <string.h>

#define TASKS 48
#define CHANNELS 24
#define VALUES 12

int main(int argc, char **argv) {
    FlatBytecode bc;
    FlatRuntime *runtime = NULL;
    FlatTaskId tasks[TASKS];
    FlatChannelId channels[CHANNELS];
    FlatCollectionPool *coll = NULL;
    FlatStringPool *pool = NULL;
    FlatValue value, out;
    uint64_t turns = 0;
    int i, j, rc = 0;
    memset(&bc, 0, sizeof bc);
    memset(tasks, 0, sizeof tasks);
    memset(channels, 0, sizeof channels);
    memset(&value, 0, sizeof value);
    memset(&out, 0, sizeof out);
    if (argc != 2 || flat_fb_load_file(argv[1], &bc) != 0) return 2;
    runtime = flat_runtime_create(NULL);
    coll = flat_coll_create();
    pool = flat_pool_create();
    if (!runtime || !coll || !pool) { rc = 3; goto done; }

    for (i = 0; i < CHANNELS; ++i) {
        if (flat_runtime_channel_create(runtime, &channels[i]) != 0) { rc = 4; goto done; }
        for (j = 0; j < VALUES; ++j) {
            flat_value_set_number(&value, (double)(i * VALUES + j));
            {
                char key[32];
                snprintf(key, sizeof key, "v%d", j);
                if (flat_runtime_channel_set(runtime, channels[i], 0, key, &value,
                                             coll, pool, NULL) != 0) { rc = 5; goto done; }
            }
        }
    }
    for (i = 0; i < CHANNELS; ++i) {
        for (j = 0; j < VALUES; ++j) {
            char key[32];
            snprintf(key, sizeof key, "v%d", j);
            if (flat_runtime_channel_var(runtime, channels[i], 0, key, coll, pool,
                                         NULL, &out) != 0 || out.tag != VAL_NUMBER ||
                out.u.number.payload.f64 != (double)(i * VALUES + j)) { rc = 6; goto done; }
            flat_value_release(coll, pool, &out);
            out.tag = VAL_EMPTY;
        }
    }
    for (i = 0; i < TASKS; ++i) {
        if (flat_runtime_task_create(runtime, &bc, NULL, &tasks[i]) != 0 ||
            flat_runtime_task_start(runtime, tasks[i]) != 0) { rc = 7; goto done; }
    }
    if (flat_runtime_schedule(runtime, 1, 0, &turns) != 0 || turns < TASKS) { rc = 8; goto done; }
    for (i = 0; i < TASKS; ++i) {
        FlatVmResult result;
        if (flat_runtime_task_state(runtime, tasks[i]) != FLAT_TASK_COMPLETED ||
            flat_runtime_task_result(runtime, tasks[i], &result) != 0 ||
            result.termination != FLAT_TERM_OK) { rc = 9; goto done; }
    }
    for (i = 0; i < TASKS; ++i) {
        if (flat_runtime_task_destroy(runtime, tasks[i]) != 0) { rc = 10; goto done; }
        tasks[i] = 0;
    }
    for (i = 0; i < CHANNELS; ++i) {
        if (flat_runtime_channel_destroy(runtime, channels[i]) != 0) { rc = 11; goto done; }
        channels[i] = 0;
    }
    if (flat_runtime_destroy(runtime) != 0) { rc = 12; goto done; }
    runtime = NULL;

done:
    if (out.tag != VAL_EMPTY) flat_value_release(coll, pool, &out);
    flat_pool_destroy(pool);
    flat_coll_destroy(coll);
    if (runtime) {
        for (i = 0; i < TASKS; ++i) {
            if (tasks[i]) {
                (void)flat_runtime_task_cancel(runtime, tasks[i]);
                (void)flat_runtime_task_destroy(runtime, tasks[i]);
            }
        }
        for (i = 0; i < CHANNELS; ++i)
            if (channels[i]) (void)flat_runtime_channel_destroy(runtime, channels[i]);
        (void)flat_runtime_destroy(runtime);
    }
    flat_fb_free(&bc);
    if (rc) fprintf(stderr, "runtime task stress test failed: %d\n", rc);
    else printf("runtime-task-stress-ok tasks=%d channels=%d values=%d turns=%llu\n",
                TASKS, CHANNELS, CHANNELS * VALUES, (unsigned long long)turns);
    return rc;
}
