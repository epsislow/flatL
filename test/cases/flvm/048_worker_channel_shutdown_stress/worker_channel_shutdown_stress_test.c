#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"
#include "flat_channel.h"
#include "flat_value.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHANNELS 4
#define WRITERS_PER_CHANNEL 8
#define WORKERS 4
#define WAIT_LIMIT 15000

typedef struct CancelProbe {
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

static int wait_shutdown(FlatRuntime *runtime, FlatRuntimeShutdownStatus expected) {
    unsigned i;
    for (i = 0; i < WAIT_LIMIT; ++i) {
        FlatRuntimeShutdownStatus status = flat_runtime_shutdown_status(runtime);
        if (status == expected) return 0;
        pause_one_ms();
    }
    return -1;
}

static int load_bc(const char *path, FlatBytecode *bc) {
    memset(bc, 0, sizeof *bc);
    return flat_fb_load_file(path, bc);
}

static int set_number(FlatRuntime *runtime, FlatChannelId channel,
                      const char *key, double number) {
    FlatCollectionPool *coll = flat_coll_create();
    FlatStringPool *pool = flat_pool_create();
    FlatValue value;
    int rc = -1;
    memset(&value, 0, sizeof value);
    if (!coll || !pool) goto done;
    flat_value_set_number(&value, number);
    rc = flat_runtime_channel_set(runtime, channel, 0, key, &value, coll, pool, NULL);
done:
    flat_coll_destroy(coll);
    flat_pool_destroy(pool);
    return rc;
}

static int get_number(FlatRuntime *runtime, FlatChannelId channel,
                      const char *key, double *out_number) {
    FlatCollectionPool *coll = flat_coll_create();
    FlatStringPool *pool = flat_pool_create();
    FlatValue value;
    int rc = -1;
    memset(&value, 0, sizeof value);
    if (!coll || !pool) goto done;
    if (flat_runtime_channel_var(runtime, channel, 0, key, coll, pool, NULL, &value) != 0 ||
        value.tag != VAL_NUMBER) goto done;
    *out_number = value.u.number.payload.f64;
    rc = 0;
done:
    flat_value_release(coll, pool, &value);
    flat_coll_destroy(coll);
    flat_pool_destroy(pool);
    return rc;
}

static int run_multi_channel_drain(FlatBytecode writer_bc[CHANNELS]) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    FlatChannelId channels[CHANNELS] = {0};
    FlatTaskId tasks[CHANNELS * WRITERS_PER_CHANNEL] = {0};
    unsigned created = 0, i, j;
    int pool_started = 0;
    int rc = -1;
    if (!runtime) return -1;

    for (i = 0; i < CHANNELS; ++i) {
        if (flat_runtime_channel_create(runtime, &channels[i]) != 0 ||
            set_number(runtime, channels[i], "counter", 0) != 0) {
            fprintf(stderr, "multi-channel DRAIN: channel setup %u failed\n", i);
            goto done;
        }
    }
    for (i = 0; i < CHANNELS; ++i) {
        for (j = 0; j < WRITERS_PER_CHANNEL; ++j) {
            unsigned index = i * WRITERS_PER_CHANNEL + j;
            if (flat_runtime_task_create(runtime, &writer_bc[i], NULL, &tasks[index]) != 0) {
                fprintf(stderr, "multi-channel DRAIN: task create channel=%u writer=%u failed\n", i, j);
                goto done;
            }
            ++created;
            if (flat_runtime_task_start(runtime, tasks[index]) != 0) {
                fprintf(stderr, "multi-channel DRAIN: task start channel=%u writer=%u failed\n", i, j);
                goto done;
            }
        }
    }
    if (flat_runtime_workers_start(runtime, WORKERS, 1) != 0) {
        fprintf(stderr, "multi-channel DRAIN: worker startup failed\n");
        goto done;
    }
    pool_started = 1;
    if (flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_DRAIN) != 0 ||
        wait_shutdown(runtime, FLAT_RUNTIME_SHUTDOWN_COMPLETE) != 0) {
        fprintf(stderr, "multi-channel DRAIN: shutdown did not complete status=%d\n",
                (int)flat_runtime_shutdown_status(runtime));
        goto done;
    }
    for (i = 0; i < CHANNELS; ++i) {
        double value = -1;
        if (get_number(runtime, channels[i], "counter", &value) != 0 ||
            value != (double)WRITERS_PER_CHANNEL) {
            fprintf(stderr, "multi-channel DRAIN: channel %u expected %u got %.0f\n",
                    i, WRITERS_PER_CHANNEL, value);
            goto done;
        }
    }
    if (flat_runtime_workers_join(runtime) != 0) {
        fprintf(stderr, "multi-channel DRAIN: worker join failed\n");
        goto done;
    }
    pool_started = 0;
    for (i = 0; i < created; ++i) {
        if (flat_runtime_task_state(runtime, tasks[i]) != FLAT_TASK_COMPLETED ||
            flat_runtime_task_destroy(runtime, tasks[i]) != 0) {
            fprintf(stderr, "multi-channel DRAIN: task %u did not complete/cleanup\n", i);
            goto done;
        }
        tasks[i] = 0;
    }
    created = 0;
    for (i = 0; i < CHANNELS; ++i) {
        if (flat_runtime_channel_destroy(runtime, channels[i]) != 0) {
            fprintf(stderr, "multi-channel DRAIN: channel %u destroy failed\n", i);
            goto done;
        }
        channels[i] = 0;
    }
    if (flat_runtime_destroy(runtime) != 0) {
        runtime = NULL;
        fprintf(stderr, "multi-channel DRAIN: runtime destroy failed\n");
        return -1;
    }
    runtime = NULL;
    rc = 0;

