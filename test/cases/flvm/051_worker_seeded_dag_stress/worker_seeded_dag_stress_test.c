#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define NODES 16u
#define CANCEL_COUNT 4u
#define WORKERS 6u
#define SEEDS 6u
#define REPEATS 2u
#define WAIT_LIMIT 20000u

typedef struct {
    FlatRuntime *runtime;
    FlatTaskId task_id;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
    unsigned *ready;
    int *released;
    int result;
} CancelProbe;

static void pause_one_ms(void) {
    struct timespec ts = {0, 1000000L};
    (void)nanosleep(&ts, NULL);
}

static unsigned next_random(unsigned *state) {
    unsigned x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x ? x : 0x9e3779b9u;
    return *state;
}

static void *cancel_main(void *opaque) {
    CancelProbe *probe = (CancelProbe *)opaque;
    pthread_mutex_lock(probe->mutex);
    ++*probe->ready;
    pthread_cond_broadcast(probe->cond);
    while (!*probe->released)
        pthread_cond_wait(probe->cond, probe->mutex);
    pthread_mutex_unlock(probe->mutex);
    probe->result = flat_runtime_task_cancel_request(probe->runtime, probe->task_id);
    return NULL;
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

static unsigned make_topology(unsigned seed, int targets[NODES], unsigned cancelled[CANCEL_COUNT]) {
    unsigned state = seed ? seed : 0x6d2b79f5u;
    unsigned i, edges = 0;
    unsigned char chosen[NODES];
    unsigned hash = 2166136261u;
    memset(chosen, 0, sizeof chosen);
    for (i = 0; i < NODES; ++i) targets[i] = -1;

    /* Every edge points to a higher node index, so the generated graph is
     * necessarily acyclic. Each task has at most one outgoing wait edge,
     * while arbitrary fan-in and disconnected components are possible. */
    for (i = 0; i + 1u < NODES; ++i) {
        unsigned roll = next_random(&state) % 100u;
        if (roll < 76u) {
            unsigned span = NODES - i - 1u;
            targets[i] = (int)(i + 1u + next_random(&state) % span);
            ++edges;
        }
        hash ^= (unsigned)(targets[i] + 2);
        hash *= 16777619u;
    }
    hash ^= edges;
    hash *= 16777619u;

    for (i = 0; i < CANCEL_COUNT; ++i) {
        unsigned node;
        do {
            node = next_random(&state) % NODES;
        } while (chosen[node]);
        chosen[node] = 1;
        cancelled[i] = node;
        hash ^= node + 1u;
        hash *= 16777619u;
    }
    return hash;
}

static int run_case(const FlatBytecode *bc, unsigned seed, unsigned *signature) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    FlatTaskId ids[NODES];
    int targets[NODES];
    unsigned cancelled[CANCEL_COUNT];
    pthread_t threads[CANCEL_COUNT];
    CancelProbe probes[CANCEL_COUNT];
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned ready = 0, created = 0, i;
    int released = 0, mutex_ready = 0, cond_ready = 0;
    int rc = -1;

    memset(ids, 0, sizeof ids);
    if (!runtime) return -1;
    *signature = make_topology(seed, targets, cancelled);
    if (pthread_mutex_init(&mutex, NULL) != 0) goto done;
    mutex_ready = 1;
    if (pthread_cond_init(&cond, NULL) != 0) goto done;
    cond_ready = 1;

    for (i = 0; i < NODES; ++i) {
        if (flat_runtime_task_create(runtime, bc, NULL, &ids[i]) != 0 ||
            flat_runtime_task_start(runtime, ids[i]) != 0) {
            fprintf(stderr, "seed %08x: task setup failed at node %u\n", seed, i);
            goto done;
        }
    }
    for (i = 0; i < NODES; ++i) {
        if (targets[i] >= 0 &&
            flat_runtime_task_wait(runtime, ids[i], ids[(unsigned)targets[i]]) != 0) {
            fprintf(stderr, "seed %08x: wait edge %u -> %d rejected\n", seed, i, targets[i]);
            goto done;
        }
    }

    /* Publish four unique cancellation requests concurrently while all
     * tasks are RUNNABLE and before workers begin executing the DAG. */
    for (i = 0; i < CANCEL_COUNT; ++i) {
        probes[i].runtime = runtime;
        probes[i].task_id = ids[cancelled[i]];
        probes[i].mutex = &mutex;
        probes[i].cond = &cond;
        probes[i].ready = &ready;
        probes[i].released = &released;
        probes[i].result = -99;
        if (pthread_create(&threads[i], NULL, cancel_main, &probes[i]) != 0) {
            fprintf(stderr, "seed %08x: cancel thread creation failed at %u\n", seed, i);
            goto release_and_join;
        }
        ++created;
    }
    pthread_mutex_lock(&mutex);
    while (ready != created) pthread_cond_wait(&cond, &mutex);
    if (created != CANCEL_COUNT) {
        pthread_mutex_unlock(&mutex);
        goto release_and_join;
    }
    released = 1;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mutex);
    for (i = 0; i < created; ++i) (void)pthread_join(threads[i], NULL);
    created = 0;
    for (i = 0; i < CANCEL_COUNT; ++i) {
        if (probes[i].result != 0) {
            fprintf(stderr, "seed %08x: cancel node %u returned %d\n",
                    seed, cancelled[i], probes[i].result);
            goto done;
        }
    }

    if (flat_runtime_workers_start(runtime, WORKERS, 1) != 0 ||
        flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_DRAIN) != 0 ||
        wait_shutdown(runtime) != 0) {
        fprintf(stderr, "seed %08x: DRAIN failed status=%d lifecycle=%d\n",
                seed, (int)flat_runtime_shutdown_status(runtime),
                (int)flat_runtime_lifecycle(runtime));
        goto done;
    }

    for (i = 0; i < NODES; ++i) {
        unsigned j;
        int was_cancelled = 0;
        FlatTaskState expected, actual;
        for (j = 0; j < CANCEL_COUNT; ++j)
            if (cancelled[j] == i) was_cancelled = 1;
        expected = was_cancelled ? FLAT_TASK_CANCELLED : FLAT_TASK_COMPLETED;
        actual = flat_runtime_task_state(runtime, ids[i]);
        if (actual != expected) {
            fprintf(stderr, "seed %08x: node %u state=%d expected=%d target=%d\n",
                    seed, i, (int)actual, (int)expected, targets[i]);
            goto done;
        }
    }

    if (flat_runtime_workers_join(runtime) != 0) {
        fprintf(stderr, "seed %08x: worker join failed\n", seed);
        goto done;
    }
    for (i = 0; i < NODES; ++i) {
        if (flat_runtime_task_destroy(runtime, ids[i]) != 0) {
            fprintf(stderr, "seed %08x: task destroy failed at %u\n", seed, i);
            goto done;
        }
        ids[i] = 0;
    }
    if (flat_runtime_destroy(runtime) != 0) {
        fprintf(stderr, "seed %08x: runtime destroy failed\n", seed);
        runtime = NULL;
        goto done;
    }
    runtime = NULL;
    rc = 0;
    goto done;

