#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TASKS 24u
#define STARTED 18u
#define WORKERS 4u
#define REQUESTERS 8u
#define ROUNDS 12u
#define WAIT_LIMIT 30000u

typedef struct {
    FlatRuntime *runtime;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
    unsigned *ready;
    int *released;
    FlatRuntimeShutdownMode mode;
    int result;
} ShutdownProbe;

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

static void *shutdown_main(void *opaque) {
    ShutdownProbe *probe = (ShutdownProbe *)opaque;
    pthread_mutex_lock(probe->mutex);
    ++*probe->ready;
    pthread_cond_broadcast(probe->cond);
    while (!*probe->released)
        pthread_cond_wait(probe->cond, probe->mutex);
    pthread_mutex_unlock(probe->mutex);
    probe->result = flat_runtime_shutdown_request(probe->runtime, probe->mode);
    return NULL;
}

static int wait_complete(FlatRuntime *runtime) {
    unsigned i;
    for (i = 0; i < WAIT_LIMIT; ++i) {
        if (flat_runtime_shutdown_status(runtime) == FLAT_RUNTIME_SHUTDOWN_COMPLETE)
            return 0;
        pause_one_ms();
    }
    return -1;
}

static int run_round(const FlatBytecode *bc, unsigned seed) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    FlatTaskId ids[TASKS];
    pthread_t threads[REQUESTERS];
    ShutdownProbe probes[REQUESTERS];
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned state = seed ? seed : 0x243f6a88u;
    unsigned ready = 0, created = 0, i;
    int released = 0, mutex_ready = 0, cond_ready = 0;
    int rc = -1;

    memset(ids, 0, sizeof ids);
    if (!runtime) return -1;
    if (pthread_mutex_init(&mutex, NULL) != 0) goto done;
    mutex_ready = 1;
    if (pthread_cond_init(&cond, NULL) != 0) goto done;
    cond_ready = 1;

    for (i = 0; i < TASKS; ++i) {
        if (flat_runtime_task_create(runtime, bc, NULL, &ids[i]) != 0) {
            fprintf(stderr, "seed %08x: task create failed at %u\n", seed, i);
            goto done;
        }
        if (i < STARTED && flat_runtime_task_start(runtime, ids[i]) != 0) {
            fprintf(stderr, "seed %08x: task start failed at %u\n", seed, i);
            goto done;
        }
    }

    /* Every generated edge points forward. Force one edge into the CREATED
     * suffix so DRAIN has outstanding work and CANCEL must escalate it. */
    if (flat_runtime_task_wait(runtime, ids[0], ids[TASKS - 1u]) != 0) {
        fprintf(stderr, "seed %08x: forced wait edge rejected\n", seed);
        goto done;
    }
    for (i = 1; i + 1u < STARTED; ++i) {
        if ((next_random(&state) & 3u) == 0u) {
            unsigned span = TASKS - i - 1u;
            unsigned target = i + 1u + next_random(&state) % span;
            if (flat_runtime_task_wait(runtime, ids[i], ids[target]) != 0) {
                fprintf(stderr, "seed %08x: generated wait edge %u -> %u rejected\n",
                        seed, i, target);
                goto done;
            }
        }
    }

    if (flat_runtime_workers_start(runtime, WORKERS, 1) != 0) {
        fprintf(stderr, "seed %08x: worker startup failed\n", seed);
        goto done;
    }

    /* Release DRAIN and CANCEL callers together. At least one CANCEL request
     * is guaranteed; later DRAIN requests must never downgrade cancellation. */
    for (i = 0; i < REQUESTERS; ++i) {
        probes[i].runtime = runtime;
        probes[i].mutex = &mutex;
        probes[i].cond = &cond;
        probes[i].ready = &ready;
        probes[i].released = &released;
        probes[i].mode = i == 0u ? FLAT_RUNTIME_SHUTDOWN_DRAIN :
            (i == REQUESTERS - 1u || (next_random(&state) & 1u))
                ? FLAT_RUNTIME_SHUTDOWN_CANCEL : FLAT_RUNTIME_SHUTDOWN_DRAIN;
        probes[i].result = -99;
        if (pthread_create(&threads[i], NULL, shutdown_main, &probes[i]) != 0) {
            fprintf(stderr, "seed %08x: shutdown requester create failed at %u\n", seed, i);
            goto release_and_join;
        }
        ++created;
    }
    pthread_mutex_lock(&mutex);
    while (ready != created) pthread_cond_wait(&cond, &mutex);
    if (created != REQUESTERS) {
        pthread_mutex_unlock(&mutex);
        goto release_and_join;
    }
    released = 1;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mutex);

    for (i = 0; i < created; ++i) (void)pthread_join(threads[i], NULL);
    created = 0;
    for (i = 0; i < REQUESTERS; ++i) {
        if (probes[i].result != 0) {
            fprintf(stderr, "seed %08x: shutdown request %u mode=%d returned %d\n",
                    seed, i, (int)probes[i].mode, probes[i].result);
            goto done;
        }
    }

    if ((flat_runtime_lifecycle(runtime) != FLAT_RUNTIME_CANCELLING &&
         flat_runtime_lifecycle(runtime) != FLAT_RUNTIME_STOPPED) ||
        wait_complete(runtime) != 0) {
        fprintf(stderr, "seed %08x: escalation incomplete lifecycle=%d status=%d\n",
                seed, (int)flat_runtime_lifecycle(runtime),
                (int)flat_runtime_shutdown_status(runtime));
        goto done;
    }
    for (i = 0; i < TASKS; ++i) {
        FlatTaskState actual = flat_runtime_task_state(runtime, ids[i]);
        if (actual != FLAT_TASK_CANCELLED) {
            fprintf(stderr, "seed %08x: task %u state=%d expected CANCELLED\n",
                    seed, i, (int)actual);
            goto done;
        }
    }
    if (flat_runtime_workers_join(runtime) != 0) {
        fprintf(stderr, "seed %08x: worker join failed\n", seed);
        goto done;
    }
    for (i = 0; i < TASKS; ++i) {
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
             flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++i)
            pause_one_ms();
        if (flat_runtime_lifecycle(runtime) == FLAT_RUNTIME_STOPPED)
            (void)flat_runtime_workers_join(runtime);
        for (i = 0; i < TASKS; ++i)
            if (ids[i]) (void)flat_runtime_task_destroy(runtime, ids[i]);
        (void)flat_runtime_destroy(runtime);
    }
    if (cond_ready) pthread_cond_destroy(&cond);
    if (mutex_ready) pthread_mutex_destroy(&mutex);
    return rc;
}

int main(int argc, char **argv) {
    FlatBytecode bc;
    unsigned round;
    if (argc != 2) return 2;
    memset(&bc, 0, sizeof bc);
    if (flat_fb_load_file(argv[1], &bc) != 0) return 2;
    for (round = 0; round < ROUNDS; ++round) {
        unsigned seed = 0xa5a50001u ^ (0x9e3779b9u * (round + 1u));
        if (run_round(&bc, seed) != 0) {
            flat_fb_free(&bc);
            return 1;
        }
    }
    flat_fb_free(&bc);
    puts("worker-mixed-shutdown-soak-ok rounds=12 tasks=24 started=18 workers=4 shutdown=drain+cancel join=complete cleanup=complete");
    return 0;
}