done:
    if (runtime) {
        (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        if (pool_started) {
            unsigned spins;
            for (spins = 0; spins < WAIT_LIMIT &&
                 flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++spins)
                pause_one_ms();
            if (flat_runtime_lifecycle(runtime) == FLAT_RUNTIME_STOPPED &&
                flat_runtime_workers_join(runtime) == 0) pool_started = 0;
        } else {
            unsigned spins;
            for (spins = 0; spins < WAIT_LIMIT &&
                 flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++spins) {
                (void)flat_runtime_schedule(runtime, 16, 1, NULL);
                pause_one_ms();
            }
        }
        for (i = 0; i < created; ++i) if (tasks[i]) (void)flat_runtime_task_destroy(runtime, tasks[i]);
        for (i = 0; i < CHANNELS; ++i) if (channels[i]) (void)flat_runtime_channel_destroy(runtime, channels[i]);
        (void)flat_runtime_destroy(runtime);
    }
    return rc;
}

static void *cancel_main(void *opaque) {
    CancelProbe *probe = (CancelProbe *)opaque;
    pthread_mutex_lock(probe->mutex);
    ++*probe->ready;
    pthread_cond_broadcast(probe->cond);
    while (!*probe->released) pthread_cond_wait(probe->cond, probe->mutex);
    pthread_mutex_unlock(probe->mutex);
    probe->result = flat_runtime_task_cancel_request(probe->runtime, probe->task_id);
    return NULL;
}

