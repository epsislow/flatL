#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define GRAPHS 4
#define NODES 10
#define TASKS (GRAPHS * NODES)
#define WORKERS 8
#define WAIT_LIMIT 20000

enum {
    NODE_A = 0, NODE_B, NODE_C, NODE_D, NODE_E,
    NODE_F, NODE_G, NODE_X, NODE_Y, NODE_Z
};

static void pause_one_ms(void) {
    struct timespec ts = {0, 1000000L};
    (void)nanosleep(&ts, NULL);
}

static FlatTaskId task_id(FlatTaskId ids[TASKS], unsigned graph, unsigned node) {
    return ids[graph * NODES + node];
}

static int wait_state(FlatRuntime *runtime, FlatTaskId id, FlatTaskState expected,
                      const char *label) {
    unsigned i;
    for (i = 0; i < WAIT_LIMIT; ++i) {
        FlatTaskState state = flat_runtime_task_state(runtime, id);
        if (state == expected) return 0;
        if (state == FLAT_TASK_COMPLETED || state == FLAT_TASK_FAILED ||
            state == FLAT_TASK_CANCELLED) {
            fprintf(stderr, "%s reached terminal state %d before expected state %d\n",
                    label, (int)state, (int)expected);
            return -1;
        }
        pause_one_ms();
    }
    fprintf(stderr, "%s did not reach state %d (last=%d)\n", label,
            (int)expected, (int)flat_runtime_task_state(runtime, id));
    return -1;
}

static int wait_shutdown(FlatRuntime *runtime) {
    unsigned i;
    for (i = 0; i < WAIT_LIMIT; ++i) {
        if (flat_runtime_shutdown_status(runtime) == FLAT_RUNTIME_SHUTDOWN_COMPLETE)
            return 0;
        pause_one_ms();
    }
    return -1;
}

static int request_cancel(FlatRuntime *runtime, FlatTaskId id, const char *label) {
    int rc = flat_runtime_task_cancel_request(runtime, id);
    if (rc != 0) {
        fprintf(stderr, "%s cancel request returned %d\n", label, rc);
        return -1;
    }
    return 0;
}

