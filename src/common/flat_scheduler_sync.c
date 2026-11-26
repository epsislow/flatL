#include "flat_scheduler_sync.h"

#include <stdlib.h>
#include <string.h>

int flat_ready_queue_init(FlatReadyQueue *queue, size_t initial_capacity) {
    if (!queue) return -1;
    memset(queue, 0, sizeof *queue);
    if (initial_capacity == 0) initial_capacity = 16;
    if (initial_capacity > SIZE_MAX / sizeof *queue->items) return -1;
    queue->items = (uint64_t *)malloc(initial_capacity * sizeof *queue->items);
    if (!queue->items) return -1;
    if (flat_mutex_init(&queue->mutex) != 0) {
        free(queue->items);
        memset(queue, 0, sizeof *queue);
        return -1;
    }
    if (flat_cond_init(&queue->available) != 0) {
        (void)flat_mutex_destroy(&queue->mutex);
        free(queue->items);
        memset(queue, 0, sizeof *queue);
        return -1;
    }
    queue->capacity = initial_capacity;
    queue->initialized = 1;
    return 0;
}

static int flat_ready_queue_grow(FlatReadyQueue *queue) {
    size_t new_capacity;
    uint64_t *items;
    size_t i;
    if (queue->capacity > SIZE_MAX / 2 / sizeof *queue->items) return -1;
    new_capacity = queue->capacity * 2;
    items = (uint64_t *)malloc(new_capacity * sizeof *items);
    if (!items) return -1;
    for (i = 0; i < queue->count; ++i)
        items[i] = queue->items[(queue->head + i) % queue->capacity];
    free(queue->items);
    queue->items = items;
    queue->capacity = new_capacity;
    queue->head = 0;
    return 0;
}

int flat_ready_queue_push(FlatReadyQueue *queue, uint64_t task_id) {
    size_t tail;
    int rc = -1;
    if (!queue || !queue->initialized || task_id == 0) return -1;
    if (flat_mutex_lock(&queue->mutex) != 0) return -1;
    if (!queue->closed) {
        if (queue->count == queue->capacity && flat_ready_queue_grow(queue) != 0) {
            rc = -1;
        } else {
            tail = (queue->head + queue->count) % queue->capacity;
            queue->items[tail] = task_id;
            queue->count++;
            rc = flat_cond_signal(&queue->available);
        }
    }
    (void)flat_mutex_unlock(&queue->mutex);
    return rc;
}

int flat_ready_queue_pop(FlatReadyQueue *queue, uint64_t *out_task_id) {
    int rc;
    if (!queue || !queue->initialized || !out_task_id) return -1;
    if (flat_mutex_lock(&queue->mutex) != 0) return -1;
    while (queue->count == 0 && !queue->closed) {
        if (flat_cond_wait(&queue->available, &queue->mutex) != 0) {
            (void)flat_mutex_unlock(&queue->mutex);
            return -1;
        }
    }
    if (queue->count == 0) {
        rc = 0;
    } else {
        *out_task_id = queue->items[queue->head];
        queue->head = (queue->head + 1) % queue->capacity;
        queue->count--;
        rc = 1;
    }
    (void)flat_mutex_unlock(&queue->mutex);
    return rc;
}

uint64_t flat_ready_queue_wake_epoch(FlatReadyQueue *queue) {
    uint64_t epoch = 0;
    if (!queue || !queue->initialized || flat_mutex_lock(&queue->mutex) != 0) return 0;
    epoch = queue->wake_epoch;
    (void)flat_mutex_unlock(&queue->mutex);
    return epoch;
}

int flat_ready_queue_wait_since(FlatReadyQueue *queue, uint64_t observed_epoch) {
    int rc;
    if (!queue || !queue->initialized) return -1;
    if (flat_mutex_lock(&queue->mutex) != 0) return -1;
    while (queue->count == 0 && !queue->closed &&
           queue->wake_epoch == observed_epoch) {
        if (flat_cond_wait(&queue->available, &queue->mutex) != 0) {
            (void)flat_mutex_unlock(&queue->mutex);
            return -1;
        }
    }
    if (queue->count > 0) rc = 1;
    else if (queue->closed) rc = 0;
    else rc = 2;
    (void)flat_mutex_unlock(&queue->mutex);
    return rc;
}

int flat_ready_queue_wait(FlatReadyQueue *queue) {
    uint64_t epoch = flat_ready_queue_wake_epoch(queue);
    return flat_ready_queue_wait_since(queue, epoch);
}