static int run_cancel_escalation(FlatBytecode waiter_bc[CHANNELS]) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    FlatChannelId channels[CHANNELS] = {0};
    FlatTaskId owners[CHANNELS] = {0}, waiters[CHANNELS] = {0};
    pthread_t threads[CHANNELS];
    CancelProbe probes[CHANNELS];
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned ready = 0, created_threads = 0, i;
    int released = 0, mutex_ready = 0, cond_ready = 0, workers_started = 0;
    int rc = -1;
    if (!runtime) return -1;
    if (pthread_mutex_init(&mutex, NULL) != 0) goto done;
    mutex_ready = 1;
    if (pthread_cond_init(&cond, NULL) != 0) goto done;
    cond_ready = 1;

    for (i = 0; i < CHANNELS; ++i) {
        if (flat_runtime_channel_create(runtime, &channels[i]) != 0 ||
            flat_runtime_task_create(runtime, &waiter_bc[i], NULL, &owners[i]) != 0 ||
            flat_runtime_channel_lock(runtime, channels[i], owners[i]) != 0 ||
            flat_runtime_task_create(runtime, &waiter_bc[i], NULL, &waiters[i]) != 0 ||
            flat_runtime_task_start(runtime, waiters[i]) != 0) {
            fprintf(stderr, "CANCEL escalation: setup channel %u failed\n", i);
            goto done;
        }
    }
    for (i = 0; i < CHANNELS; ++i) {
        uint64_t turns = 0;
        unsigned spin;
        for (spin = 0; spin < 16 && flat_runtime_task_state(runtime, waiters[i]) != FLAT_TASK_WAITING; ++spin) {
            if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0) {
                fprintf(stderr, "CANCEL escalation: waiter %u schedule failed\n", i);
                goto done;
            }
        }
        if (flat_runtime_task_state(runtime, waiters[i]) != FLAT_TASK_WAITING) {
            fprintf(stderr, "CANCEL escalation: waiter %u did not park\n", i);
            goto done;
        }
    }
    if (flat_runtime_workers_start(runtime, WORKERS, 1) != 0) {
        fprintf(stderr, "CANCEL escalation: worker start failed\n");
        goto done;
    }
    workers_started = 1;
    if (flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_DRAIN) != 0 ||
        wait_shutdown(runtime, FLAT_RUNTIME_SHUTDOWN_BLOCKED) != 0) {
        fprintf(stderr, "CANCEL escalation: DRAIN did not become blocked, got %d\n",
                (int)flat_runtime_shutdown_status(runtime));
        goto done;
    }

    for (i = 0; i < CHANNELS; ++i) {
        probes[i].runtime = runtime;
        probes[i].task_id = waiters[i];
        probes[i].mutex = &mutex;
        probes[i].cond = &cond;
        probes[i].ready = &ready;
        probes[i].released = &released;
        probes[i].result = -99;
        if (pthread_create(&threads[i], NULL, cancel_main, &probes[i]) != 0) {
            fprintf(stderr, "CANCEL escalation: cancel thread create %u failed\n", i);
            goto release_cancel_threads;
        }
        ++created_threads;
    }
    pthread_mutex_lock(&mutex);
    while (ready != created_threads) pthread_cond_wait(&cond, &mutex);
    if (created_threads != CHANNELS) {
        pthread_mutex_unlock(&mutex);
        goto release_cancel_threads;
    }
    released = 1;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mutex);
    for (i = 0; i < created_threads; ++i) pthread_join(threads[i], NULL);
    created_threads = 0;
    for (i = 0; i < CHANNELS; ++i) {
        if (probes[i].result != 0) {
            fprintf(stderr, "CANCEL escalation: cancel request %u returned %d\n", i, probes[i].result);
            goto done;
        }
    }
    /* DRAIN owns only started work. Once the cancelled waiters become
     * terminal, the pool reaches STOPPED while the CREATED transaction owners
     * remain host-owned. Join the exited pool before escalating CANCEL; the
     * host then drives cleanup cooperatively, matching the runtime contract. */
    if (wait_shutdown(runtime, FLAT_RUNTIME_SHUTDOWN_COMPLETE) != 0) {
        fprintf(stderr, "CANCEL escalation: DRAIN did not complete status=%d lifecycle=%d\n",
                (int)flat_runtime_shutdown_status(runtime), (int)flat_runtime_lifecycle(runtime));
        goto done;
    }
    for (i = 0; i < CHANNELS; ++i) {
        if (flat_runtime_task_state(runtime, owners[i]) != FLAT_TASK_CREATED ||
            flat_runtime_task_state(runtime, waiters[i]) != FLAT_TASK_CANCELLED) {
            fprintf(stderr, "DRAIN result: channel=%u owner=%d waiter=%d\n", i,
                    (int)flat_runtime_task_state(runtime, owners[i]),
                    (int)flat_runtime_task_state(runtime, waiters[i]));
            goto done;
        }
    }
    if (flat_runtime_workers_join(runtime) != 0) {
        fprintf(stderr, "CANCEL escalation: worker join failed\n");
        goto done;
    }
    workers_started = 0;
    if (flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL) != 0 ||
        flat_runtime_schedule(runtime, 1, 0, NULL) != 0 ||
        wait_shutdown(runtime, FLAT_RUNTIME_SHUTDOWN_COMPLETE) != 0) {
        fprintf(stderr, "CANCEL escalation: cooperative cleanup after join failed status=%d lifecycle=%d\n",
                (int)flat_runtime_shutdown_status(runtime), (int)flat_runtime_lifecycle(runtime));
        goto done;
    }
    for (i = 0; i < CHANNELS; ++i) {
        if (flat_runtime_task_state(runtime, owners[i]) != FLAT_TASK_CANCELLED ||
            flat_runtime_task_state(runtime, waiters[i]) != FLAT_TASK_CANCELLED) {
            fprintf(stderr, "CANCEL result: channel=%u owner=%d waiter=%d\n", i,
                    (int)flat_runtime_task_state(runtime, owners[i]),
                    (int)flat_runtime_task_state(runtime, waiters[i]));
            goto done;
        }
    }
    for (i = 0; i < CHANNELS; ++i) {
        if (flat_runtime_task_destroy(runtime, waiters[i]) != 0 ||
            flat_runtime_task_destroy(runtime, owners[i]) != 0 ||
            flat_runtime_channel_destroy(runtime, channels[i]) != 0) {
            fprintf(stderr, "CANCEL escalation: cleanup channel %u failed\n", i);
            goto done;
        }
        waiters[i] = owners[i] = 0;
        channels[i] = 0;
    }
    if (flat_runtime_destroy(runtime) != 0) {
        runtime = NULL;
        fprintf(stderr, "CANCEL escalation: runtime destroy failed\n");
        return -1;
    }
    runtime = NULL;
    rc = 0;
    goto done;