static int run_round(const FlatBytecode *bc, unsigned round) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    FlatTaskId ids[TASKS];
    unsigned i, j;
    int workers_started = 0;
    int rc = -1;

    memset(ids, 0, sizeof ids);
    if (!runtime) return -1;

    /* Deep fan-out into two intermediate nodes, converging on one sink.
     * Every waiter has one outgoing edge; fan-in is represented by several
     * waiters sharing X or Y, while X and Y both depend on Z. */
    for (i = 0; i < GRAPHS; ++i) {
        for (j = 0; j < NODES; ++j) {
            FlatTaskId id = 0;
            if (flat_runtime_task_create(runtime, bc, NULL, &id) != 0 ||
                flat_runtime_task_start(runtime, id) != 0) {
                fprintf(stderr, "round %u graph %u node %u create/start failed\n", round, i, j);
                if (id) ids[i * NODES + j] = id;
                goto done;
            }
            ids[i * NODES + j] = id;
        }
        {
            unsigned x_fanout = 2u + ((round + i) % 3u);
            unsigned leaf;
            for (leaf = 0; leaf < 7; ++leaf) {
                unsigned target = leaf < x_fanout ? NODE_X : NODE_Y;
                if (flat_runtime_task_wait(runtime, task_id(ids, i, leaf),
                                           task_id(ids, i, target)) != 0) {
                    fprintf(stderr, "round %u graph %u leaf %u wait setup failed\n",
                            round, i, leaf);
                    goto done;
                }
            }
            if (flat_runtime_task_wait(runtime, task_id(ids, i, NODE_X), task_id(ids, i, NODE_Z)) != 0 ||
                flat_runtime_task_wait(runtime, task_id(ids, i, NODE_Y), task_id(ids, i, NODE_Z)) != 0) {
                fprintf(stderr, "round %u graph %u intermediate wait setup failed\n", round, i);
                goto done;
            }
        }
    }

    if (flat_runtime_workers_start(runtime, WORKERS, 1) != 0) {
        fprintf(stderr, "round %u worker startup failed\n", round);
        goto done;
    }
    workers_started = 1;

    fprintf(stderr, "round %u: sinks-started\n", round);
    /* Ensure each sink is actually executing before cancellation begins. */
    for (i = 0; i < GRAPHS; ++i) {
        char label[64];
        (void)snprintf(label, sizeof label, "round %u sink %u", round, i);
        if (wait_state(runtime, task_id(ids, i, NODE_Z), FLAT_TASK_RUNNING, label) != 0)
            goto done;
    }

    fprintf(stderr, "round %u: sinks-running\n", round);
    /* Phase 1: cancel a running sink; its two intermediate dependents wake. */
    if (request_cancel(runtime, task_id(ids, 0, NODE_Z), "graph0/Z") != 0 ||
        wait_state(runtime, task_id(ids, 0, NODE_X), FLAT_TASK_RUNNING, "graph0/X") != 0 ||
        request_cancel(runtime, task_id(ids, 0, NODE_X), "graph0/X") != 0 ||
        wait_state(runtime, task_id(ids, 0, NODE_A), FLAT_TASK_RUNNING, "graph0/A") != 0 ||
        request_cancel(runtime, task_id(ids, 0, NODE_A), "graph0/A") != 0)
        goto done;

    fprintf(stderr, "round %u: phase1-done\n", round);
    /* Phase 2: cancel an intermediate node while it is WAITING on its sink. */
    {
        unsigned x_fanout = 2u + ((round + 1u) % 3u);
        unsigned first_y_leaf = x_fanout;
        FlatTaskId y_leaf = task_id(ids, 1, first_y_leaf);
        if (flat_runtime_task_state(runtime, task_id(ids, 1, NODE_Y)) != FLAT_TASK_WAITING ||
            request_cancel(runtime, task_id(ids, 1, NODE_Y), "graph1/Y-waiting") != 0 ||
            wait_state(runtime, y_leaf, FLAT_TASK_RUNNING, "graph1/Y-leaf") != 0 ||
            request_cancel(runtime, y_leaf, "graph1/Y-leaf-running") != 0)
            goto done;
    }

    fprintf(stderr, "round %u: phase2-done\n", round);
    /* Phase 3: a later sink cancellation wakes a second fan-out branch. */
    if (request_cancel(runtime, task_id(ids, 2, NODE_Z), "graph2/Z") != 0 ||
        wait_state(runtime, task_id(ids, 2, NODE_Y), FLAT_TASK_RUNNING, "graph2/Y") != 0 ||
        request_cancel(runtime, task_id(ids, 2, NODE_Y), "graph2/Y-running") != 0)
        goto done;

    fprintf(stderr, "round %u: phase3-done\n", round);
    /* Graph 3 is deliberately left untouched and must drain normally. */
    if (flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_DRAIN) != 0 ||
        wait_shutdown(runtime) != 0) {
        fprintf(stderr, "round %u DRAIN did not complete (status=%d lifecycle=%d)\n",
                round, (int)flat_runtime_shutdown_status(runtime),
                (int)flat_runtime_lifecycle(runtime));
        goto done;
    }

    fprintf(stderr, "round %u: drain-complete\n", round);
    for (i = 0; i < GRAPHS; ++i) {
        for (j = 0; j < NODES; ++j) {
            FlatTaskState expected = FLAT_TASK_COMPLETED;
            if ((i == 0 && (j == NODE_Z || j == NODE_X || j == NODE_A)) ||
                (i == 1 && (j == NODE_Y || j == (2u + ((round + 1u) % 3u)))) ||
                (i == 2 && (j == NODE_Z || j == NODE_Y)))
                expected = FLAT_TASK_CANCELLED;
            if (flat_runtime_task_state(runtime, task_id(ids, i, j)) != expected) {
                fprintf(stderr, "round %u graph %u node %u state=%d expected=%d\n",
                        round, i, j,
                        (int)flat_runtime_task_state(runtime, task_id(ids, i, j)),
                        (int)expected);
                goto done;
            }
        }
    }

    fprintf(stderr, "round %u: states-verified\n", round);
    if (flat_runtime_workers_join(runtime) != 0) {
        fprintf(stderr, "round %u workers join failed\n", round);
        goto done;
    }
    workers_started = 0;
    for (i = 0; i < TASKS; ++i) {
        if (flat_runtime_task_destroy(runtime, ids[i]) != 0) {
            fprintf(stderr, "round %u task destroy failed at %u\n", round, i);
            goto done;
        }
        ids[i] = 0;
    }
    if (flat_runtime_destroy(runtime) != 0) {
        runtime = NULL;
        fprintf(stderr, "round %u runtime destroy failed\n", round);
        return -1;
    }
    runtime = NULL;
    rc = 0;

done:
    if (runtime) {
        (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        if (workers_started) {
            unsigned spins;
            for (spins = 0; spins < WAIT_LIMIT &&
                 flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++spins)
                pause_one_ms();
            if (flat_runtime_lifecycle(runtime) == FLAT_RUNTIME_STOPPED &&
                flat_runtime_workers_join(runtime) == 0)
                workers_started = 0;
        } else {
            unsigned spins;
            for (spins = 0; spins < WAIT_LIMIT &&
                 flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++spins) {
                (void)flat_runtime_schedule(runtime, 64, 1, NULL);
                pause_one_ms();
            }
        }
        for (i = 0; i < TASKS; ++i)
            if (ids[i]) (void)flat_runtime_task_destroy(runtime, ids[i]);
        (void)flat_runtime_destroy(runtime);
    }
    return rc;
}

int main(int argc, char **argv) {
    FlatBytecode bc;
    unsigned round;
    if (argc != 2) return 2;
    memset(&bc, 0, sizeof bc);
    if (flat_fb_load_file(argv[1], &bc) != 0) return 2;
    for (round = 0; round < 3; ++round)
        if (run_round(&bc, round) != 0) return 1;
    puts("worker-dag-staged-cancel-ok rounds=3 graphs=4 tasks=40 fanout=7 staged-cancel=running-waiting drain=complete cleanup=complete");
    return 0;
}
