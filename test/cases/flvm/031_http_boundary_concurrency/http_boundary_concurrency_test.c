#include "flat_http_boundary.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#define THREAD_COUNT 8
#define SEQUENCES_PER_THREAD 5000
#define TOTAL_SEQUENCES (THREAD_COUNT * SEQUENCES_PER_THREAD)

static atomic_uchar seen[TOTAL_SEQUENCES + 1];
static atomic_int failures = ATOMIC_VAR_INIT(0);

static void *sequence_worker(void *unused) {
    int i;
    (void)unused;
    for (i = 0; i < SEQUENCES_PER_THREAD; ++i) {
        uint64_t sequence = flat_http_boundary_next_sequence();
        if (sequence == 0 || sequence > TOTAL_SEQUENCES ||
            atomic_exchange_explicit(&seen[sequence], 1, memory_order_relaxed) != 0) {
            atomic_fetch_add_explicit(&failures, 1, memory_order_relaxed);
        }
    }
    return NULL;
}

int main(void) {
    pthread_t threads[THREAD_COUNT];
    int i;

    for (i = 0; i < THREAD_COUNT; ++i) {
        if (pthread_create(&threads[i], NULL, sequence_worker, NULL) != 0) return 1;
    }
    for (i = 0; i < THREAD_COUNT; ++i) {
        if (pthread_join(threads[i], NULL) != 0) return 2;
    }
    if (atomic_load_explicit(&failures, memory_order_relaxed) != 0) return 3;
    for (i = 1; i <= TOTAL_SEQUENCES; ++i) {
        if (atomic_load_explicit(&seen[i], memory_order_relaxed) != 1) return 4;
    }
    puts("http-boundary-concurrency-ok");
    return 0;
}
