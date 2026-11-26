#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"
#include "flat_channel.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define WORKERS 4
#define WAIT_LIMIT 10000

static void pause_one_ms(void) {
    struct timespec ts = {0, 1000000L};
    (void)nanosleep(&ts, NULL);
}

static int load_bytecode(const char *path, FlatBytecode *bc) {
    memset(bc, 0, sizeof *bc);
    return flat_fb_load_file(path, bc);
}

static int wait_for_shutdown(FlatRuntime *runtime) {
    unsigned i;
    for (i = 0; i < WAIT_LIMIT; ++i) {
        if (flat_runtime_shutdown_status(runtime) == FLAT_RUNTIME_SHUTDOWN_COMPLETE)
            return 0;
        pause_one_ms();
    }
    return -1;
}

static int expect_state(FlatRuntime *runtime, FlatTaskId id, FlatTaskState expected,
                        const char *label) {
    FlatTaskState actual = flat_runtime_task_state(runtime, id);
    if (actual != expected) {
        fprintf(stderr, "%s: task %llu expected state %d got %d\n", label,
                (unsigned long long)id, (int)expected, (int)actual);
        return -1;
    }
    return 0;
}

static int destroy_tasks(FlatRuntime *runtime, const FlatTaskId *ids, size_t count) {
    size_t i;
    for (i = 0; i < count; ++i) {
        if (ids[i] && flat_runtime_task_destroy(runtime, ids[i]) != 0) {
            fprintf(stderr, "task destroy failed: %llu\n", (unsigned long long)ids[i]);
            return -1;
        }
    }
    return 0;
}

/* Four-node chain: A->B->C->D. A reverse edge D->A must be rejected.
 * Cancelling B must remove B's outgoing edge and wake A, while C and D
 * continue normally. DRAIN then proves no stale wait edge remains. */
static int run_wait_graph_case(const FlatBytecode *bc) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    FlatTaskId ids[4] = {0, 0, 0, 0};
    unsigned i;
    int rc = -1;
    if (!runtime) return -1;
    for (i = 0; i < 4; ++i) {
        if (flat_runtime_task_create(runtime, bc, NULL, &ids[i]) != 0 ||
            flat_runtime_task_start(runtime, ids[i]) != 0) {
            fprintf(stderr, "wait graph: task setup %u failed\n", i);
            goto done;
        }
    }
    if (flat_runtime_task_wait(runtime, ids[0], ids[1]) != 0 ||
        flat_runtime_task_wait(runtime, ids[1], ids[2]) != 0 ||
        flat_runtime_task_wait(runtime, ids[2], ids[3]) != 0 ||
        flat_runtime_task_wait(runtime, ids[3], ids[0]) == 0) {
        fprintf(stderr, "wait graph: chain or cycle validation failed\n");
        goto done;
    }
    for (i = 0; i < 3; ++i)
        if (expect_state(runtime, ids[i], FLAT_TASK_WAITING, "wait graph setup") != 0) goto done;
    if (flat_runtime_task_cancel_request(runtime, ids[1]) != 0 ||
        flat_runtime_workers_start(runtime, WORKERS, 1) != 0 ||
        flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_DRAIN) != 0 ||
        wait_for_shutdown(runtime) != 0) {
        fprintf(stderr, "wait graph: worker DRAIN failed status=%d lifecycle=%d\n",
                (int)flat_runtime_shutdown_status(runtime), (int)flat_runtime_lifecycle(runtime));
        goto done;
    }
    if (expect_state(runtime, ids[0], FLAT_TASK_COMPLETED, "dependent A") != 0 ||
        expect_state(runtime, ids[1], FLAT_TASK_CANCELLED, "cancelled B") != 0 ||
        expect_state(runtime, ids[2], FLAT_TASK_COMPLETED, "dependent C") != 0 ||
        expect_state(runtime, ids[3], FLAT_TASK_COMPLETED, "terminal D") != 0)
        goto done;
    if (flat_runtime_workers_join(runtime) != 0 ||
        destroy_tasks(runtime, ids, 4) != 0 || flat_runtime_destroy(runtime) != 0) {
        runtime = NULL;
        fprintf(stderr, "wait graph: join or teardown failed\n");
        return -1;
    }
    runtime = NULL;
    rc = 0;
