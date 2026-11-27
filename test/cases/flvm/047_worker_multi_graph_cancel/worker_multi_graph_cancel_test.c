#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define ROUNDS 5
#define GRAPHS 8
#define NODES_PER_GRAPH 4
#define TASK_COUNT (GRAPHS * NODES_PER_GRAPH)
#define WORKERS 4
#define WAIT_LIMIT 10000

typedef struct {
    FlatRuntime *runtime;
    FlatTaskId task_id;
    pthread_mutex_t *gate_mutex;
    pthread_cond_t *gate_cond;
    unsigned *ready_count;
    int *released;
    int result;
} CancelProbe;

static void pause_one_ms(void) {
    struct timespec ts = {0, 1000000L};
    (void)nanosleep(&ts, NULL);
}

static int wait_stopped(FlatRuntime *runtime) {
    unsigned i;
    for (i = 0; i < WAIT_LIMIT; ++i) {
        if (flat_runtime_shutdown_status(runtime) == FLAT_RUNTIME_SHUTDOWN_COMPLETE)
            return 0;
        pause_one_ms();
    }
    return -1;
}

static void *cancel_main(void *opaque) {
    CancelProbe *probe = (CancelProbe *)opaque;
    pthread_mutex_lock(probe->gate_mutex);
    ++*probe->ready_count;
    pthread_cond_broadcast(probe->gate_cond);
    while (!*probe->released)
        pthread_cond_wait(probe->gate_cond, probe->gate_mutex);
    pthread_mutex_unlock(probe->gate_mutex);
    probe->result = flat_runtime_task_cancel_request(probe->runtime, probe->task_id);
    return NULL;
}

