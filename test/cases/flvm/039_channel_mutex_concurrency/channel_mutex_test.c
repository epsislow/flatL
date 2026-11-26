#include "flat_channel.h"
#include "flat_runtime.h"
#include "flat_value.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define THREAD_COUNT 8
#define ITERATIONS 2000

typedef struct WorkerArgs {
    FlatRuntime *runtime;
    FlatChannelId channel;
    unsigned index;
    int result;
} WorkerArgs;

static int value_is_number(const FlatValue *value, double expected) {
    return value && value->tag == VAL_NUMBER && value->u.number.payload.f64 == expected;
}

static void *worker_main(void *opaque) {
    WorkerArgs *args = (WorkerArgs *)opaque;
    FlatCollectionPool *source_coll = flat_coll_create();
    FlatStringPool *source_pool = flat_pool_create();
    FlatCollectionPool *destination_coll = flat_coll_create();
    FlatStringPool *destination_pool = flat_pool_create();
    char key[32];
    int i;
    if (!source_coll || !source_pool || !destination_coll || !destination_pool) {
        args->result = 1;
        goto done;
    }
    (void)snprintf(key, sizeof key, "worker_%u", args->index);
    for (i = 0; i < ITERATIONS; ++i) {
        FlatValue value;
        FlatValue out;
        int rc;
        memset(&value, 0, sizeof value);
        memset(&out, 0, sizeof out);
        flat_value_set_number(&value, (double)i);
        rc = flat_runtime_channel_set(args->runtime, args->channel, 0, key, &value,
                                      source_coll, source_pool, NULL);
        if (rc != 0 || flat_runtime_channel_var(args->runtime, args->channel, 0, key,
                                                destination_coll, destination_pool,
                                                NULL, &out) != 0 ||
            !value_is_number(&out, (double)i)) {
            flat_value_release(destination_coll, destination_pool, &out);
            args->result = 2;
            goto done;
        }
        flat_value_release(destination_coll, destination_pool, &out);
    }

done:
    flat_coll_destroy(source_coll);
    flat_pool_destroy(source_pool);
    flat_coll_destroy(destination_coll);
    flat_pool_destroy(destination_pool);
    return NULL;
}

int main(void) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    FlatChannelId channel = 0;
    pthread_t threads[THREAD_COUNT];
    WorkerArgs args[THREAD_COUNT];
    unsigned i;
    int rc = 0;
    if (!runtime || flat_runtime_channel_create(runtime, &channel) != 0 || channel == 0) {
        if (runtime) (void)flat_runtime_destroy(runtime);
        return 1;
    }
    for (i = 0; i < THREAD_COUNT; ++i) {
        args[i].runtime = runtime;
        args[i].channel = channel;
        args[i].index = i;
        args[i].result = 0;
        if (pthread_create(&threads[i], NULL, worker_main, &args[i]) != 0) {
            rc = 2;
            break;
        }
    }
    while (i > 0) {
        --i;
        if (pthread_join(threads[i], NULL) != 0 || args[i].result != 0) rc = 3;
    }
    if (rc == 0 && flat_runtime_channel_destroy(runtime, channel) != 0) rc = 4;
    if (rc == 0 && flat_runtime_destroy(runtime) != 0) rc = 5;
    if (rc != 0) {
        /* Best-effort teardown for a failed stress run. */
        (void)flat_runtime_channel_destroy(runtime, channel);
        (void)flat_runtime_destroy(runtime);
        fprintf(stderr, "channel mutex concurrency failed: %d\n", rc);
        return rc;
    }
    puts("channel-mutex-ok threads=8 iterations=2000 shared-channel=1");
    return 0;
}