int flat_ready_queue_wake_all(FlatReadyQueue *queue) {
    int rc;
    if (!queue || !queue->initialized) return -1;
    if (flat_mutex_lock(&queue->mutex) != 0) return -1;
    queue->wake_epoch++;
    rc = flat_cond_broadcast(&queue->available);
    (void)flat_mutex_unlock(&queue->mutex);
    return rc;
}

int flat_ready_queue_try_pop(FlatReadyQueue *queue, uint64_t *out_task_id) {
    if (!queue || !queue->initialized || !out_task_id) return -1;
    if (flat_mutex_lock(&queue->mutex) != 0) return -1;
    if (queue->count == 0) {
        (void)flat_mutex_unlock(&queue->mutex);
        return 0;
    }
    *out_task_id = queue->items[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    (void)flat_mutex_unlock(&queue->mutex);
    return 1;
}

int flat_ready_queue_remove(FlatReadyQueue *queue, uint64_t task_id) {
    size_t i;
    size_t found = SIZE_MAX;
    if (!queue || !queue->initialized || task_id == 0) return -1;
    if (flat_mutex_lock(&queue->mutex) != 0) return -1;
    for (i = 0; i < queue->count; ++i) {
        if (queue->items[(queue->head + i) % queue->capacity] == task_id) {
            found = i;
            break;
        }
    }
    if (found != SIZE_MAX) {
        for (i = found; i + 1 < queue->count; ++i) {
            size_t dst = (queue->head + i) % queue->capacity;
            size_t src = (queue->head + i + 1) % queue->capacity;
            queue->items[dst] = queue->items[src];
        }
        queue->count--;
    }
    (void)flat_mutex_unlock(&queue->mutex);
    return found == SIZE_MAX ? 0 : 1;
}

int flat_ready_queue_close(FlatReadyQueue *queue) {
    int rc;
    if (!queue || !queue->initialized) return -1;
    if (flat_mutex_lock(&queue->mutex) != 0) return -1;
    queue->closed = 1;
    rc = flat_cond_broadcast(&queue->available);
    (void)flat_mutex_unlock(&queue->mutex);
    return rc;
}

int flat_ready_queue_destroy(FlatReadyQueue *queue) {
    int rc = 0;
    int drained;
    if (!queue || !queue->initialized) return -1;
    /* Caller must also have joined all producers/consumers before destruction. */
    if (flat_mutex_lock(&queue->mutex) != 0) return -1;
    drained = queue->closed && queue->count == 0;
    (void)flat_mutex_unlock(&queue->mutex);
    if (!drained) return -1;
    if (flat_cond_destroy(&queue->available) != 0) rc = -1;
    if (flat_mutex_destroy(&queue->mutex) != 0) rc = -1;
    free(queue->items);
    memset(queue, 0, sizeof *queue);
    return rc;
}

void flat_task_dispatch_init(FlatTaskDispatchState *state) {
    if (state) memset(state, 0, sizeof *state);
}

int flat_task_dispatch_enqueue(FlatTaskDispatchState *state) {
    if (!state) return -1;
    if (state->worker_owned) {
        state->wake_pending = 1;
        return 0;
    }
    if (state->queued) return 0;
    state->queued = 1;
    return 1;
}

int flat_task_dispatch_claim(FlatTaskDispatchState *state, uint64_t *out_token) {
    if (!state || !out_token || !state->queued || state->worker_owned) return -1;
    state->queued = 0;
    state->worker_owned = 1;
    state->dispatch_token++;
    if (state->dispatch_token == 0) state->dispatch_token++;
    *out_token = state->dispatch_token;
    return 0;
}

int flat_task_dispatch_wake(FlatTaskDispatchState *state) {
    if (!state) return -1;
    if (state->worker_owned) {
        state->wake_pending = 1;
        return 0;
    }
    if (state->queued) return 0;
    state->queued = 1;
    return 1;
}

int flat_task_dispatch_release(FlatTaskDispatchState *state, uint64_t token,
                               int *out_requeue) {
    if (!state || !out_requeue || !state->worker_owned || token == 0 ||
        token != state->dispatch_token) return -1;
    state->worker_owned = 0;
    *out_requeue = 0;
    if (state->wake_pending) {
        state->wake_pending = 0;
        if (!state->queued) {
            state->queued = 1;
            *out_requeue = 1;
        }
    }
    return 0;
}