done:
    if (runtime) {
        (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        if (wait_for_shutdown(runtime) == 0 &&
            flat_runtime_lifecycle(runtime) == FLAT_RUNTIME_STOPPED)
            (void)flat_runtime_workers_join(runtime);
        for (i = 0; i < 4; ++i) if (ids[i])
            (void)flat_runtime_task_destroy(runtime, ids[i]);
        (void)flat_runtime_destroy(runtime);
    }
    return rc;
}

/* A Channel waiter is first parked cooperatively while its owner retains the
 * transaction. Request-only cancellation is then published before workers
 * start; CANCEL must clean both tasks, abort the transaction and release the
 * waiter's Channel reference before Channel/runtime destruction. */
static int run_channel_waiter_case(const FlatBytecode *owner_bc,
                                   const FlatBytecode *waiter_bc) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    FlatTaskId owner = 0, waiter = 0;
    FlatChannelId channel = 0;
    uint64_t turns = 0;
    unsigned i;
    int rc = -1;
    if (!runtime) return -1;
    if (flat_runtime_channel_create(runtime, &channel) != 0 || channel != 1 ||
        flat_runtime_task_create(runtime, owner_bc, NULL, &owner) != 0 ||
        flat_runtime_channel_lock(runtime, channel, owner) != 0 ||
        flat_runtime_task_create(runtime, waiter_bc, NULL, &waiter) != 0 ||
        flat_runtime_task_start(runtime, waiter) != 0) {
        fprintf(stderr, "Channel waiter: setup failed\n");
        goto done;
    }
    for (i = 0; i < 16 && flat_runtime_task_state(runtime, waiter) != FLAT_TASK_WAITING; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) {
            fprintf(stderr, "Channel waiter: could not enter WAITING\n");
            goto done;
        }
    }
    if (expect_state(runtime, waiter, FLAT_TASK_WAITING, "Channel waiter") != 0 ||
        flat_runtime_task_cancel_request(runtime, waiter) != 0 ||
        flat_runtime_workers_start(runtime, 2, 1) != 0 ||
        flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL) != 0 ||
        wait_for_shutdown(runtime) != 0) {
        fprintf(stderr, "Channel waiter: CANCEL/join preparation failed status=%d lifecycle=%d\n",
                (int)flat_runtime_shutdown_status(runtime), (int)flat_runtime_lifecycle(runtime));
        goto done;
    }
    if (expect_state(runtime, owner, FLAT_TASK_CANCELLED, "Channel owner") != 0 ||
        expect_state(runtime, waiter, FLAT_TASK_CANCELLED, "Channel waiter cancelled") != 0 ||
        flat_runtime_workers_join(runtime) != 0 ||
        flat_runtime_task_destroy(runtime, waiter) != 0 ||
        flat_runtime_task_destroy(runtime, owner) != 0 ||
        flat_runtime_channel_destroy(runtime, channel) != 0 ||
        flat_runtime_destroy(runtime) != 0) {
        runtime = NULL;
        fprintf(stderr, "Channel waiter: cleanup/join failed\n");
        return -1;
    }
    runtime = NULL;
    rc = 0;
done:
    if (runtime) {
        (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        if (wait_for_shutdown(runtime) == 0 &&
            flat_runtime_lifecycle(runtime) == FLAT_RUNTIME_STOPPED)
            (void)flat_runtime_workers_join(runtime);
        if (waiter) (void)flat_runtime_task_destroy(runtime, waiter);
        if (owner) (void)flat_runtime_task_destroy(runtime, owner);
        if (channel) (void)flat_runtime_channel_destroy(runtime, channel);
        (void)flat_runtime_destroy(runtime);
    }
    return rc;
}

int main(int argc, char **argv) {
    FlatBytecode simple_bc, waiter_bc;
    int rc = 1;
    memset(&simple_bc, 0, sizeof simple_bc);
    memset(&waiter_bc, 0, sizeof waiter_bc);
    if (argc != 3 || load_bytecode(argv[1], &simple_bc) != 0 ||
        load_bytecode(argv[2], &waiter_bc) != 0) return 2;
    if (run_wait_graph_case(&simple_bc) != 0) goto done;
    if (run_channel_waiter_case(&simple_bc, &waiter_bc) != 0) goto done;
    rc = 0;
done:
    flat_fb_free(&waiter_bc);
    flat_fb_free(&simple_bc);
    if (rc == 0)
        puts("worker-wait-graph-cancel-ok chain=4 cycle=rejected cancelled-middle=woke-dependent channel-waiter=cancelled refs=released drain=joined cancel=joined");
    else
        fprintf(stderr, "worker wait graph cancellation test failed\n");
    return rc;
}