static int run_round(const FlatBytecode *bc, unsigned round) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    FlatTaskId ids[TASK_COUNT];
    pthread_t cancellers[GRAPHS];
    CancelProbe probes[GRAPHS];
    pthread_mutex_t gate_mutex;
    pthread_cond_t gate_cond;
    unsigned ready_count = 0;
    int released = 0;
    unsigned created = 0;
    unsigned i;
    int gate_mutex_ready = 0;
    int gate_cond_ready = 0;
    int rc = -1;

    memset(ids, 0, sizeof ids);
    if (!runtime) return -1;
    if (pthread_mutex_init(&gate_mutex, NULL) != 0) goto done;
    gate_mutex_ready = 1;
    if (pthread_cond_init(&gate_cond, NULL) != 0) goto done;
    gate_cond_ready = 1;

    /* Construct eight independent A->B->C->D wait graphs. */
    for (i = 0; i < TASK_COUNT; ++i) {
        if (flat_runtime_task_create(runtime, bc, NULL, &ids[i]) != 0 ||
            flat_runtime_task_start(runtime, ids[i]) != 0) {
            fprintf(stderr, "round %u: task setup failed at %u\n", round, i);
            goto done;
        }
    }
    for (i = 0; i < GRAPHS; ++i) {
        FlatTaskId *g = &ids[i * NODES_PER_GRAPH];
        if (flat_runtime_task_wait(runtime, g[0], g[1]) != 0 ||
            flat_runtime_task_wait(runtime, g[1], g[2]) != 0 ||
            flat_runtime_task_wait(runtime, g[2], g[3]) != 0) {
            fprintf(stderr, "round %u: wait graph setup failed at graph %u\n", round, i);
            goto done;
        }
    }

    /* Release eight independent cancellation requests at one barrier. */
    for (i = 0; i < GRAPHS; ++i) {
        probes[i].runtime = runtime;
        probes[i].task_id = ids[i * NODES_PER_GRAPH + 1];
        probes[i].gate_mutex = &gate_mutex;
        probes[i].gate_cond = &gate_cond;
        probes[i].ready_count = &ready_count;
        probes[i].released = &released;
        probes[i].result = -99;
        if (pthread_create(&cancellers[i], NULL, cancel_main, &probes[i]) != 0) {
            fprintf(stderr, "round %u: cancellation thread creation failed at %u\n", round, i);
            goto release_and_join;
        }
        ++created;
    }
    pthread_mutex_lock(&gate_mutex);
    while (ready_count != created)
        pthread_cond_wait(&gate_cond, &gate_mutex);
    if (created != GRAPHS) {
        pthread_mutex_unlock(&gate_mutex);
        goto release_and_join;
    }
    released = 1;
    pthread_cond_broadcast(&gate_cond);
    pthread_mutex_unlock(&gate_mutex);

    for (i = 0; i < created; ++i) pthread_join(cancellers[i], NULL);
    created = 0;
    for (i = 0; i < GRAPHS; ++i) {
        if (probes[i].result != 0) {
            fprintf(stderr, "round %u: cancel request %u returned %d\n", round, i, probes[i].result);
            goto done;
        }
    }

    if (flat_runtime_workers_start(runtime, WORKERS, 1) != 0 ||
        flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_DRAIN) != 0 ||
        wait_stopped(runtime) != 0) {
        fprintf(stderr, "round %u: worker DRAIN failed status=%d lifecycle=%d\n",
                round, (int)flat_runtime_shutdown_status(runtime),
                (int)flat_runtime_lifecycle(runtime));
        goto done;
    }

    for (i = 0; i < GRAPHS; ++i) {
        FlatTaskId *g = &ids[i * NODES_PER_GRAPH];
        if (flat_runtime_task_state(runtime, g[0]) != FLAT_TASK_COMPLETED ||
            flat_runtime_task_state(runtime, g[1]) != FLAT_TASK_CANCELLED ||
            flat_runtime_task_state(runtime, g[2]) != FLAT_TASK_COMPLETED ||
            flat_runtime_task_state(runtime, g[3]) != FLAT_TASK_COMPLETED) {
            fprintf(stderr, "round %u: graph %u terminal states unexpected (%d,%d,%d,%d)\n",
                    round, i,
                    (int)flat_runtime_task_state(runtime, g[0]),
                    (int)flat_runtime_task_state(runtime, g[1]),
                    (int)flat_runtime_task_state(runtime, g[2]),
                    (int)flat_runtime_task_state(runtime, g[3]));
            goto done;
        }
    }
    if (flat_runtime_workers_join(runtime) != 0) {
        fprintf(stderr, "round %u: worker join failed\n", round);
        goto done;
    }
    for (i = 0; i < TASK_COUNT; ++i) {
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
    goto done;

release_and_join:
    pthread_mutex_lock(&gate_mutex);
    released = 1;
    pthread_cond_broadcast(&gate_cond);
    pthread_mutex_unlock(&gate_mutex);
    for (i = 0; i < created; ++i) pthread_join(cancellers[i], NULL);
    created = 0;

done:
    if (runtime) {
        (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        if (flat_runtime_lifecycle(runtime) == FLAT_RUNTIME_RUNNING) {
            (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        }
        if (flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE) {
            /* If no workers were started, cooperative scheduling completes the
             * request-only cancellations and remaining runnable tasks. */
            unsigned spins;
            for (spins = 0; spins < WAIT_LIMIT &&
                 flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++spins) {
                (void)flat_runtime_schedule(runtime, 64, 1, NULL);
                pause_one_ms();
            }
        }
        if (flat_runtime_lifecycle(runtime) == FLAT_RUNTIME_STOPPED)
            (void)flat_runtime_workers_join(runtime);
        for (i = 0; i < TASK_COUNT; ++i)
            if (ids[i]) (void)flat_runtime_task_destroy(runtime, ids[i]);
        (void)flat_runtime_destroy(runtime);
    }
    if (gate_cond_ready) pthread_cond_destroy(&gate_cond);
    if (gate_mutex_ready) pthread_mutex_destroy(&gate_mutex);
    return rc;
}

int main(int argc, char **argv) {
    FlatBytecode bc;
    unsigned round;
    if (argc != 2) return 2;
    memset(&bc, 0, sizeof bc);
    if (flat_fb_load_file(argv[1], &bc) != 0) return 2;
    for (round = 0; round < ROUNDS; ++round) {
        if (run_round(&bc, round) != 0) {
            flat_fb_free(&bc);
            return 1;
        }
    }
    flat_fb_free(&bc);
    puts("worker-multi-graph-cancel-ok rounds=5 graphs=8 tasks=32 simultaneous-cancel=8 drain=joined cleanup=complete");
    return 0;
}
