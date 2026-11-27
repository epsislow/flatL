#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define TASKS 8u
#define FAIL_TASKS 2u
#define WORKERS 4u
#define ROUNDS 100u
#define WAIT_LIMIT 30000u

static void pause_one_ms(void) {
    struct timespec ts = {0, 1000000L};
    (void)nanosleep(&ts, NULL);
}

static int wait_complete(FlatRuntime *runtime, unsigned round) {
    unsigned i;
    for (i = 0; i < WAIT_LIMIT; ++i) {
        if (flat_runtime_shutdown_status(runtime) == FLAT_RUNTIME_SHUTDOWN_COMPLETE)
            return 0;
        pause_one_ms();
    }
    fprintf(stderr, "round %u: shutdown timeout lifecycle=%d status=%d\n",
            round, (int)flat_runtime_lifecycle(runtime),
            (int)flat_runtime_shutdown_status(runtime));
    return -1;
}

static int run_round(const FlatBytecode *ok_bc, const FlatBytecode *fail_bc,
                     unsigned round) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    FlatTaskId ids[TASKS];
    unsigned i;
    int rc = -1;

    memset(ids, 0, sizeof ids);
    if (!runtime) return -1;

    for (i = 0; i < TASKS; ++i) {
        const FlatBytecode *bc = i < FAIL_TASKS ? fail_bc : ok_bc;
        if (flat_runtime_task_create(runtime, bc, NULL, &ids[i]) != 0 ||
            flat_runtime_task_start(runtime, ids[i]) != 0) {
            fprintf(stderr, "round %u: task setup failed at %u\n", round, i);
            goto done;
        }
    }

    if (flat_runtime_workers_start(runtime, WORKERS, 1) != 0) {
        fprintf(stderr, "round %u: worker startup failed\n", round);
        goto done;
    }
    if (flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_DRAIN) != 0) {
        fprintf(stderr, "round %u: DRAIN request failed\n", round);
        goto done;
    }
    if (wait_complete(runtime, round) != 0) goto done;

    for (i = 0; i < TASKS; ++i) {
        FlatTaskState expected = i < FAIL_TASKS ? FLAT_TASK_FAILED : FLAT_TASK_COMPLETED;
        FlatTaskState actual = flat_runtime_task_state(runtime, ids[i]);
        FlatVmResult result;
        if (actual != expected) {
            fprintf(stderr, "round %u: task %u state=%d expected=%d\n",
                    round, i, (int)actual, (int)expected);
            goto done;
        }
        if (flat_runtime_task_result(runtime, ids[i], &result) != 0 ||
            (i < FAIL_TASKS &&
             (result.state != FLAT_VM_FAILED || result.termination != FLAT_TERM_FAIL)) ||
            (i >= FAIL_TASKS && result.state != FLAT_VM_COMPLETED)) {
            fprintf(stderr, "round %u: task %u terminal result mismatch\n", round, i);
            goto done;
        }
    }

    if (flat_runtime_workers_join(runtime) != 0) {
        fprintf(stderr, "round %u: worker join failed\n", round);
        goto done;
    }
    for (i = 0; i < TASKS; ++i) {
        if (flat_runtime_task_destroy(runtime, ids[i]) != 0) {
            fprintf(stderr, "round %u: task destroy failed at %u\n", round, i);
            goto done;
        }
        ids[i] = 0;
    }
    if (flat_runtime_destroy(runtime) != 0) {
        fprintf(stderr, "round %u: runtime destroy failed\n", round);
        runtime = NULL;
        goto done;
    }
    runtime = NULL;
    rc = 0;

done:
    if (runtime) {
        (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        (void)wait_complete(runtime, round);
        if (flat_runtime_lifecycle(runtime) == FLAT_RUNTIME_STOPPED)
            (void)flat_runtime_workers_join(runtime);
        for (i = 0; i < TASKS; ++i)
            if (ids[i]) (void)flat_runtime_task_destroy(runtime, ids[i]);
        (void)flat_runtime_destroy(runtime);
    }
    return rc;
}

int main(int argc, char **argv) {
    FlatBytecode ok_bc, fail_bc;
    unsigned round;
    if (argc != 3) return 2;
    memset(&ok_bc, 0, sizeof ok_bc);
    memset(&fail_bc, 0, sizeof fail_bc);
    if (flat_fb_load_file(argv[1], &ok_bc) != 0) return 3;
    if (flat_fb_load_file(argv[2], &fail_bc) != 0) {
        flat_fb_free(&ok_bc);
        return 4;
    }
    for (round = 0; round < ROUNDS; ++round) {
        if (run_round(&ok_bc, &fail_bc, round) != 0) {
            flat_fb_free(&fail_bc);
            flat_fb_free(&ok_bc);
            return 1;
        }
    }
    flat_fb_free(&fail_bc);
    flat_fb_free(&ok_bc);
    puts("worker-failure-isolation-soak-ok rounds=100 tasks_per_round=8 failed=2 completed=6 workers=4 drain=complete join=complete cleanup=complete");
    return 0;
}