release_and_join:
    pthread_mutex_lock(&mutex);
    released = 1;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mutex);
    for (i = 0; i < created; ++i) (void)pthread_join(threads[i], NULL);
    created = 0;

done:
    if (runtime) {
        (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        for (i = 0; i < WAIT_LIMIT &&
             flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++i) {
            (void)flat_runtime_schedule(runtime, 64, 1, NULL);
            pause_one_ms();
        }
        if (flat_runtime_lifecycle(runtime) == FLAT_RUNTIME_STOPPED)
            (void)flat_runtime_workers_join(runtime);
        for (i = 0; i < NODES; ++i)
            if (ids[i]) (void)flat_runtime_task_destroy(runtime, ids[i]);
        (void)flat_runtime_destroy(runtime);
    }
    if (cond_ready) pthread_cond_destroy(&cond);
    if (mutex_ready) pthread_mutex_destroy(&mutex);
    return rc;
}

int main(int argc, char **argv) {
    FlatBytecode bc;
    unsigned seed_index, repeat;
    unsigned signatures[SEEDS];
    if (argc != 2) return 2;
    memset(&bc, 0, sizeof bc);
    if (flat_fb_load_file(argv[1], &bc) != 0) return 2;

    for (seed_index = 0; seed_index < SEEDS; ++seed_index) {
        unsigned seed = 0x13579bdfu ^ (0x9e3779b9u * (seed_index + 1u));
        for (repeat = 0; repeat < REPEATS; ++repeat) {
            unsigned signature = 0;
            if (run_case(&bc, seed, &signature) != 0) return 1;
            if (repeat == 0) signatures[seed_index] = signature;
            else if (signature != signatures[seed_index]) {
                fprintf(stderr, "seed %08x: topology was not reproducible\n", seed);
                return 1;
            }
        }
    }
    puts("worker-seeded-dag-stress-ok seeds=6 repeats=2 graphs=1 tasks=16 concurrent-cancel=4 drain=joined cleanup=complete");
    return 0;
}
