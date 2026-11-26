#include "flat_scheduler_sync.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define PRODUCERS 4
#define CONSUMERS 4
#define ITEMS_PER_PRODUCER 5000
#define TOTAL_ITEMS (PRODUCERS * ITEMS_PER_PRODUCER)

typedef struct {
    FlatReadyQueue *queue;
    int producer_id;
    atomic_int *failed;
} ProducerArgs;

typedef struct {
    FlatReadyQueue *queue;
    atomic_int *seen;
    atomic_int *consumed;
    atomic_int *failed;
} ConsumerArgs;

typedef struct {
    FlatReadyQueue *queue;
    atomic_int started;
    int result;
} EmptyConsumerArgs;

static void *producer_main(void *opaque) {
    ProducerArgs *args = (ProducerArgs *)opaque;
    int i;
    for (i = 0; i < ITEMS_PER_PRODUCER; ++i) {
        uint64_t id = (uint64_t)args->producer_id * ITEMS_PER_PRODUCER + (uint64_t)i + 1;
        if (flat_ready_queue_push(args->queue, id) != 0) {
            atomic_store(args->failed, 1);
            break;
        }
    }
    return NULL;
}

static void *consumer_main(void *opaque) {
    ConsumerArgs *args = (ConsumerArgs *)opaque;
    for (;;) {
        uint64_t id = 0;
        int rc = flat_ready_queue_pop(args->queue, &id);
        if (rc == 0) break;
        if (rc != 1 || id == 0 || id > TOTAL_ITEMS) {
            atomic_store(args->failed, 1);
            break;
        }
        if (atomic_fetch_add(&args->seen[id - 1], 1) != 0)
            atomic_store(args->failed, 1);
        atomic_fetch_add(args->consumed, 1);
    }
    return NULL;
}

static void *empty_consumer_main(void *opaque) {
    EmptyConsumerArgs *args = (EmptyConsumerArgs *)opaque;
    uint64_t id = 0;
    atomic_store(&args->started, 1);
    args->result = flat_ready_queue_pop(args->queue, &id);
    return NULL;
}

static int test_queue_growth(void) {
    FlatReadyQueue queue;
    uint64_t id = 0;
    int i;
    if (flat_ready_queue_init(&queue, 2) != 0) return 1;
    if (flat_ready_queue_try_pop(&queue, &id) != 0) return 8; /* nonblocking empty */
    if (flat_ready_queue_push(&queue, 900) != 0 ||
        flat_ready_queue_push(&queue, 901) != 0 ||
        flat_ready_queue_push(&queue, 902) != 0) return 9;
    if (flat_ready_queue_remove(&queue, 901) != 1 ||
        flat_ready_queue_remove(&queue, 901) != 0) return 10;
    if (flat_ready_queue_try_pop(&queue, &id) != 1 || id != 900) return 11;
    if (flat_ready_queue_try_pop(&queue, &id) != 1 || id != 902) return 12;
    if (flat_ready_queue_try_pop(&queue, &id) != 0) return 13;
    /* Removal must preserve FIFO order when the ring head has wrapped. */
    if (flat_ready_queue_push(&queue, 910) != 0 ||
        flat_ready_queue_push(&queue, 911) != 0 ||
        flat_ready_queue_push(&queue, 912) != 0 ||
        flat_ready_queue_try_pop(&queue, &id) != 1 || id != 910 ||
        flat_ready_queue_push(&queue, 913) != 0 ||
        flat_ready_queue_remove(&queue, 912) != 1 ||
        flat_ready_queue_try_pop(&queue, &id) != 1 || id != 911 ||
        flat_ready_queue_try_pop(&queue, &id) != 1 || id != 913 ||
        flat_ready_queue_try_pop(&queue, &id) != 0) return 14;
    for (i = 1; i <= 256; ++i) {
        if (flat_ready_queue_push(&queue, (uint64_t)i) != 0) {
            return 2;
        }
    }
    if (flat_ready_queue_destroy(&queue) == 0) return 3; /* open/nonempty is busy */
    if (flat_ready_queue_close(&queue) != 0) return 4;
    if (flat_ready_queue_destroy(&queue) == 0) return 5; /* closed but not drained */
    for (i = 1; i <= 256; ++i) {
        if (flat_ready_queue_pop(&queue, &id) != 1 || id != (uint64_t)i) {
            (void)flat_ready_queue_destroy(&queue);
            return 3;
        }
    }
    if (flat_ready_queue_pop(&queue, &id) != 0) return 6;
    return flat_ready_queue_destroy(&queue) == 0 ? 0 : 7;
}

static int test_dispatch_ownership(void) {
    FlatTaskDispatchState state;
    uint64_t first = 0, second = 0;
    int requeue = -1;
    flat_task_dispatch_init(&state);

    if (flat_task_dispatch_enqueue(&state) != 1 || !state.queued) return 1;
    if (flat_task_dispatch_enqueue(&state) != 0 || !state.queued) return 2;
    if (flat_task_dispatch_claim(&state, &first) != 0 || first == 0 ||
        state.queued || !state.worker_owned) return 3;

    /* A wakeup while the worker still owns the VM is deferred, never queued. */
    if (flat_task_dispatch_wake(&state) != 0 || !state.wake_pending || state.queued)
        return 4;
    if (flat_task_dispatch_enqueue(&state) != 0 || !state.wake_pending || state.queued)
        return 5;
    if (flat_task_dispatch_release(&state, first, &requeue) != 0 || requeue != 1 ||
        state.worker_owned || state.wake_pending || !state.queued) return 6;

    if (flat_task_dispatch_claim(&state, &second) != 0 || second == first ||
        !state.worker_owned || state.queued) return 7;
    if (flat_task_dispatch_release(&state, first, &requeue) == 0) return 8;
    if (flat_task_dispatch_release(&state, second, &requeue) != 0 || requeue != 0 ||
        state.worker_owned || state.queued || state.wake_pending) return 9;

    /* A wakeup for an already queued task is coalesced. */
    if (flat_task_dispatch_enqueue(&state) != 1) return 10;
    if (flat_task_dispatch_wake(&state) != 0 || !state.queued) return 11;
    return 0;
}