release_cancel_threads:
    pthread_mutex_lock(&mutex);
    released = 1;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mutex);
    for (i = 0; i < created_threads; ++i) pthread_join(threads[i], NULL);
    created_threads = 0;

done:
    if (runtime) {
        if (workers_started && flat_runtime_lifecycle(runtime) == FLAT_RUNTIME_STOPPED) {
            if (flat_runtime_workers_join(runtime) == 0) workers_started = 0;
        }
        (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        if (workers_started) {
            unsigned spin;
            for (spin = 0; spin < WAIT_LIMIT &&
                 flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++spin)
                pause_one_ms();
            if (flat_runtime_lifecycle(runtime) == FLAT_RUNTIME_STOPPED &&
                flat_runtime_workers_join(runtime) == 0) workers_started = 0;
        }
        if (!workers_started && flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE) {
            unsigned spin;
            for (spin = 0; spin < WAIT_LIMIT &&
                 flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++spin) {
                (void)flat_runtime_schedule(runtime, 16, 1, NULL);
                pause_one_ms();
            }
        }
        for (i = 0; i < CHANNELS; ++i) {
            if (waiters[i]) (void)flat_runtime_task_destroy(runtime, waiters[i]);
            if (owners[i]) (void)flat_runtime_task_destroy(runtime, owners[i]);
            if (channels[i]) (void)flat_runtime_channel_destroy(runtime, channels[i]);
        }
        (void)flat_runtime_destroy(runtime);
    }
    if (cond_ready) pthread_cond_destroy(&cond);
    if (mutex_ready) pthread_mutex_destroy(&mutex);
    return rc;
}

int main(int argc, char **argv) {
    FlatBytecode *writer_bc = (FlatBytecode *)calloc(CHANNELS, sizeof *writer_bc);
    FlatBytecode *waiter_bc = (FlatBytecode *)calloc(CHANNELS, sizeof *waiter_bc);
    unsigned i;
    int rc = 1;
    if (!writer_bc || !waiter_bc || argc != 1 + CHANNELS * 2) {
        free(writer_bc);
        free(waiter_bc);
        return 2;
    }
    for (i = 0; i < CHANNELS; ++i) {
        if (load_bc(argv[1 + i], &writer_bc[i]) != 0 ||
            load_bc(argv[1 + CHANNELS + i], &waiter_bc[i]) != 0) goto done;
    }
    if (run_multi_channel_drain(writer_bc) != 0) goto done;
    if (run_cancel_escalation(waiter_bc) != 0) goto done;
    rc = 0;
done:
    for (i = 0; i < CHANNELS; ++i) {
        flat_fb_free(&waiter_bc[i]);
        flat_fb_free(&writer_bc[i]);
    }
    free(waiter_bc);
    free(writer_bc);
    if (rc == 0)
        puts("worker-channel-shutdown-stress-ok channels=4 writers-per-channel=8 workers=4 drain=blocked-then-complete cancel-requests=4 escalation=post-join-cleanup refs=released");
    else
        fprintf(stderr, "worker Channel/shutdown stress failed\n");
    return rc;
}
