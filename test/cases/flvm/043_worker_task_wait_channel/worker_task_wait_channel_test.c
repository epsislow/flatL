#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define RUNS 20
#define WORKERS 4

static void pause_one_ms(void) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000L;
    (void)nanosleep(&ts, NULL);
}

static int run_once(const FlatBytecode *bc, unsigned run) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    FlatTaskId parent = 0;
    FlatTaskId children[3] = {0, 0, 0};
    unsigned i;
    int rc = -1;

    if (!runtime) return -1;
    if (flat_runtime_task_create(runtime, bc, NULL, &parent) != 0 ||
        flat_runtime_task_start(runtime, parent) != 0 ||
        flat_runtime_workers_start(runtime, WORKERS, 1) != 0) {
        fprintf(stderr, "run %u: worker startup failed\n", run);
        goto done;
    }

    /* The Flat program spawns two Channel-lock contenders, waits for both,
     * then spawns a task which attempts task.wait while holding a transaction.
     * That final child must fail rather than suspend while owning the Channel. */
    for (i = 0; i < 10000; ++i) {
        FlatTaskState state = flat_runtime_task_state(runtime, parent);
        if (state == FLAT_TASK_COMPLETED) break;
        if (state == FLAT_TASK_FAILED || state == FLAT_TASK_CANCELLED) {
            fprintf(stderr, "run %u: parent terminated unexpectedly state=%d\n", run, (int)state);
            goto done;
        }
        pause_one_ms();
    }
    if (flat_runtime_task_state(runtime, parent) != FLAT_TASK_COMPLETED) {
        fprintf(stderr, "run %u: parent did not complete\n", run);
        goto done;
    }

    /* task.spawn allocates the two successful children and the deliberately
     * invalid waiter immediately after the parent in this single-root program. */
    children[0] = parent + 1;
    children[1] = parent + 2;
    children[2] = parent + 3;
    if (flat_runtime_task_state(runtime, children[0]) != FLAT_TASK_COMPLETED ||
        flat_runtime_task_state(runtime, children[1]) != FLAT_TASK_COMPLETED ||
        flat_runtime_task_state(runtime, children[2]) != FLAT_TASK_FAILED) {
        fprintf(stderr, "run %u: child states %d,%d,%d\n", run,
                (int)flat_runtime_task_state(runtime, children[0]),
                (int)flat_runtime_task_state(runtime, children[1]),
                (int)flat_runtime_task_state(runtime, children[2]));
        goto done;
    }

    if (flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_DRAIN) != 0) {
        fprintf(stderr, "run %u: DRAIN request failed\n", run);
        goto done;
    }
    for (i = 0; i < 10000 &&
         flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++i)
        pause_one_ms();
    if (flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE ||
        flat_runtime_workers_join(runtime) != 0) {
        fprintf(stderr, "run %u: shutdown/join failed status=%d\n", run,
                (int)flat_runtime_shutdown_status(runtime));
        goto done;
    }

    for (i = 0; i < 3; ++i) {
        if (flat_runtime_task_destroy(runtime, children[i]) != 0) {
            fprintf(stderr, "run %u: child destroy %u failed\n", run, i);
            goto done;
        }
    }
    if (flat_runtime_task_destroy(runtime, parent) != 0 ||
        flat_runtime_destroy(runtime) != 0) {
        runtime = NULL;
        fprintf(stderr, "run %u: runtime teardown failed\n", run);
        return -1;
    }
    runtime = NULL;
    rc = 0;

done:
    if (runtime) {
        (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        for (i = 0; i < 10000 &&
             flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++i)
            pause_one_ms();
        if (flat_runtime_lifecycle(runtime) == FLAT_RUNTIME_STOPPED)
            (void)flat_runtime_workers_join(runtime);
        for (i = 0; i < 3; ++i)
            if (children[i]) (void)flat_runtime_task_destroy(runtime, children[i]);
        if (parent) (void)flat_runtime_task_destroy(runtime, parent);
        (void)flat_runtime_destroy(runtime);
    }
    return rc;
}

int main(int argc, char **argv) {
    FlatBytecode bc;
    unsigned i;
    if (argc != 2) return 2;
    memset(&bc, 0, sizeof bc);
    if (flat_fb_load_file(argv[1], &bc) != 0) return 3;
    for (i = 0; i < RUNS; ++i) {
        if (run_once(&bc, i) != 0) {
            flat_fb_free(&bc);
            return 1;
        }
    }
    flat_fb_free(&bc);
    puts("worker-task-wait-channel-ok runs=20 workers=4 children=2 channel-contention=serialized wait=completed transaction-suspend=rejected");
    return 0;
}
