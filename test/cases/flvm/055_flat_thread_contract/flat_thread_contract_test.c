#include "flat_sync.h"

#include <stdio.h>
#include <string.h>

typedef struct Shared {
    FlatMutex mutex;
    FlatCond cond;
    int ready;
    int completed;
} Shared;

static void *waiter_main(void *opaque) {
    Shared *shared = (Shared *)opaque;
    if (flat_mutex_lock(&shared->mutex) != 0) return (void *)1;
    while (!shared->ready) {
        if (flat_cond_wait(&shared->cond, &shared->mutex) != 0) {
            (void)flat_mutex_unlock(&shared->mutex);
            return (void *)2;
        }
    }
    shared->completed = 1;
    (void)flat_mutex_unlock(&shared->mutex);
    return NULL;
}

int main(void) {
    FlatThread invalid;
    FlatThread thread;
    Shared shared;

    /* A failed create must leave a safely non-joinable object, even when the
     * caller supplied an otherwise uninitialized FlatThread storage slot. */
    memset(&invalid, 0xA5, sizeof invalid);
    if (flat_thread_create(&invalid, NULL, NULL) == 0 ||
        flat_thread_join(&invalid) == 0) {
        fprintf(stderr, "invalid-create left a joinable FlatThread\n");
        return 1;
    }
    if (flat_thread_create(NULL, waiter_main, NULL) == 0 ||
        flat_thread_join(NULL) == 0) {
        fprintf(stderr, "null FlatThread validation failed\n");
        return 2;
    }

    memset(&shared, 0, sizeof shared);
    if (flat_mutex_init(&shared.mutex) != 0 ||
        flat_cond_init(&shared.cond) != 0) return 3;

    memset(&thread, 0xA5, sizeof thread);
    if (flat_thread_create(&thread, waiter_main, &shared) != 0) return 4;

    if (flat_mutex_lock(&shared.mutex) != 0) return 5;
    shared.ready = 1;
    if (flat_cond_signal(&shared.cond) != 0) return 6;
    if (flat_mutex_unlock(&shared.mutex) != 0) return 7;

    if (flat_thread_join(&thread) != 0) return 8;
    if (shared.completed != 1) return 9;
    if (flat_thread_join(&thread) == 0) return 10;

    if (flat_cond_destroy(&shared.cond) != 0 ||
        flat_mutex_destroy(&shared.mutex) != 0) return 11;

    puts("flat-thread-contract-ok failed-create=nonjoinable cond-wakeup=joined repeated-join=rejected");
    return 0;
}