int main(void) {
    FlatReadyQueue queue;
    FlatThread producers[PRODUCERS];
    FlatThread consumers[CONSUMERS];
    ProducerArgs producer_args[PRODUCERS];
    ConsumerArgs consumer_args;
    atomic_int *seen;
    atomic_int failed = 0;
    atomic_int consumed = 0;
    int i;
    int dispatch_rc;
    uint64_t drained_id = 0;

    dispatch_rc = test_dispatch_ownership();
    if (dispatch_rc != 0) {
        fprintf(stderr, "dispatch ownership state machine failed: %d\n", dispatch_rc);
        return 2;
    }
    dispatch_rc = test_queue_growth();
    if (dispatch_rc != 0) {
        fprintf(stderr, "ready queue growth/FIFO test failed: %d\n", dispatch_rc);
        return 3;
    }

    seen = (atomic_int *)calloc(TOTAL_ITEMS, sizeof *seen);
    if (!seen) return 4;
    for (i = 0; i < TOTAL_ITEMS; ++i) atomic_init(&seen[i], 0);
    if (flat_ready_queue_init(&queue, 4) != 0) {
        free(seen);
        return 5;
    }

    consumer_args.queue = &queue;
    consumer_args.seen = seen;
    consumer_args.consumed = &consumed;
    consumer_args.failed = &failed;
    for (i = 0; i < CONSUMERS; ++i) {
        if (flat_thread_create(&consumers[i], consumer_main, &consumer_args) != 0) {
            atomic_store(&failed, 1);
            break;
        }
    }
    if (i != CONSUMERS) {
        (void)flat_ready_queue_close(&queue);
        while (i > 0) (void)flat_thread_join(&consumers[--i]);
        (void)flat_ready_queue_destroy(&queue);
        free(seen);
        return 6;
    }

    for (i = 0; i < PRODUCERS; ++i) {
        producer_args[i].queue = &queue;
        producer_args[i].producer_id = i;
        producer_args[i].failed = &failed;
        if (flat_thread_create(&producers[i], producer_main, &producer_args[i]) != 0) {
            atomic_store(&failed, 1);
            break;
        }
    }
    {
        int created = i;
        for (i = 0; i < created; ++i) (void)flat_thread_join(&producers[i]);
        if (created != PRODUCERS) atomic_store(&failed, 1);
    }

    /* Closing rejects new producers but consumers drain all already queued IDs. */
    if (flat_ready_queue_close(&queue) != 0) atomic_store(&failed, 1);
    if (flat_ready_queue_push(&queue, (uint64_t)TOTAL_ITEMS + 1) == 0)
        atomic_store(&failed, 1);
    for (i = 0; i < CONSUMERS; ++i) (void)flat_thread_join(&consumers[i]);

    if (atomic_load(&failed) || atomic_load(&consumed) != TOTAL_ITEMS) {
        fprintf(stderr, "queue stress failed: consumed=%d expected=%d failed=%d\n",
                atomic_load(&consumed), TOTAL_ITEMS, atomic_load(&failed));
        (void)flat_ready_queue_destroy(&queue);
        free(seen);
        return 7;
    }
    for (i = 0; i < TOTAL_ITEMS; ++i) {
        if (atomic_load(&seen[i]) != 1) {
            fprintf(stderr, "queue item %d observed %d times\n", i + 1,
                    atomic_load(&seen[i]));
            (void)flat_ready_queue_destroy(&queue);
            free(seen);
            return 8;
        }
    }
    if (flat_ready_queue_pop(&queue, &drained_id) != 0) {
        (void)flat_ready_queue_destroy(&queue);
        free(seen);
        return 9;
    }
    if (flat_ready_queue_destroy(&queue) != 0) {
        free(seen);
        return 10;
    }
    free(seen);

    /* All consumers blocked on an empty queue must wake when close broadcasts. */
    {
        FlatReadyQueue empty_queue;
        FlatThread waiting_consumers[CONSUMERS];
        EmptyConsumerArgs args[CONSUMERS];
        struct timespec delay = { 0, 1000000L };
        int spins;
        if (flat_ready_queue_init(&empty_queue, 2) != 0) return 11;
        for (i = 0; i < CONSUMERS; ++i) {
            args[i].queue = &empty_queue;
            args[i].result = -99;
            atomic_init(&args[i].started, 0);
            if (flat_thread_create(&waiting_consumers[i], empty_consumer_main, &args[i]) != 0)
                return 12;
        }
        for (spins = 0; spins < 1000; ++spins) {
            int all_started = 1;
            for (i = 0; i < CONSUMERS; ++i)
                if (!atomic_load(&args[i].started)) all_started = 0;
            if (all_started) break;
            nanosleep(&delay, NULL);
        }
        nanosleep(&delay, NULL);
        if (flat_ready_queue_close(&empty_queue) != 0) return 13;
        for (i = 0; i < CONSUMERS; ++i) {
            if (flat_thread_join(&waiting_consumers[i]) != 0 || args[i].result != 0)
                return 14;
        }
        if (flat_ready_queue_destroy(&empty_queue) != 0) return 15;
    }

    puts("scheduler-sync-ok mpmc=20000 growth=fifo dispatch=single-owner wake-pending=coalesced close=drain-broadcast");
    return 0;
}
