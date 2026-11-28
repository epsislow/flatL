#include "flat_runtime.h"
#include "flat_runtime_event.h"
#include "flat_runtime_socket.h"
#include "flat_native_mods.h"
#include "flat_runtime_ws_server.h"
#if FLAT_WITH_WS_SERVER
#include "flat_io_reactor.h"
#include "flat_ws_server_registry.h"
#endif
#if FLAT_WITH_WEBSOCKET
#include "flat_ws_event_client.h"
#endif
#include "flat_vcx.h"
#include "flat_task.h"
#include "flat_task_native.h"
#include "flat_channel_internal.h"
#include "flat_vm_internal.h"
#include "flat_runtime_log.h"
#include "flat_number.h"
#include "flat_sync.h"
#include "flat_scheduler_sync.h"
#include "flat_native_plugin.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include <limits.h>
#ifdef FLAT_WASM_BUILD
#include <emscripten.h>
#endif
#ifdef FLAT_RUNTIME_TESTING
#include <stdatomic.h>
#include <time.h>
#endif

typedef struct FlatRuntimeWorker {
    FlatThread thread;
    struct FlatRuntime *runtime;
    uint64_t quantum;
    int started;
} FlatRuntimeWorker;

#if FLAT_WITH_WEBSOCKET
typedef struct FlatRuntimeSocketEntry {
    uint64_t id;
    FlatWsEventClient *client;
    struct FlatRuntimeSocketEntry *next;
} FlatRuntimeSocketEntry;
#endif

typedef struct FlatRuntimeOpenSocketEvent {
    uint64_t source_id;
    struct FlatRuntimeOpenSocketEvent *next;
} FlatRuntimeOpenSocketEvent;

typedef struct FlatRuntimeEventNode {
    FlatValue value;
    FlatCollectionPool *coll;
    FlatStringPool *pool;
    struct FlatRuntimeEventNode *next;
} FlatRuntimeEventNode;

typedef struct FlatRuntimePendingSocketEvents {
    uint64_t source_id;
    FlatRuntimeEventNode *head;
    FlatRuntimeEventNode *tail;
    size_t count;
    struct FlatRuntimePendingSocketEvents *next;
} FlatRuntimePendingSocketEvents;

typedef struct FlatRuntimeEventSubscription {
    FlatEventSubscriptionId id;
    uint64_t source_id;
    char source_kind[32];
    char event_name[48];
    FlatRuntimeEventNode *head;
    FlatRuntimeEventNode *tail;
    size_t queued;
    int dispatching;
    int handler_dispatching;
    int source_closed;
    size_t active_handlers;
    size_t max_active;
    size_t queue_capacity;
    FlatEventOverflowPolicy overflow_policy;
    FlatEventDispatchMode dispatch_mode;
    const FlatBytecode *handler_bc;
    FlatBytecode handler_bytecode;
    int owns_handler_bytecode;
    char handler_label[FGIR_MAX_LABEL];
    FlatValue *bound_args;
    size_t bound_arg_count;
    FlatCollectionPool *bound_coll;
    FlatStringPool *bound_pool;
    struct FlatRuntimeEventSubscription *next;
} FlatRuntimeEventSubscription;

typedef struct FlatTaskRecord {
    FlatTaskId id;
    FlatTaskState state;
    FlatVm *vm;
    FlatVmResult result;
    FlatTaskId waiting_on;
    FlatChannelId waiting_channel_id;
    int cleanup_owned;
    int cancel_requested;
    FlatEventSubscriptionId event_subscription_id;
    int event_handler_accounted;
    FlatTaskDispatchState dispatch;
    struct FlatTaskRecord *next;
} FlatTaskRecord;

struct FlatRuntime {
    FlatHostConfig host;
    FlatMutex scheduler_mutex;
#if FLAT_WITH_WEBSOCKET
    FlatMutex socket_mutex;
    FlatRuntimeSocketEntry *sockets;
    size_t socket_count;
    uint64_t next_socket_id;
#endif
#if FLAT_WITH_WS_SERVER
    FlatIoReactor *io_reactor;
    FlatWsServerRegistry *ws_server_registry;
#endif
    FlatReadyQueue ready_queue;
    unsigned active_runs;
    unsigned schedule_active;
    FlatRuntimeWorker *workers;
    size_t worker_count;
    int workers_started;
    int worker_launch;
    int worker_starting;
    int workers_joined;
    int workers_joining;
    int worker_mode_entered;
    int worker_start_failed;
    int worker_stop;
    int worker_error;
    FlatTaskRecord *tasks;
    FlatRuntimeEventSubscription *event_subscriptions;
    FlatRuntimeOpenSocketEvent *open_socket_events;
    FlatRuntimePendingSocketEvents *pending_socket_events;
    size_t pending_socket_event_count;
    size_t event_subscription_count;
    FlatEventSubscriptionId next_event_subscription_id;
    FlatChannel *channels;
    size_t channel_count;
    FlatChannelId next_channel_id;
    FlatTaskRecord *tasks_tail;
    size_t task_count;
    FlatTaskId next_task_id;
    FlatTaskId last_scheduled_id;
    FlatRuntimeLifecycle lifecycle;
};

static int runtime_schedule_internal(FlatRuntime *runtime, uint64_t quantum,
                                     uint64_t max_quanta, uint64_t *out_quanta,
                                     int worker_mode);
static void *runtime_worker_main(void *opaque);

#ifdef FLAT_RUNTIME_TESTING
static int runtime_test_create_fail_after = -1;
static int runtime_test_join_fail_after = -1;
static atomic_int runtime_test_join_delay_ms = ATOMIC_VAR_INIT(0);

void flat_runtime_test_worker_fail_create_after(int successful_creates) {
    runtime_test_create_fail_after = successful_creates;
}

void flat_runtime_test_worker_fail_join_after(int successful_joins) {
    runtime_test_join_fail_after = successful_joins;
}

void flat_runtime_test_worker_join_delay_ms(int delay_ms) {
    atomic_store(&runtime_test_join_delay_ms, delay_ms > 0 ? delay_ms : 0);
}
#endif

static int runtime_worker_thread_create(FlatThread *thread, FlatThreadFn fn, void *arg) {
#ifdef FLAT_RUNTIME_TESTING
    if (runtime_test_create_fail_after >= 0) {
        if (runtime_test_create_fail_after == 0) {
            runtime_test_create_fail_after = -1;
            return -1;
        }
        runtime_test_create_fail_after--;
    }
#endif
    return flat_thread_create(thread, fn, arg);
}

static int runtime_worker_thread_join(FlatThread *thread) {
#ifdef FLAT_RUNTIME_TESTING
    {
        int delay_ms = atomic_exchange(&runtime_test_join_delay_ms, 0);
        if (delay_ms > 0) {
            struct timespec delay;
            delay.tv_sec = delay_ms / 1000;
            delay.tv_nsec = (long)(delay_ms % 1000) * 1000000L;
            (void)nanosleep(&delay, NULL);
        }
    }
    if (runtime_test_join_fail_after >= 0) {
        if (runtime_test_join_fail_after == 0) {
            runtime_test_join_fail_after = -1;
            return -1;
        }
        runtime_test_join_fail_after--;
    }
#endif
    return flat_thread_join(thread);
}
static int runtime_task_create_internal(FlatRuntime *runtime, const FlatBytecode *bc,
                                        const FlatVmOptions *opts, const char *entry_label,
                                        FlatTaskState initial_state, const FlatValue *args,
                                        size_t arg_count, FlatCollectionPool *source_coll,
                                        FlatStringPool *source_pool, FlatFileTable *source_files,
                                        FlatEventSubscriptionId event_subscription_id,
                                        FlatTaskId *out_id);
static void runtime_event_handler_task_finished_locked(FlatRuntime *runtime,
                                                        FlatTaskRecord *task);
static int runtime_task_native_spawn(void *user, uint64_t parent_id, const FlatBytecode *bc,
                                     const char *entry_label, const FlatValue *args,
                                     size_t arg_count, FlatCollectionPool *source_coll,
                                     FlatStringPool *source_pool, FlatFileTable *source_files,
                                     uint64_t *out_id);
static int runtime_task_native_spawn_for(void *user, uint64_t parent_id, const FlatBytecode *bc,
                                         uint64_t source_id, const char *source_kind,
                                         const char *event_name, const char *handler_label,
                                         const FlatValue *args, size_t arg_count,
                                         FlatCollectionPool *source_coll, FlatStringPool *source_pool,
                                         FlatFileTable *source_files,
                                         const FlatEventSubscriptionOptions *options,
                                         uint64_t *out_subscription_id);
static int runtime_task_native_stop_for(void *user, uint64_t parent_id,
                                        uint64_t subscription_id);
static int runtime_task_native_wait(void *user, uint64_t waiter_id, uint64_t target_id);
static int runtime_task_native_state(void *user, uint64_t task_id, int *out_state);
static int runtime_task_native_result(void *user, uint64_t task_id,
                                      FlatVmResult *out_result, int *out_task_state);
static int runtime_task_native_channel_create(void *user, uint64_t task_id, uint64_t *out_channel_id);
static int runtime_task_native_channel_set(void *user, uint64_t task_id, uint64_t channel_id,
                                           const char *name, const FlatValue *value,
                                           FlatCollectionPool *source_coll,
                                           FlatStringPool *source_pool,
                                           FlatFileTable *source_files);
static int runtime_task_native_channel_var(void *user, uint64_t task_id, uint64_t channel_id,
                                           const char *name,
                                           FlatCollectionPool *destination_coll,
                                           FlatStringPool *destination_pool,
                                           FlatFileTable *destination_files,
                                           FlatValue *out_value);
static int runtime_task_native_channel_lock(void *user, uint64_t task_id, uint64_t channel_id);
static int runtime_task_native_channel_unlock(void *user, uint64_t task_id, uint64_t channel_id);
static int runtime_task_native_channel_revert(void *user, uint64_t task_id, uint64_t channel_id);
static int runtime_task_native_channel_destroy(void *user, uint64_t task_id, uint64_t channel_id);
static int runtime_task_native_socket_supported(void *user);
static int runtime_task_native_socket_connect(void *user, const char *url, long connect_timeout_ms,
                                              size_t max_frame_bytes, uint64_t *out_id);
static int runtime_task_native_socket_send(void *user, uint64_t socket_id, int opcode,
                                           const void *data, size_t length);
static int runtime_task_native_socket_close(void *user, uint64_t socket_id);
static FlatChannel *runtime_channel_acquire(FlatRuntime *runtime, FlatChannelId id);
static FlatChannel *runtime_channel_acquire_for_owner(FlatRuntime *runtime, FlatChannelId id,
                                                       FlatTaskId owner_id);
static int runtime_task_wait_channel(FlatRuntime *runtime, FlatTaskId task_id,
                                     FlatChannelId channel_id);
static int runtime_channel_add_user_locked(FlatChannel *channel, FlatTaskId task_id);
static void runtime_release_task_channel_refs(FlatRuntime *runtime, FlatTaskId task_id);
static void runtime_abort_task_channel_transactions_locked(FlatRuntime *runtime, FlatTaskId task_id);
static void runtime_release_task_channel_refs_locked(FlatRuntime *runtime, FlatTaskId task_id);
static int runtime_task_is_running(FlatRuntime *runtime, FlatTaskId task_id);
static int runtime_channel_destroy_owned(FlatRuntime *runtime, FlatChannelId id,
                                         FlatTaskId owner_id);
static int runtime_channel_create_owned(FlatRuntime *runtime, FlatTaskId owner_id,
                                        FlatChannelId *out_id);

static void runtime_lock(FlatRuntime *runtime) {
    if (runtime) (void)flat_mutex_lock(&runtime->scheduler_mutex);
}

static void runtime_unlock(FlatRuntime *runtime) {
    if (runtime) (void)flat_mutex_unlock(&runtime->scheduler_mutex);
}

/* Dispatch publication is serialized by scheduler_mutex. Queue consumers
 * release the queue mutex before acquiring scheduler_mutex. */
static int runtime_dispatch_enqueue_locked(FlatRuntime *runtime, FlatTaskRecord *task) {
    int rc;
    if (!runtime || !task) return -1;
    rc = flat_task_dispatch_enqueue(&task->dispatch);
    if (rc <= 0) return rc;
    if (flat_ready_queue_push(&runtime->ready_queue, task->id) != 0) {
        task->dispatch.queued = 0;
        return -1;
    }
    return 1;
}

static int runtime_dispatch_wake_locked(FlatRuntime *runtime, FlatTaskRecord *task) {
    int rc;
    if (!runtime || !task) return -1;
    rc = flat_task_dispatch_wake(&task->dispatch);
    if (rc <= 0) return rc;
    if (flat_ready_queue_push(&runtime->ready_queue, task->id) != 0) {
        task->dispatch.queued = 0;
        return -1;
    }
    return 1;
}

static void runtime_dispatch_dequeue_locked(FlatRuntime *runtime, FlatTaskRecord *task) {
    if (!runtime || !task) return;
    if (task->dispatch.queued)
        (void)flat_ready_queue_remove(&runtime->ready_queue, task->id);
    task->dispatch.queued = 0;
}

static FlatTaskRecord *runtime_find_task(const FlatRuntime *runtime, FlatTaskId id);
static int runtime_task_is_terminal(const FlatTaskRecord *task);

/* Caller holds scheduler_mutex. CREATED records are deliberately ignored by
 * DRAIN, but are included by CANCEL because CANCEL requests their cleanup. */
static void runtime_refresh_lifecycle_locked(FlatRuntime *runtime) {
    FlatTaskRecord *task;
    int runnable = 0, waiting = 0, owned = 0, pending_cancel = 0;
    if (!runtime || runtime->lifecycle == FLAT_RUNTIME_RUNNING ||
        runtime->lifecycle == FLAT_RUNTIME_STOPPED || runtime->schedule_active) return;
    for (task = runtime->tasks; task; task = task->next) {
        if (task->dispatch.worker_owned || task->cleanup_owned) owned = 1;
        if (runtime_task_is_terminal(task)) continue;
        if (runtime->lifecycle == FLAT_RUNTIME_CANCELLING) {
            pending_cancel = 1;
            continue;
        }
        if (task->state == FLAT_TASK_RUNNABLE || task->state == FLAT_TASK_RUNNING) runnable = 1;
        else if (task->state == FLAT_TASK_WAITING) waiting = 1;
        /* CREATED tasks are not part of the accepted DRAIN workload. */
    }
    if (owned || runnable || pending_cancel) return;
    if (runtime->lifecycle == FLAT_RUNTIME_DRAINING && waiting) return;
    if (runtime->lifecycle != FLAT_RUNTIME_STOPPED) {
        runtime->lifecycle = FLAT_RUNTIME_STOPPED;
        /* Wake idle workers so they can observe STOPPED and exit. */
        (void)flat_ready_queue_wake_all(&runtime->ready_queue);
    }
}

static int runtime_has_waiters(const FlatRuntime *runtime, FlatTaskId id) {
    const FlatTaskRecord *task;
    for (task = runtime ? runtime->tasks : NULL; task; task = task->next) {
        if (task->state == FLAT_TASK_WAITING && task->waiting_on == id) return 1;
    }
    return 0;
}

/* Caller holds scheduler_mutex. */
static void runtime_wake_waiters_locked(FlatRuntime *runtime, FlatTaskId target_id) {
    FlatTaskRecord *task;
    for (task = runtime->tasks; task; task = task->next) {
        if (!task->cleanup_owned && task->state == FLAT_TASK_WAITING &&
            task->waiting_on == target_id) {
            task->waiting_on = 0;
            task->waiting_channel_id = 0;
            task->state = FLAT_TASK_RUNNABLE;
            (void)runtime_dispatch_wake_locked(runtime, task);
        }
    }
}

/* Caller holds scheduler_mutex. */
static void runtime_wake_channel_waiters_locked(FlatRuntime *runtime, FlatChannelId channel_id) {
    FlatTaskRecord *task;
    if (!runtime || channel_id == 0) return;
    for (task = runtime->tasks; task; task = task->next) {
        if (!task->cleanup_owned && task->state == FLAT_TASK_WAITING &&
            task->waiting_channel_id == channel_id) {
            task->waiting_on = 0;
            task->waiting_channel_id = 0;
            task->state = FLAT_TASK_RUNNABLE;
            (void)runtime_dispatch_wake_locked(runtime, task);
        }
    }
}

static void runtime_wake_channel_waiters(FlatRuntime *runtime, FlatChannelId channel_id) {
    if (!runtime || channel_id == 0) return;
    runtime_lock(runtime);
    runtime_wake_channel_waiters_locked(runtime, channel_id);
    runtime_unlock(runtime);
}

/* Each task has at most one outgoing wait edge. Following the target chain is
 * therefore sufficient to reject a newly-created cycle without allocating. */
static int runtime_wait_would_cycle(const FlatRuntime *runtime, FlatTaskId waiter_id,
                                    FlatTaskId target_id) {
    FlatTaskId cursor = target_id;
    size_t visited = 0;
    while (cursor != 0 && visited <= runtime->task_count) {
        FlatTaskRecord *task;
        if (cursor == waiter_id) return 1;
        task = runtime_find_task(runtime, cursor);
        if (!task || task->state != FLAT_TASK_WAITING) return 0;
        cursor = task->waiting_on;
        visited++;
    }
    return cursor != 0;
}

static void runtime_log_channel(FlatTaskId task_id, FlatChannelId channel_id,
                                const char *event, const char *name,
                                const char *value_type, const char *action,
                                const char *reason) {
    FlatRuntimeLogEvent log_event;
    if (!flat_runtime_log_enabled(FLAT_RUNTIME_LOG_CHANNELS)) return;
    memset(&log_event, 0, sizeof log_event);
    log_event.category = FLAT_RUNTIME_LOG_CHANNELS;
    log_event.task_id = task_id && task_id <= INT64_MAX ? (int64_t)task_id : -1;
    log_event.channel_id = channel_id && channel_id <= INT64_MAX ? (int64_t)channel_id : -1;
    log_event.event = event;
    log_event.name = name;
    log_event.value_type = value_type;
    log_event.action = action;
    log_event.reason = reason;
    flat_runtime_log_emit(&log_event);
}

static const char *runtime_value_type_name(const FlatValue *value) {
    if (!value) return "unknown";
    switch (value->tag) {
        case VAL_BOOL: return "bool";
        case VAL_NUMBER: return "number";
        case VAL_STR_INLINE:
        case VAL_STR_REF: return "string";
        case VAL_LIST: return "list";
        case VAL_OBJECT: return "object";
        case VAL_FILE: return "file";
        case VAL_NULL: return "null";
        default: return "empty";
    }
}

/* A task holding an explicit Channel transaction keeps cooperative priority until
 * it commits or reverts. This makes a multi-instruction transaction atomic with
 * respect to other scheduler tasks, even when the VM quantum is very small. */
static int runtime_task_owns_channel_transaction(FlatRuntime *runtime, FlatTaskId task_id) {
    FlatChannel *channel;
    if (!runtime || task_id == 0) return 0;
    for (channel = runtime->channels; channel; channel = channel->next) {
        int owns;
        flat_channel_lock(channel);
        owns = channel->transaction_locked && channel->transaction_owner == task_id;
        flat_channel_unlock(channel);
        if (owns) return 1;
    }
    return 0;
}

/* Caller holds scheduler_mutex. */
static void runtime_abort_task_channel_transactions_locked(FlatRuntime *runtime, FlatTaskId task_id) {
    FlatChannel *channel;
    if (!runtime || task_id == 0) return;
    for (channel = runtime->channels; channel; channel = channel->next) {
        int aborted = 0;
        FlatChannelId channel_id = channel->id;
        flat_channel_lock(channel);
        if (channel->transaction_locked && channel->transaction_owner == task_id)
            aborted = flat_channel_tx_abort(channel, task_id) == 0;
        flat_channel_unlock(channel);
        if (aborted) runtime_wake_channel_waiters_locked(runtime, channel_id);
    }
}

static FlatTaskRecord *runtime_find_task(const FlatRuntime *runtime, FlatTaskId id) {
    FlatTaskRecord *task;
    if (!runtime || id == 0) return NULL;
    for (task = runtime->tasks; task; task = task->next) {
        if (task->id == id) return task;
    }
    return NULL;
}

static FlatChannel *runtime_find_channel(const FlatRuntime *runtime, FlatChannelId id) {
    FlatChannel *channel;
    if (!runtime || id == 0) return NULL;
    for (channel = runtime->channels; channel; channel = channel->next) {
        if (channel->id == id) return channel;
    }
    return NULL;
}


static void runtime_event_node_destroy(FlatRuntimeEventNode *node) {
    if (!node) return;
    if (node->coll && node->pool) flat_value_release(node->coll, node->pool, &node->value);
    if (node->coll) flat_coll_destroy(node->coll);
    if (node->pool) flat_pool_destroy(node->pool);
    free(node);
}

static void runtime_event_forget_open_locked(FlatRuntime *runtime, uint64_t source_id);

/* WebSocket connections are created before FlatL can subscribe with
 * task.spawnFor. Retain the one-shot open notification so a subscription
 * registered immediately after wsConnect receives it exactly once. */
static int runtime_event_queue_open_replay_locked(FlatRuntime *runtime,
                                                   FlatRuntimeEventSubscription *sub) {
    FlatRuntimeOpenSocketEvent *open_event;
    FlatRuntimeEventNode *node;
    if (!runtime || !sub || strcmp(sub->source_kind, "socket") != 0 ||
        strcmp(sub->event_name, "open") != 0) return 0;
    for (open_event = runtime->open_socket_events; open_event; open_event = open_event->next)
        if (open_event->source_id == sub->source_id) break;
    if (!open_event) return 0;
    node = (FlatRuntimeEventNode *)calloc(1, sizeof *node);
    if (!node) return -1;
    node->coll = flat_coll_create();
    node->pool = flat_pool_create();
    if (!node->coll || !node->pool || flat_value_set_null(&node->value) != 0) {
        runtime_event_node_destroy(node);
        return -1;
    }
    sub->head = sub->tail = node;
    sub->queued = 1;
    runtime_event_forget_open_locked(runtime, sub->source_id);
    return 0;
}

static int runtime_event_remember_open_locked(FlatRuntime *runtime, uint64_t source_id) {
    FlatRuntimeOpenSocketEvent *entry;
    for (entry = runtime->open_socket_events; entry; entry = entry->next)
        if (entry->source_id == source_id) return 0;
    entry = (FlatRuntimeOpenSocketEvent *)calloc(1, sizeof *entry);
    if (!entry) return -1;
    entry->source_id = source_id;
    entry->next = runtime->open_socket_events;
    runtime->open_socket_events = entry;
    return 0;
}

static void runtime_event_forget_open_locked(FlatRuntime *runtime, uint64_t source_id) {
    FlatRuntimeOpenSocketEvent **link, *entry;
    for (link = &runtime->open_socket_events; *link; link = &(*link)->next) {
        if ((*link)->source_id != source_id) continue;
        entry = *link;
        *link = entry->next;
        free(entry);
        return;
    }
}

#define FLAT_RUNTIME_SOCKET_PRE_SUBSCRIBE_CAPACITY 256
#define FLAT_RUNTIME_SOCKET_PRE_SUBSCRIBE_TOTAL_CAPACITY 4096

static FlatRuntimePendingSocketEvents *runtime_find_pending_socket_events_locked(
        FlatRuntime *runtime, uint64_t source_id) {
    FlatRuntimePendingSocketEvents *entry;
    for (entry = runtime->pending_socket_events; entry; entry = entry->next)
        if (entry->source_id == source_id) return entry;
    return NULL;
}

static int runtime_event_queue_receive_replay_locked(FlatRuntime *runtime,
                                                       FlatRuntimeEventSubscription *sub) {
    FlatRuntimePendingSocketEvents **link, *entry;
    FlatRuntimeEventNode *node;
    size_t keep, skip = 0, i;
    if (!runtime || !sub || strcmp(sub->source_kind, "socket") != 0 ||
        strcmp(sub->event_name, "receive") != 0) return 0;
    for (link = &runtime->pending_socket_events; *link; link = &(*link)->next)
        if ((*link)->source_id == sub->source_id) break;
    if (!*link) return 0;
    entry = *link;
    if (entry->count > sub->queue_capacity) {
        if (sub->overflow_policy == FLAT_EVENT_OVERFLOW_WAIT ||
            sub->overflow_policy == FLAT_EVENT_OVERFLOW_ERROR) return -1;
        if (sub->overflow_policy == FLAT_EVENT_OVERFLOW_DROP_OLDEST)
            skip = entry->count - sub->queue_capacity;
        keep = sub->queue_capacity;
    } else {
        keep = entry->count;
    }
    node = entry->head;
    for (i = 0; i < skip && node; ++i) {
        FlatRuntimeEventNode *next = node->next;
        runtime_event_node_destroy(node);
        node = next;
    }
    if (sub->overflow_policy == FLAT_EVENT_OVERFLOW_DROP_NEWEST &&
        entry->count > sub->queue_capacity) {
        FlatRuntimeEventNode *tail = node;
        for (i = 1; i < keep && tail; ++i) tail = tail->next;
        if (tail) {
            FlatRuntimeEventNode *discard = tail->next;
            tail->next = NULL;
            while (discard) {
                FlatRuntimeEventNode *next = discard->next;
                runtime_event_node_destroy(discard);
                discard = next;
            }
            sub->tail = tail;
        }
    } else {
        sub->tail = entry->tail;
    }
    sub->head = node;
    sub->queued = keep;
    if (runtime->pending_socket_event_count >= entry->count)
        runtime->pending_socket_event_count -= entry->count;
    else
        runtime->pending_socket_event_count = 0;
    *link = entry->next;
    free(entry);
    return 0;
}

static int runtime_event_remember_receive_locked(FlatRuntime *runtime, uint64_t source_id,
                                                  const FlatValue *value,
                                                  FlatCollectionPool *source_coll,
                                                  FlatStringPool *source_pool,
                                                  FlatFileTable *source_files) {
    FlatRuntimePendingSocketEvents *entry;
    FlatRuntimeEventNode *node;
    if (!runtime || !value || !source_coll || !source_pool) return -1;
    entry = runtime_find_pending_socket_events_locked(runtime, source_id);
    if ((entry && entry->count >= FLAT_RUNTIME_SOCKET_PRE_SUBSCRIBE_CAPACITY) ||
        runtime->pending_socket_event_count >= FLAT_RUNTIME_SOCKET_PRE_SUBSCRIBE_TOTAL_CAPACITY)
        return -3;
    node = (FlatRuntimeEventNode *)calloc(1, sizeof *node);
    if (!node) return -1;
    node->coll = flat_coll_create();
    node->pool = flat_pool_create();
    if (!node->coll || !node->pool ||
        flat_value_deep_copy_between(value, &node->value, source_coll, source_pool,
                                     source_files, node->coll, node->pool, NULL) != 0) {
        runtime_event_node_destroy(node);
        return -1;
    }
    flat_value_retain(node->coll, node->pool, &node->value);
    if (!entry) {
        entry = (FlatRuntimePendingSocketEvents *)calloc(1, sizeof *entry);
        if (!entry) { runtime_event_node_destroy(node); return -1; }
        entry->source_id = source_id;
        entry->next = runtime->pending_socket_events;
        runtime->pending_socket_events = entry;
    }
    if (entry->tail) entry->tail->next = node;
    else entry->head = node;
    entry->tail = node;
    entry->count++;
    runtime->pending_socket_event_count++;
    return 0;
}

static void runtime_event_subscription_destroy(FlatRuntimeEventSubscription *sub) {
    FlatRuntimeEventNode *node;
    if (!sub) return;
    node = sub->head;
    while (node) {
        FlatRuntimeEventNode *next = node->next;
        runtime_event_node_destroy(node);
        node = next;
    }
    if (sub->bound_args && sub->bound_coll && sub->bound_pool) {
        size_t i;
        for (i = 0; i < sub->bound_arg_count; ++i)
            flat_value_release(sub->bound_coll, sub->bound_pool, &sub->bound_args[i]);
    }
    free(sub->bound_args);
    if (sub->bound_coll) flat_coll_destroy(sub->bound_coll);
    if (sub->bound_pool) flat_pool_destroy(sub->bound_pool);
    if (sub->owns_handler_bytecode) flat_fb_free(&sub->handler_bytecode);
    free(sub);
}

static int runtime_event_name_valid(const char *s, size_t max_len) {
    size_t n;
    if (!s || !s[0]) return 0;
    n = strlen(s);
    return n < max_len;
}

int flat_runtime_event_subscribe(FlatRuntime *runtime, uint64_t source_id,
                                 const char *source_kind, const char *event_name,
                                 FlatEventSubscriptionId *out_subscription_id) {
    FlatRuntimeEventSubscription *sub;
    if (!runtime || source_id == 0 || !out_subscription_id ||
        !runtime_event_name_valid(source_kind, sizeof sub->source_kind) ||
        !runtime_event_name_valid(event_name, sizeof sub->event_name)) return -1;
    sub = (FlatRuntimeEventSubscription *)calloc(1, sizeof *sub);
    if (!sub) return -1;
    sub->source_id = source_id;
    sub->queue_capacity = FLAT_EVENT_DEFAULT_QUEUE_CAPACITY;
    sub->overflow_policy = FLAT_EVENT_OVERFLOW_WAIT;
    snprintf(sub->source_kind, sizeof sub->source_kind, "%s", source_kind);
    snprintf(sub->event_name, sizeof sub->event_name, "%s", event_name);
    runtime_lock(runtime);
    if (runtime->lifecycle != FLAT_RUNTIME_RUNNING || runtime->next_event_subscription_id == 0) {
        runtime_unlock(runtime);
        free(sub);
        return -1;
    }
    sub->id = runtime->next_event_subscription_id++;
    sub->next = runtime->event_subscriptions;
    runtime->event_subscriptions = sub;
    if (runtime_event_queue_open_replay_locked(runtime, sub) != 0 ||
        runtime_event_queue_receive_replay_locked(runtime, sub) != 0) {
        runtime->event_subscriptions = sub->next;
        runtime_unlock(runtime);
        runtime_event_subscription_destroy(sub);
        return -1;
    }
    runtime->event_subscription_count++;
    *out_subscription_id = sub->id;
    runtime_unlock(runtime);
    return 0;
}

int flat_runtime_event_subscribe_handler_config(FlatRuntime *runtime, uint64_t source_id,
                                                 const char *source_kind, const char *event_name,
                                         const FlatBytecode *bc, const char *handler_label,
                                         const FlatValue *bound_args, size_t bound_arg_count,
                                         FlatCollectionPool *source_coll,
                                         FlatStringPool *source_pool,
                                         FlatFileTable *source_files,
                                         const FlatEventSubscriptionOptions *options,
                                         FlatEventSubscriptionId *out_subscription_id) {
    FlatRuntimeEventSubscription *sub;
    FlatCollectionPool *copy_coll = NULL;
    FlatStringPool *copy_pool = NULL;
    FlatValue *copies = NULL;
    size_t bi, i, expected = 0;
    FlatEventSubscriptionOptions defaults;
    if (!options) {
        defaults.mode = FLAT_EVENT_DISPATCH_PARALLEL;
        defaults.max_active = SIZE_MAX;
        defaults.queue_capacity = FLAT_EVENT_DEFAULT_QUEUE_CAPACITY;
        defaults.overflow = FLAT_EVENT_OVERFLOW_WAIT;
        options = &defaults;
    }
    if (!runtime || source_id == 0 || !bc || !handler_label || handler_label[0] != '#' ||
        !out_subscription_id || (bound_arg_count && (!bound_args || !source_coll || !source_pool)) ||
        bound_arg_count >= FGIR_MAX_PARAMS ||
        (options->mode != FLAT_EVENT_DISPATCH_PARALLEL && options->mode != FLAT_EVENT_DISPATCH_SERIAL) ||
        options->max_active == 0 || options->queue_capacity == 0 ||
        (options->overflow != FLAT_EVENT_OVERFLOW_WAIT &&
         options->overflow != FLAT_EVENT_OVERFLOW_DROP_OLDEST &&
         options->overflow != FLAT_EVENT_OVERFLOW_DROP_NEWEST &&
         options->overflow != FLAT_EVENT_OVERFLOW_ERROR)) return -1;
    for (bi = 0; bi < bc->block_count; ++bi) {
        if (bc->block_labels && strcmp(bc->block_labels[bi], handler_label) == 0) {
            expected = bc->block_params ? bc->block_params[bi].param_count : 0;
            break;
        }
    }
    if (bi >= bc->block_count || expected != bound_arg_count + 1 ||
        strlen(handler_label) >= sizeof sub->handler_label) return -2;
    {
        FlatBytecode bytecode_copy;
        memset(&bytecode_copy, 0, sizeof bytecode_copy);
        if (!bc->data || bc->size == 0 || flat_fb_load_buffer(bc->data, bc->size, &bytecode_copy) != 0)
            return -1;
        /* Ownership is transferred to the subscription after it is created. */
        sub = (FlatRuntimeEventSubscription *)calloc(1, sizeof *sub);
        if (!sub) { flat_fb_free(&bytecode_copy); return -1; }
        sub->handler_bytecode = bytecode_copy;
        sub->owns_handler_bytecode = 1;
        sub->handler_bc = &sub->handler_bytecode;
    }
    if (bound_arg_count) {
        copy_coll = flat_coll_create();
        copy_pool = flat_pool_create();
        copies = (FlatValue *)calloc(bound_arg_count, sizeof *copies);
        if (!copy_coll || !copy_pool || !copies) goto fail;
        for (i = 0; i < bound_arg_count; ++i) {
            if (flat_value_deep_copy_between(&bound_args[i], &copies[i], source_coll,
                                             source_pool, source_files, copy_coll,
                                             copy_pool, NULL) != 0) goto fail;
        }
    }
    if (!runtime_event_name_valid(source_kind, sizeof sub->source_kind) ||
        !runtime_event_name_valid(event_name, sizeof sub->event_name)) goto fail;
    sub->source_id = source_id;
    sub->dispatch_mode = options->mode;
    sub->max_active = options->mode == FLAT_EVENT_DISPATCH_SERIAL ? 1 : options->max_active;
    sub->queue_capacity = options->queue_capacity;
    sub->overflow_policy = options->overflow;
    snprintf(sub->source_kind, sizeof sub->source_kind, "%s", source_kind);
    snprintf(sub->event_name, sizeof sub->event_name, "%s", event_name);
    snprintf(sub->handler_label, sizeof sub->handler_label, "%s", handler_label);
    sub->bound_args = copies;
    sub->bound_arg_count = bound_arg_count;
    sub->bound_coll = copy_coll;
    sub->bound_pool = copy_pool;
    copies = NULL; copy_coll = NULL; copy_pool = NULL;
    runtime_lock(runtime);
    if (runtime->lifecycle != FLAT_RUNTIME_RUNNING || runtime->next_event_subscription_id == 0) {
        runtime_unlock(runtime);
        goto fail_sub;
    }
    sub->id = runtime->next_event_subscription_id++;
    sub->next = runtime->event_subscriptions;
    runtime->event_subscriptions = sub;
    if (runtime_event_queue_open_replay_locked(runtime, sub) != 0 ||
        runtime_event_queue_receive_replay_locked(runtime, sub) != 0) {
        runtime->event_subscriptions = sub->next;
        runtime_unlock(runtime);
        runtime_event_subscription_destroy(sub);
        return -1;
    }
    runtime->event_subscription_count++;
    *out_subscription_id = sub->id;
    runtime_unlock(runtime);
    /* A handler subscription may have received replayed open/receive events.
     * Wake worker-mode schedulers after releasing the runtime lock. */
    (void)flat_ready_queue_wake_all(&runtime->ready_queue);
    return 0;
fail_sub:
    runtime_event_subscription_destroy(sub);
    return -1;
fail:
    if (sub) runtime_event_subscription_destroy(sub);
    if (copies) for (i = 0; i < bound_arg_count; ++i)
        flat_value_release(copy_coll, copy_pool, &copies[i]);
    free(copies);
    if (copy_coll) flat_coll_destroy(copy_coll);
    if (copy_pool) flat_pool_destroy(copy_pool);
    return -1;
}

int flat_runtime_event_subscribe_handler_mode(FlatRuntime *runtime, uint64_t source_id,
                                              const char *source_kind, const char *event_name,
                                              const FlatBytecode *bc, const char *handler_label,
                                              const FlatValue *bound_args, size_t bound_arg_count,
                                              FlatCollectionPool *source_coll,
                                              FlatStringPool *source_pool,
                                              FlatFileTable *source_files,
                                              FlatEventDispatchMode mode,
                                              FlatEventSubscriptionId *out_subscription_id) {
    return flat_runtime_event_subscribe_handler_config(runtime, source_id, source_kind, event_name,
        bc, handler_label, bound_args, bound_arg_count, source_coll, source_pool, source_files,
        &(FlatEventSubscriptionOptions){ mode, SIZE_MAX, FLAT_EVENT_DEFAULT_QUEUE_CAPACITY, FLAT_EVENT_OVERFLOW_WAIT }, out_subscription_id);
}

int flat_runtime_event_subscribe_handler(FlatRuntime *runtime, uint64_t source_id,
                                         const char *source_kind, const char *event_name,
                                         const FlatBytecode *bc, const char *handler_label,
                                         const FlatValue *bound_args, size_t bound_arg_count,
                                         FlatCollectionPool *source_coll,
                                         FlatStringPool *source_pool,
                                         FlatFileTable *source_files,
                                         FlatEventSubscriptionId *out_subscription_id) {
    return flat_runtime_event_subscribe_handler_config(runtime, source_id, source_kind, event_name,
        bc, handler_label, bound_args, bound_arg_count, source_coll, source_pool, source_files,
        &(FlatEventSubscriptionOptions){ FLAT_EVENT_DISPATCH_PARALLEL, SIZE_MAX, FLAT_EVENT_DEFAULT_QUEUE_CAPACITY, FLAT_EVENT_OVERFLOW_WAIT }, out_subscription_id);
}

int flat_runtime_event_subscribe_handler_options(FlatRuntime *runtime, uint64_t source_id,
                                                 const char *source_kind, const char *event_name,
                                                 const FlatBytecode *bc, const char *handler_label,
                                                 const FlatValue *bound_args, size_t bound_arg_count,
                                                 FlatCollectionPool *source_coll,
                                                 FlatStringPool *source_pool,
                                                 FlatFileTable *source_files,
                                                 FlatEventDispatchMode mode, size_t max_active,
                                                 FlatEventSubscriptionId *out_subscription_id) {
    FlatEventSubscriptionOptions options;
    options.mode = mode;
    options.max_active = max_active;
    options.queue_capacity = FLAT_EVENT_DEFAULT_QUEUE_CAPACITY;
    options.overflow = FLAT_EVENT_OVERFLOW_WAIT;
    return flat_runtime_event_subscribe_handler_config(runtime, source_id, source_kind, event_name,
        bc, handler_label, bound_args, bound_arg_count, source_coll, source_pool, source_files,
        &options, out_subscription_id);
}

int flat_runtime_event_unsubscribe(FlatRuntime *runtime,
                                   FlatEventSubscriptionId subscription_id) {
    FlatRuntimeEventSubscription **link, *sub;
    if (!runtime || subscription_id == 0) return -1;
    runtime_lock(runtime);
    for (link = &runtime->event_subscriptions; *link; link = &(*link)->next) {
        if ((*link)->id == subscription_id) break;
    }
    sub = *link;
    if (!sub || sub->dispatching || sub->handler_dispatching || sub->active_handlers != 0) {
        runtime_unlock(runtime);
        return -1;
    }
    *link = sub->next;
    if (runtime->event_subscription_count) runtime->event_subscription_count--;
    runtime_unlock(runtime);
    runtime_event_subscription_destroy(sub);
    return 0;
}

int flat_runtime_event_publish(FlatRuntime *runtime, uint64_t source_id,
                               const char *source_kind, const char *event_name,
                               const FlatValue *value,
                               FlatCollectionPool *source_coll,
                               FlatStringPool *source_pool,
                               FlatFileTable *source_files) {
    FlatRuntimeEventSubscription *sub;
    FlatRuntimeEventNode **prepared = NULL;
    size_t prepared_count = 0;
    size_t matched = 0;
    if (!runtime || source_id == 0 || !source_kind || !event_name || !value ||
        !source_coll || !source_pool) return -1;
    runtime_lock(runtime);
    if (runtime->lifecycle != FLAT_RUNTIME_RUNNING) {
        runtime_unlock(runtime);
        return -1;
    }
    if (strcmp(source_kind, "socket") == 0 && strcmp(event_name, "close") == 0) {
        runtime_event_forget_open_locked(runtime, source_id);
        /* A closed socket can no longer produce events. Keep its subscriptions
         * until queued close notifications and already-started handlers drain. */
        for (sub = runtime->event_subscriptions; sub; sub = sub->next) {
            if (sub->source_id == source_id && strcmp(sub->source_kind, "socket") == 0)
                sub->source_closed = 1;
        }
    }
    /* Preflight all matching subscriptions before allocating or mutating any
     * queue. WAIT/ERROR preserve atomic fan-out; DROP_NEWEST skips only the
     * overflowing subscriber; DROP_OLDEST replaces its oldest queued item. */
    for (sub = runtime->event_subscriptions; sub; sub = sub->next) {
        if (sub->source_id != source_id || strcmp(sub->source_kind, source_kind) != 0 ||
            strcmp(sub->event_name, event_name) != 0) continue;
        matched++;
        if (sub->queued >= sub->queue_capacity &&
            (sub->overflow_policy == FLAT_EVENT_OVERFLOW_WAIT ||
             sub->overflow_policy == FLAT_EVENT_OVERFLOW_ERROR)) {
            int overflow_rc = sub->overflow_policy == FLAT_EVENT_OVERFLOW_WAIT ? -2 : -3;
            runtime_unlock(runtime);
            return overflow_rc;
        }
        if (sub->queued < sub->queue_capacity ||
            sub->overflow_policy != FLAT_EVENT_OVERFLOW_DROP_NEWEST) prepared_count++;
    }
    if (matched == 0) {
        if (strcmp(source_kind, "socket") == 0 && strcmp(event_name, "open") == 0 &&
            runtime_event_remember_open_locked(runtime, source_id) != 0) {
            runtime_unlock(runtime);
            return -1;
        }
        if (strcmp(source_kind, "socket") == 0 && strcmp(event_name, "receive") == 0) {
            int remember_rc = runtime_event_remember_receive_locked(runtime, source_id, value,
                                                                    source_coll, source_pool,
                                                                    source_files);
            runtime_unlock(runtime);
            return remember_rc;
        }
        runtime_unlock(runtime);
        return 0;
    }
    if (prepared_count) {
        prepared = (FlatRuntimeEventNode **)calloc(prepared_count, sizeof *prepared);
        if (!prepared) {
            runtime_unlock(runtime);
            return -1;
        }
    }
    prepared_count = 0;
    for (sub = runtime->event_subscriptions; sub; sub = sub->next) {
        FlatRuntimeEventNode *node;
        if (sub->source_id != source_id || strcmp(sub->source_kind, source_kind) != 0 ||
            strcmp(sub->event_name, event_name) != 0 ||
            (sub->queued >= sub->queue_capacity &&
             sub->overflow_policy == FLAT_EVENT_OVERFLOW_DROP_NEWEST)) continue;
        node = (FlatRuntimeEventNode *)calloc(1, sizeof *node);
        if (!node) goto publish_fail;
        node->coll = flat_coll_create();
        node->pool = flat_pool_create();
        if (!node->coll || !node->pool ||
            flat_value_deep_copy_between(value, &node->value, source_coll, source_pool,
                                         source_files, node->coll, node->pool, NULL) != 0) {
            runtime_event_node_destroy(node);
            goto publish_fail;
        }
        flat_value_retain(node->coll, node->pool, &node->value);
        prepared[prepared_count++] = node;
    }
    prepared_count = 0;
    for (sub = runtime->event_subscriptions; sub; sub = sub->next) {
        FlatRuntimeEventNode *node;
        if (sub->source_id != source_id || strcmp(sub->source_kind, source_kind) != 0 ||
            strcmp(sub->event_name, event_name) != 0 ||
            (sub->queued >= sub->queue_capacity &&
             sub->overflow_policy == FLAT_EVENT_OVERFLOW_DROP_NEWEST)) continue;
        node = prepared[prepared_count];
        prepared[prepared_count++] = NULL;
        if (sub->queued >= sub->queue_capacity &&
            sub->overflow_policy == FLAT_EVENT_OVERFLOW_DROP_OLDEST) {
            FlatRuntimeEventNode *oldest = sub->head;
            if (oldest) {
                sub->head = oldest->next;
                if (!sub->head) sub->tail = NULL;
                if (sub->queued) sub->queued--;
                runtime_event_node_destroy(oldest);
            }
        }
        if (sub->tail) sub->tail->next = node;
        else sub->head = node;
        sub->tail = node;
        sub->queued++;
    }
    free(prepared);
    runtime_unlock(runtime);
    /* External producers (network reactors, host callbacks) can publish while
     * worker threads are asleep. The event broker is scheduler input, so make
     * the worker condition observable only after the queue mutation is visible. */
    (void)flat_ready_queue_wake_all(&runtime->ready_queue);
    return (int)matched;
publish_fail:
    runtime_unlock(runtime);
    if (prepared) {
        size_t i;
        for (i = 0; i < prepared_count; ++i) runtime_event_node_destroy(prepared[i]);
    }
    free(prepared);
    return -1;
}

int flat_runtime_event_pending(FlatRuntime *runtime,
                               FlatEventSubscriptionId subscription_id) {
    FlatRuntimeEventSubscription *sub;
    int queued;
    if (!runtime || subscription_id == 0) return -1;
    runtime_lock(runtime);
    for (sub = runtime->event_subscriptions; sub; sub = sub->next)
        if (sub->id == subscription_id) break;
    if (!sub) {
        runtime_unlock(runtime);
        return -1;
    }
    queued = sub->queued > (size_t)INT_MAX ? INT_MAX : (int)sub->queued;
    runtime_unlock(runtime);
    return queued;
}

int flat_runtime_event_next(FlatRuntime *runtime,
                            FlatEventSubscriptionId subscription_id,
                            FlatCollectionPool *destination_coll,
                            FlatStringPool *destination_pool,
                            FlatFileTable *destination_files,
                            FlatValue *out_value) {
    FlatRuntimeEventSubscription *sub;
    FlatRuntimeEventNode *node;
    int rc;
    if (!runtime || subscription_id == 0 || !destination_coll || !destination_pool || !out_value)
        return -1;
    runtime_lock(runtime);
    for (sub = runtime->event_subscriptions; sub; sub = sub->next)
        if (sub->id == subscription_id) break;
    if (!sub) {
        runtime_unlock(runtime);
        return -1;
    }
    node = sub->head;
    if (!node) {
        runtime_unlock(runtime);
        return 0;
    }
    rc = flat_value_deep_copy_between(&node->value, out_value, node->coll, node->pool, NULL,
                                      destination_coll, destination_pool, destination_files);
    if (rc != 0) {
        runtime_unlock(runtime);
        return -1;
    }
    sub->head = node->next;
    if (!sub->head) sub->tail = NULL;
    if (sub->queued) sub->queued--;
    runtime_unlock(runtime);
    runtime_event_node_destroy(node);
    return 1;
}

FlatRuntime *flat_runtime_create(const FlatHostConfig *config) {
    FlatRuntime *runtime = (FlatRuntime *)calloc(1, sizeof *runtime);
    if (!runtime) return NULL;
    if (flat_mutex_init(&runtime->scheduler_mutex) != 0) {
        free(runtime);
        return NULL;
    }
#if FLAT_WITH_WEBSOCKET
    if (flat_mutex_init(&runtime->socket_mutex) != 0) {
        (void)flat_mutex_destroy(&runtime->scheduler_mutex);
        free(runtime);
        return NULL;
    }
#endif
#if FLAT_WITH_WS_SERVER
    runtime->io_reactor = flat_io_reactor_create();
    if (!runtime->io_reactor) {
#if FLAT_WITH_WEBSOCKET
        (void)flat_mutex_destroy(&runtime->socket_mutex);
#endif
        (void)flat_mutex_destroy(&runtime->scheduler_mutex);
        free(runtime);
        return NULL;
    }
    runtime->ws_server_registry = flat_ws_server_registry_create();
    if (!runtime->ws_server_registry) {
        (void)flat_io_reactor_destroy(runtime->io_reactor);
        runtime->io_reactor = NULL;
#if FLAT_WITH_WEBSOCKET
        (void)flat_mutex_destroy(&runtime->socket_mutex);
#endif
        (void)flat_mutex_destroy(&runtime->scheduler_mutex);
        free(runtime);
        return NULL;
    }
#endif
    if (flat_ready_queue_init(&runtime->ready_queue, 16) != 0) {
#if FLAT_WITH_WS_SERVER
        (void)flat_ws_server_registry_destroy(runtime->ws_server_registry);
        (void)flat_io_reactor_destroy(runtime->io_reactor);
#endif
#if FLAT_WITH_WEBSOCKET
        (void)flat_mutex_destroy(&runtime->socket_mutex);
#endif
        (void)flat_mutex_destroy(&runtime->scheduler_mutex);
        free(runtime);
        return NULL;
    }
    runtime->next_task_id = 1;
    runtime->next_event_subscription_id = 1;
    runtime->lifecycle = FLAT_RUNTIME_RUNNING;
    runtime->next_channel_id = 1;
#if FLAT_WITH_WEBSOCKET
    runtime->next_socket_id = 1;
#endif

    if (config) {
        if (flat_env_context_clone(&runtime->host.env, &config->env) != 0) {
            (void)flat_ready_queue_close(&runtime->ready_queue);
            (void)flat_ready_queue_destroy(&runtime->ready_queue);
#if FLAT_WITH_WS_SERVER
            (void)flat_ws_server_registry_destroy(runtime->ws_server_registry);
            (void)flat_io_reactor_destroy(runtime->io_reactor);
#endif
#if FLAT_WITH_WEBSOCKET
            (void)flat_mutex_destroy(&runtime->socket_mutex);
#endif
            (void)flat_mutex_destroy(&runtime->scheduler_mutex);
            free(runtime);
            return NULL;
        }
        runtime->host.path = config->path;
        if (runtime->host.path.sep != '/' && runtime->host.path.sep != '\\') {
            flat_host_config_destroy(&runtime->host);
            (void)flat_ready_queue_close(&runtime->ready_queue);
            (void)flat_ready_queue_destroy(&runtime->ready_queue);
#if FLAT_WITH_WS_SERVER
            (void)flat_ws_server_registry_destroy(runtime->ws_server_registry);
            (void)flat_io_reactor_destroy(runtime->io_reactor);
#endif
#if FLAT_WITH_WEBSOCKET
            (void)flat_mutex_destroy(&runtime->socket_mutex);
#endif
            (void)flat_mutex_destroy(&runtime->scheduler_mutex);
            free(runtime);
            return NULL;
        }
    } else if (flat_host_config_capture_staging(&runtime->host) != 0) {
        flat_host_config_destroy(&runtime->host);
        (void)flat_ready_queue_close(&runtime->ready_queue);
        (void)flat_ready_queue_destroy(&runtime->ready_queue);
#if FLAT_WITH_WS_SERVER
        (void)flat_ws_server_registry_destroy(runtime->ws_server_registry);
        (void)flat_io_reactor_destroy(runtime->io_reactor);
#endif
#if FLAT_WITH_WEBSOCKET
        (void)flat_mutex_destroy(&runtime->socket_mutex);
#endif
        (void)flat_mutex_destroy(&runtime->scheduler_mutex);
        free(runtime);
        return NULL;
    }
    return runtime;
}

int flat_runtime_shutdown_request(FlatRuntime *runtime, FlatRuntimeShutdownMode mode) {
    FlatTaskRecord *task;
    if (!runtime || (mode != FLAT_RUNTIME_SHUTDOWN_DRAIN &&
                     mode != FLAT_RUNTIME_SHUTDOWN_CANCEL)) return -1;
    runtime_lock(runtime);
    if (runtime->worker_starting) {
        runtime_unlock(runtime);
        return -1;
    }
    if (runtime->lifecycle == FLAT_RUNTIME_STOPPED && mode == FLAT_RUNTIME_SHUTDOWN_DRAIN) {
        runtime_unlock(runtime);
        return 0;
    }
    if (mode == FLAT_RUNTIME_SHUTDOWN_CANCEL) {
        int has_nonterminal = 0;
        for (task = runtime->tasks; task; task = task->next) {
            if (!runtime_task_is_terminal(task)) {
                task->cancel_requested = 1;
                has_nonterminal = 1;
            }
        }
        runtime->lifecycle = FLAT_RUNTIME_CANCELLING;
        (void)has_nonterminal;
    } else if (runtime->lifecycle == FLAT_RUNTIME_RUNNING) {
        runtime->lifecycle = FLAT_RUNTIME_DRAINING;
    }
    runtime_refresh_lifecycle_locked(runtime);
    (void)flat_ready_queue_wake_all(&runtime->ready_queue);
    runtime_unlock(runtime);
    return 0;
}

FlatRuntimeShutdownStatus flat_runtime_shutdown_status(FlatRuntime *runtime) {
    FlatTaskRecord *task;
    int runnable = 0, waiting = 0, owned = 0, pending = 0;
    FlatRuntimeShutdownStatus status;
    if (!runtime) return FLAT_RUNTIME_SHUTDOWN_BLOCKED;
    runtime_lock(runtime);
    runtime_refresh_lifecycle_locked(runtime);
    if (runtime->lifecycle == FLAT_RUNTIME_RUNNING) {
        status = FLAT_RUNTIME_SHUTDOWN_IN_PROGRESS;
    } else if (runtime->lifecycle == FLAT_RUNTIME_STOPPED) {
        status = FLAT_RUNTIME_SHUTDOWN_COMPLETE;
    } else {
        for (task = runtime->tasks; task; task = task->next) {
            if (task->dispatch.worker_owned || task->cleanup_owned) owned = 1;
            if (runtime_task_is_terminal(task)) continue;
            if (runtime->lifecycle == FLAT_RUNTIME_CANCELLING) pending = 1;
            else if (task->state == FLAT_TASK_RUNNABLE || task->state == FLAT_TASK_RUNNING) runnable = 1;
            else if (task->state == FLAT_TASK_WAITING) waiting = 1;
        }
        if (owned || runnable || pending || runtime->schedule_active)
            status = FLAT_RUNTIME_SHUTDOWN_IN_PROGRESS;
        else if (runtime->lifecycle == FLAT_RUNTIME_DRAINING && waiting)
            status = FLAT_RUNTIME_SHUTDOWN_BLOCKED;
        else
            status = FLAT_RUNTIME_SHUTDOWN_COMPLETE;
    }
    runtime_unlock(runtime);
    return status;
}

FlatRuntimeLifecycle flat_runtime_lifecycle(FlatRuntime *runtime) {
    FlatRuntimeLifecycle state;
    if (!runtime) return (FlatRuntimeLifecycle)0;
    runtime_lock(runtime);
    runtime_refresh_lifecycle_locked(runtime);
    state = runtime->lifecycle;
    runtime_unlock(runtime);
    return state;
}

int flat_runtime_run(FlatRuntime *runtime, const FlatBytecode *bc,
                     const FlatVmOptions *opts) {
    FlatVmOptions run_opts;
    int rc;
    if (!runtime || !bc) return 1;

    runtime_lock(runtime);
    if (runtime->lifecycle != FLAT_RUNTIME_RUNNING || runtime->worker_starting ||
        runtime->workers_started) {
        runtime_unlock(runtime);
        return 1;
    }
    runtime->active_runs++;
    runtime_unlock(runtime);

    if (opts) run_opts = *opts;
    else memset(&run_opts, 0, sizeof run_opts);
    run_opts.host_config = &runtime->host;
    rc = flat_vm_run_opts(bc, &run_opts);

    runtime_lock(runtime);
    if (runtime->active_runs > 0) runtime->active_runs--;
    runtime_unlock(runtime);
    return rc;
}

static int runtime_task_create_internal(FlatRuntime *runtime, const FlatBytecode *bc,
                                        const FlatVmOptions *opts, const char *entry_label,
                                        FlatTaskState initial_state, const FlatValue *args,
                                        size_t arg_count, FlatCollectionPool *source_coll,
                                        FlatStringPool *source_pool, FlatFileTable *source_files,
                                        FlatEventSubscriptionId event_subscription_id,
                                        FlatTaskId *out_id) {
    FlatTaskRecord *task;
    FlatVmOptions task_opts;
    FlatTaskNativeBridge bridge;
    FlatTaskId id;
    int worker_mode;
    char preflight_error[256];
    if (!runtime || !bc || !out_id) return -1;

    task = (FlatTaskRecord *)calloc(1, sizeof *task);
    if (!task) return -1;
    if (opts) task_opts = *opts;
    else memset(&task_opts, 0, sizeof task_opts);
    task_opts.host_config = &runtime->host;
    task->vm = entry_label ? flat_vm_create_at_label(bc, &task_opts, entry_label)
                           : flat_vm_create(bc, &task_opts);
    if (!task->vm) {
        free(task);
        return -1;
    }
    if (flat_vm_set_initial_args(task->vm, args, arg_count, source_coll, source_pool,
                                 source_files) != 0) {
        flat_vm_destroy(task->vm);
        free(task);
        return -1;
    }

    runtime_lock(runtime);
    if (runtime->lifecycle != FLAT_RUNTIME_RUNNING || runtime->worker_starting ||
        runtime->next_task_id == 0) {
        runtime_unlock(runtime);
        flat_vm_destroy(task->vm);
        free(task);
        return -1;
    }
    worker_mode = runtime->workers_started;
    id = runtime->next_task_id++;
    runtime_unlock(runtime);
    if (worker_mode && flat_vm_worker_preflight(task->vm, preflight_error,
                                                 sizeof preflight_error) != 0) {
        flat_vm_destroy(task->vm);
        free(task);
        return -1;
    }
    task->id = id;
    task->state = initial_state;
    flat_task_dispatch_init(&task->dispatch);
    /* Mark the new task as queued before publication; the queue item itself is
     * pushed later under scheduler_mutex together with registry insertion. */
    if (initial_state == FLAT_TASK_RUNNABLE)
        (void)flat_task_dispatch_enqueue(&task->dispatch);
    task->result.state = FLAT_VM_READY;
    memset(&bridge, 0, sizeof bridge);
    bridge.user = runtime;
    bridge.task_id = id;
    bridge.spawn = runtime_task_native_spawn;
    bridge.spawn_for = runtime_task_native_spawn_for;
    bridge.stop_for = runtime_task_native_stop_for;
    bridge.wait = runtime_task_native_wait;
    bridge.state = runtime_task_native_state;
    bridge.result = runtime_task_native_result;
    bridge.channel_create = runtime_task_native_channel_create;
    bridge.channel_set = runtime_task_native_channel_set;
    bridge.channel_var = runtime_task_native_channel_var;
    bridge.channel_lock = runtime_task_native_channel_lock;
    bridge.channel_unlock = runtime_task_native_channel_unlock;
    bridge.channel_revert = runtime_task_native_channel_revert;
    bridge.channel_destroy = runtime_task_native_channel_destroy;
    bridge.socket_supported = runtime_task_native_socket_supported;
    bridge.socket_connect = runtime_task_native_socket_connect;
    bridge.socket_send = runtime_task_native_socket_send;
    bridge.socket_close = runtime_task_native_socket_close;
    if (flat_vm_set_task_bridge(task->vm, &bridge) != 0) {
        flat_vm_destroy(task->vm);
        free(task);
        return -1;
    }
    /* Numeric Channel handles passed as direct spawn arguments pin their Channel
     * until this child reaches a terminal state. Other numeric arguments are ignored. */
    if (args && arg_count) {
        size_t ai;
        for (ai = 0; ai < arg_count; ++ai) {
            if (args[ai].tag == VAL_NUMBER) {
                double number = flat_number_as_f64(&args[ai]);
                if (isfinite(number) && number >= 1.0 && number <= 9007199254740991.0 &&
                    floor(number) == number) {
                    FlatChannel *channel = runtime_channel_acquire(runtime, (FlatChannelId)number);
                    if (channel) {
                        int ref_rc = runtime_channel_add_user_locked(channel, id);
                        flat_channel_unlock(channel);
                        if (ref_rc != 0) {
                            runtime_release_task_channel_refs(runtime, id);
                            flat_vm_destroy(task->vm);
                            free(task);
                            return -1;
                        }
                    }
                }
            }
        }
    }
    runtime_lock(runtime);
    if (runtime->lifecycle != FLAT_RUNTIME_RUNNING || runtime->worker_starting) {
        runtime_unlock(runtime);
        runtime_release_task_channel_refs(runtime, id);
        flat_vm_destroy(task->vm);
        free(task);
        return -1;
    }
    if (event_subscription_id) {
        FlatRuntimeEventSubscription *event_sub;
        for (event_sub = runtime->event_subscriptions; event_sub; event_sub = event_sub->next)
            if (event_sub->id == event_subscription_id) break;
        if (!event_sub) {
            runtime_unlock(runtime);
            runtime_release_task_channel_refs(runtime, id);
            flat_vm_destroy(task->vm);
            free(task);
            return -1;
        }
        event_sub->active_handlers++;
        task->event_subscription_id = event_subscription_id;
        task->event_handler_accounted = 1;
    }
    if (initial_state == FLAT_TASK_RUNNABLE &&
        flat_ready_queue_push(&runtime->ready_queue, task->id) != 0) {
        if (task->event_handler_accounted) {
            FlatRuntimeEventSubscription *event_sub;
            for (event_sub = runtime->event_subscriptions; event_sub; event_sub = event_sub->next)
                if (event_sub->id == task->event_subscription_id) break;
            if (event_sub && event_sub->active_handlers) event_sub->active_handlers--;
            task->event_handler_accounted = 0;
            task->event_subscription_id = 0;
        }
        runtime_unlock(runtime);
        runtime_release_task_channel_refs(runtime, id);
        flat_vm_destroy(task->vm);
        free(task);
        return -1;
    }
    if (runtime->tasks_tail) runtime->tasks_tail->next = task;
    else runtime->tasks = task;
    runtime->tasks_tail = task;
    runtime->task_count++;
    runtime_unlock(runtime);
    *out_id = id;
    return 0;
}

int flat_runtime_task_create(FlatRuntime *runtime, const FlatBytecode *bc,
                             const FlatVmOptions *opts, FlatTaskId *out_id) {
    return runtime_task_create_internal(runtime, bc, opts, NULL, FLAT_TASK_CREATED, NULL, 0,
                                        NULL, NULL, NULL, 0, out_id);
}

int flat_runtime_event_spawn_next(FlatRuntime *runtime,
                                  FlatEventSubscriptionId subscription_id,
                                  const FlatBytecode *bc, const char *handler_label,
                                  const FlatValue *bound_args, size_t bound_arg_count,
                                  FlatCollectionPool *source_coll,
                                  FlatStringPool *source_pool,
                                  FlatFileTable *source_files,
                                  uint64_t *out_task_id) {
    FlatRuntimeEventSubscription *sub;
    FlatRuntimeEventNode *node;
    FlatCollectionPool *tmp_coll = NULL;
    FlatStringPool *tmp_pool = NULL;
    FlatValue *args = NULL;
    size_t i;
    size_t bi;
    size_t expected = 0;
    int rc = -1;
    uint64_t task_id = 0;
    if (!runtime || !subscription_id || !bc || !handler_label || handler_label[0] != '#' ||
        !out_task_id || (bound_arg_count && (!bound_args || !source_coll || !source_pool)) ||
        bound_arg_count >= FGIR_MAX_PARAMS) return -1;
    *out_task_id = 0;
    for (bi = 0; bi < bc->block_count; ++bi) {
        if (bc->block_labels && strcmp(bc->block_labels[bi], handler_label) == 0) {
            expected = bc->block_params ? bc->block_params[bi].param_count : 0;
            break;
        }
    }
    if (bi >= bc->block_count || expected != bound_arg_count + 1) return -2;

    tmp_coll = flat_coll_create();
    tmp_pool = flat_pool_create();
    args = (FlatValue *)calloc(bound_arg_count + 1, sizeof *args);
    if (!tmp_coll || !tmp_pool || !args) goto done;

    runtime_lock(runtime);
    for (sub = runtime->event_subscriptions; sub; sub = sub->next)
        if (sub->id == subscription_id) break;
    if (!sub || sub->dispatching || runtime->lifecycle != FLAT_RUNTIME_RUNNING) {
        runtime_unlock(runtime);
        rc = -1;
        goto done;
    }
    if (!sub->head || (sub->dispatch_mode == FLAT_EVENT_DISPATCH_SERIAL &&
                       sub->active_handlers != 0)) {
        runtime_unlock(runtime);
        rc = 0;
        goto done;
    }
    sub->dispatching = 1;
    node = sub->head;
    if (flat_value_deep_copy_between(&node->value, &args[0], node->coll, node->pool, NULL,
                                     tmp_coll, tmp_pool, NULL) != 0) {
        sub->dispatching = 0;
        runtime_unlock(runtime);
        goto done;
    }
    for (i = 0; i < bound_arg_count; ++i) {
        if (flat_value_deep_copy_between(&bound_args[i], &args[i + 1], source_coll, source_pool,
                                         source_files, tmp_coll, tmp_pool, NULL) != 0) {
            sub->dispatching = 0;
            runtime_unlock(runtime);
            goto done;
        }
    }
    runtime_unlock(runtime);

    rc = runtime_task_create_internal(runtime, bc, NULL, handler_label, FLAT_TASK_RUNNABLE,
                                      args, bound_arg_count + 1, tmp_coll, tmp_pool, NULL, subscription_id,
                                      &task_id);
    runtime_lock(runtime);
    for (sub = runtime->event_subscriptions; sub; sub = sub->next)
        if (sub->id == subscription_id) break;
    if (!sub || !sub->dispatching || sub->head != node) {
        /* Unsubscribe is blocked while dispatching, so this is defensive only. */
        if (sub) sub->dispatching = 0;
        runtime_unlock(runtime);
        rc = -1;
        goto done;
    }
    sub->dispatching = 0;
    if (rc == 0) {
        sub->head = node->next;
        if (!sub->head) sub->tail = NULL;
        if (sub->queued) sub->queued--;
    }
    runtime_unlock(runtime);
    if (rc == 0) {
        runtime_event_node_destroy(node);
        *out_task_id = task_id;
        rc = 1;
    }
done:
    if (args) {
        for (i = 0; i < bound_arg_count + 1; ++i)
            flat_value_release(tmp_coll, tmp_pool, &args[i]);
    }
    free(args);
    if (tmp_coll) flat_coll_destroy(tmp_coll);
    if (tmp_pool) flat_pool_destroy(tmp_pool);
    return rc;
}

static int runtime_task_native_spawn(void *user, uint64_t parent_id, const FlatBytecode *bc,
                                     const char *entry_label, const FlatValue *args,
                                     size_t arg_count, FlatCollectionPool *source_coll,
                                     FlatStringPool *source_pool, FlatFileTable *source_files,
                                     uint64_t *out_id) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    FlatTaskRecord *parent;
    size_t bi;
    unsigned expected = 0;
    if (!runtime || !bc || !entry_label || !entry_label[0]) return -1;
    runtime_lock(runtime);
    parent = runtime_find_task(runtime, parent_id);
    if (!parent || parent->cleanup_owned || parent->state != FLAT_TASK_RUNNING) {
        runtime_unlock(runtime);
        return -1;
    }
    runtime_unlock(runtime);
    for (bi = 0; bi < bc->block_count; bi++) {
        if (bc->block_labels && strcmp(bc->block_labels[bi], entry_label) == 0) {
            expected = bc->block_params ? bc->block_params[bi].param_count : 0;
            break;
        }
    }
    if (bi >= bc->block_count || expected != arg_count) return -2;
    if (arg_count > 0 && (!args || !source_coll || !source_pool)) return -2;
    return runtime_task_create_internal(runtime, bc, NULL, entry_label, FLAT_TASK_RUNNABLE,
                                        args, arg_count, source_coll, source_pool, source_files,
                                        0, out_id);
}

static int runtime_task_native_spawn_for(void *user, uint64_t parent_id, const FlatBytecode *bc,
                                         uint64_t source_id, const char *source_kind,
                                         const char *event_name, const char *handler_label,
                                         const FlatValue *args, size_t arg_count,
                                         FlatCollectionPool *source_coll, FlatStringPool *source_pool,
                                         FlatFileTable *source_files,
                                         const FlatEventSubscriptionOptions *options,
                                         uint64_t *out_subscription_id) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    FlatTaskRecord *parent;
    if (!runtime || !bc || !out_subscription_id || !options) return -1;
    runtime_lock(runtime);
    parent = runtime_find_task(runtime, parent_id);
    if (!parent || parent->cleanup_owned || parent->state != FLAT_TASK_RUNNING ||
        runtime->lifecycle != FLAT_RUNTIME_RUNNING) {
        runtime_unlock(runtime);
        return -1;
    }
    runtime_unlock(runtime);
    return flat_runtime_event_subscribe_handler_config(runtime, source_id, source_kind, event_name,
        bc, handler_label, args, arg_count, source_coll, source_pool, source_files,
        options, out_subscription_id);
}

static int runtime_task_native_stop_for(void *user, uint64_t parent_id,
                                        uint64_t subscription_id) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    FlatTaskRecord *parent;
    if (!runtime || !subscription_id) return -1;
    runtime_lock(runtime);
    parent = runtime_find_task(runtime, parent_id);
    if (!parent || parent->cleanup_owned || parent->state != FLAT_TASK_RUNNING) {
        runtime_unlock(runtime);
        return -1;
    }
    runtime_unlock(runtime);
    return flat_runtime_event_unsubscribe(runtime, subscription_id);
}

static int runtime_task_native_wait(void *user, uint64_t waiter_id, uint64_t target_id) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    FlatTaskRecord *waiter;
    FlatTaskRecord *target;
    if (!runtime || waiter_id == 0 || target_id == 0 || waiter_id == target_id) return -1;
    runtime_lock(runtime);
    waiter = runtime_find_task(runtime, waiter_id);
    target = runtime_find_task(runtime, target_id);
    if (!waiter || !target || waiter->cleanup_owned || target->cleanup_owned ||
        waiter->state != FLAT_TASK_RUNNING ||
        runtime_task_owns_channel_transaction(runtime, waiter_id) ||
        runtime_wait_would_cycle(runtime, waiter_id, target_id)) {
        runtime_unlock(runtime);
        return -1;
    }
    if (target->state == FLAT_TASK_COMPLETED || target->state == FLAT_TASK_FAILED ||
        target->state == FLAT_TASK_CANCELLED) {
        runtime_unlock(runtime);
        return 0;
    }
    waiter->waiting_on = target_id;
    waiter->waiting_channel_id = 0;
    waiter->state = FLAT_TASK_WAITING;
    runtime_dispatch_dequeue_locked(runtime, waiter);
    runtime_unlock(runtime);
    return 1;
}

static int runtime_task_native_state(void *user, uint64_t task_id, int *out_state) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    FlatTaskRecord *task;
    if (!runtime || !out_state) return -1;
    runtime_lock(runtime);
    task = runtime_find_task(runtime, task_id);
    if (!task) { runtime_unlock(runtime); return -1; }
    *out_state = (int)task->state;
    runtime_unlock(runtime);
    return 0;
}

static int runtime_task_native_result(void *user, uint64_t task_id,
                                      FlatVmResult *out_result, int *out_task_state) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    FlatTaskRecord *task;
    if (!runtime || !out_result || !out_task_state) return -1;
    runtime_lock(runtime);
    task = runtime_find_task(runtime, task_id);
    if (!task || task->cleanup_owned ||
        (task->state != FLAT_TASK_COMPLETED && task->state != FLAT_TASK_FAILED &&
         task->state != FLAT_TASK_CANCELLED)) {
        runtime_unlock(runtime);
        return -1;
    }
    *out_result = task->result;
    *out_task_state = (int)task->state;
    runtime_unlock(runtime);
    return 0;
}

static int runtime_task_is_running(FlatRuntime *runtime, FlatTaskId task_id) {
    FlatTaskRecord *task;
    int running;
    if (!runtime || task_id == 0) return 0;
    runtime_lock(runtime);
    task = runtime_find_task(runtime, task_id);
    running = task && !task->cleanup_owned && task->state == FLAT_TASK_RUNNING;
    runtime_unlock(runtime);
    return running;
}

static int runtime_task_native_channel_create(void *user, uint64_t task_id,
                                              uint64_t *out_channel_id) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    if (!runtime_task_is_running(runtime, task_id) || !out_channel_id) return -1;
    if (runtime_channel_create_owned(runtime, task_id, out_channel_id) != 0) return -1;
    /* FlatL NUMBER handles must remain exactly representable as IEEE-754 integers. */
    if (*out_channel_id > UINT64_C(9007199254740991)) {
        (void)flat_runtime_channel_destroy(runtime, *out_channel_id);
        *out_channel_id = 0;
        return -1;
    }
    return 0;
}

static int runtime_task_native_channel_set(void *user, uint64_t task_id, uint64_t channel_id,
                                           const char *name, const FlatValue *value,
                                           FlatCollectionPool *source_coll,
                                           FlatStringPool *source_pool,
                                           FlatFileTable *source_files) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    int rc;
    if (!runtime_task_is_running(runtime, task_id)) return -1;
    rc = flat_runtime_channel_set(runtime, channel_id, task_id, name, value,
                                  source_coll, source_pool, source_files);
    if (rc == -2) {
        int wait_rc = runtime_task_wait_channel(runtime, task_id, channel_id);
        if (wait_rc == 1) {
            runtime_log_channel(task_id, channel_id, "set", name, runtime_value_type_name(value),
                                "waiting", "transaction-owned");
            return 1;
        }
        if (wait_rc == 0)
            rc = flat_runtime_channel_set(runtime, channel_id, task_id, name, value,
                                          source_coll, source_pool, source_files);
        else return -1;
    }
    return rc;
}

static int runtime_task_native_channel_var(void *user, uint64_t task_id, uint64_t channel_id,
                                           const char *name,
                                           FlatCollectionPool *destination_coll,
                                           FlatStringPool *destination_pool,
                                           FlatFileTable *destination_files,
                                           FlatValue *out_value) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    FlatChannel *channel;
    int rc;
    if (!runtime_task_is_running(runtime, task_id)) return -1;
    channel = runtime_channel_acquire_for_owner(runtime, channel_id, task_id);
    if (!channel) return -1;
    rc = flat_channel_var_unretained(channel, task_id, name, destination_coll,
                                     destination_pool, destination_files, out_value);
    flat_channel_unlock(channel);
    if (rc == -2) {
        int wait_rc = runtime_task_wait_channel(runtime, task_id, channel_id);
        if (wait_rc == 1) {
            runtime_log_channel(task_id, channel_id, "var", name, NULL,
                                "waiting", "transaction-owned");
            return 1;
        }
        if (wait_rc == 0) {
            channel = runtime_channel_acquire_for_owner(runtime, channel_id, task_id);
            if (!channel) return -1;
            rc = flat_channel_var_unretained(channel, task_id, name, destination_coll,
                                             destination_pool, destination_files, out_value);
            flat_channel_unlock(channel);
        } else return -1;
    }
    runtime_log_channel(task_id, channel_id, "var", name,
                        rc == 0 ? runtime_value_type_name(out_value) : NULL,
                        rc == 0 ? "read" : "rejected",
                        rc == -2 ? "transaction-owned" : (rc ? "missing-or-copy-failed" : NULL));
    return rc;
}

static int runtime_task_native_channel_lock(void *user, uint64_t task_id, uint64_t channel_id) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    if (!runtime_task_is_running(runtime, task_id)) return -1;
    return flat_runtime_channel_lock(runtime, channel_id, task_id);
}

static int runtime_task_native_channel_unlock(void *user, uint64_t task_id, uint64_t channel_id) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    if (!runtime_task_is_running(runtime, task_id)) return -1;
    return flat_runtime_channel_unlock(runtime, channel_id, task_id);
}

static int runtime_task_native_channel_revert(void *user, uint64_t task_id, uint64_t channel_id) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    if (!runtime_task_is_running(runtime, task_id)) return -1;
    return flat_runtime_channel_revert(runtime, channel_id, task_id);
}

static int runtime_task_native_channel_destroy(void *user, uint64_t task_id, uint64_t channel_id) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    if (!runtime_task_is_running(runtime, task_id)) return -1;
    return runtime_channel_destroy_owned(runtime, channel_id, task_id);
}

int flat_runtime_task_start(FlatRuntime *runtime, FlatTaskId id) {
    FlatTaskRecord *task;
    int rc;
    if (!runtime || id == 0) return -1;
    runtime_lock(runtime);
    task = runtime_find_task(runtime, id);
    if (runtime->lifecycle != FLAT_RUNTIME_RUNNING || runtime->worker_starting ||
        !task || task->cleanup_owned || task->cancel_requested ||
        task->state != FLAT_TASK_CREATED) {
        runtime_unlock(runtime);
        return -1;
    }
    task->state = FLAT_TASK_RUNNABLE;
    rc = runtime_dispatch_enqueue_locked(runtime, task);
    if (rc < 0) task->state = FLAT_TASK_CREATED;
    runtime_unlock(runtime);
    return rc < 0 ? -1 : 0;
}

static FlatTaskRecord *runtime_next_transaction_runnable_locked(FlatRuntime *runtime) {
    FlatTaskRecord *task;
    FlatTaskRecord *first = NULL;
    FlatTaskRecord *next = NULL;
    for (task = runtime->tasks; task; task = task->next) {
        if (task->cleanup_owned || task->state != FLAT_TASK_RUNNABLE ||
            !task->dispatch.queued || !runtime_task_owns_channel_transaction(runtime, task->id))
            continue;
        if (!first) first = task;
        if (task->id > runtime->last_scheduled_id && !next) next = task;
    }
    return next ? next : first;
}

static FlatTaskRecord *runtime_find_unqueued_runnable_locked(FlatRuntime *runtime) {
    FlatTaskRecord *task;
    for (task = runtime->tasks; task; task = task->next) {
        if (!task->cleanup_owned && task->state == FLAT_TASK_RUNNABLE &&
            !task->dispatch.queued && !task->dispatch.worker_owned) return task;
    }
    return NULL;
}

int flat_runtime_task_wait(FlatRuntime *runtime, FlatTaskId waiter_id, FlatTaskId target_id) {
    FlatTaskRecord *waiter;
    FlatTaskRecord *target;
    if (!runtime || waiter_id == 0 || target_id == 0 || waiter_id == target_id) return -1;
    runtime_lock(runtime);
    waiter = runtime_find_task(runtime, waiter_id);
    target = runtime_find_task(runtime, target_id);
    if (!waiter || !target || waiter->cleanup_owned || target->cleanup_owned ||
        waiter->cancel_requested || target->cancel_requested ||
        waiter->state != FLAT_TASK_RUNNABLE ||
        runtime_task_owns_channel_transaction(runtime, waiter_id) ||
        runtime_wait_would_cycle(runtime, waiter_id, target_id)) {
        runtime_unlock(runtime);
        return -1;
    }
    if (target->state == FLAT_TASK_COMPLETED || target->state == FLAT_TASK_FAILED ||
        target->state == FLAT_TASK_CANCELLED) {
        runtime_unlock(runtime);
        return 0; /* The dependency has already resolved. */
    }
    waiter->waiting_on = target_id;
    waiter->waiting_channel_id = 0;
    waiter->state = FLAT_TASK_WAITING;
    runtime_dispatch_dequeue_locked(runtime, waiter);
    runtime_unlock(runtime);
    return 0;
}

int flat_runtime_task_cancel_request(FlatRuntime *runtime, FlatTaskId id) {
    FlatTaskRecord *task;
    int rc = 0;
    if (!runtime || id == 0) return -1;
    runtime_lock(runtime);
    task = runtime_find_task(runtime, id);
    if (!task) {
        rc = -1;
    } else if (runtime_task_is_terminal(task)) {
        rc = 1; /* A terminal outcome already won; never rewrite its result. */
    } else if (task->cleanup_owned && !task->cancel_requested) {
        /* Destruction or another cleanup operation already owns this task.
         * Do not publish a cancellation request that can no longer be acted on. */
        rc = -1;
    } else {
        /* Idempotent publication only. VM teardown is always scheduler-owned. */
        task->cancel_requested = 1;
    }
    if (rc == 0) (void)flat_ready_queue_wake_all(&runtime->ready_queue);
    runtime_unlock(runtime);
    return rc;
}

int flat_runtime_task_cancel(FlatRuntime *runtime, FlatTaskId id) {
    FlatTaskRecord *task;
    FlatTaskState previous_state;
    FlatVmResult cancel_result;
    int cancel_rc;
    if (!runtime || id == 0) return -1;

    runtime_lock(runtime);
    task = runtime_find_task(runtime, id);
    if (!task || runtime->worker_starting || task->cleanup_owned || task->dispatch.worker_owned ||
        (task->state != FLAT_TASK_CREATED &&
         task->state != FLAT_TASK_RUNNABLE &&
         task->state != FLAT_TASK_WAITING)) {
        runtime_unlock(runtime);
        return -1;
    }
    previous_state = task->state;
    task->cleanup_owned = 1;
    runtime_dispatch_dequeue_locked(runtime, task);
    runtime_unlock(runtime);

    /* VM cancellation destroys VM-owned resources (including DOM/Lexbor state).
     * Never hold scheduler_mutex while performing that teardown. Keep the wait
     * edge/reference published until cancellation succeeds so its target cannot
     * be destroyed while this operation is still fallible. */
    memset(&cancel_result, 0, sizeof cancel_result);
    cancel_rc = flat_vm_cancel(task->vm, &cancel_result);

    runtime_lock(runtime);
    task = runtime_find_task(runtime, id);
    if (!task || !task->cleanup_owned) {
        runtime_unlock(runtime);
        return -1;
    }
    if (cancel_rc != 0) {
        task->cleanup_owned = 0;
        if (previous_state == FLAT_TASK_RUNNABLE) {
            (void)runtime_dispatch_enqueue_locked(runtime, task);
        } else if (previous_state == FLAT_TASK_WAITING && task->waiting_on != 0) {
            FlatTaskRecord *target = runtime_find_task(runtime, task->waiting_on);
            if (!target || target->state == FLAT_TASK_COMPLETED ||
                target->state == FLAT_TASK_FAILED || target->state == FLAT_TASK_CANCELLED) {
                task->waiting_on = 0;
                task->waiting_channel_id = 0;
                task->state = FLAT_TASK_RUNNABLE;
                (void)runtime_dispatch_enqueue_locked(runtime, task);
            }
        } else if (previous_state == FLAT_TASK_WAITING && task->waiting_channel_id != 0) {
            FlatChannel *channel = runtime_find_channel(runtime, task->waiting_channel_id);
            int available = 0;
            if (channel) {
                flat_channel_lock(channel);
                available = !channel->transaction_locked ||
                            channel->transaction_owner == task->id;
                flat_channel_unlock(channel);
            }
            if (!channel || available) {
                task->waiting_on = 0;
                task->waiting_channel_id = 0;
                task->state = FLAT_TASK_RUNNABLE;
                (void)runtime_dispatch_enqueue_locked(runtime, task);
            }
        }
        runtime_unlock(runtime);
        return -1;
    }
    task->result = cancel_result;
    task->cancel_requested = 0;
    task->waiting_on = 0;
    task->waiting_channel_id = 0;
    runtime_dispatch_dequeue_locked(runtime, task);
    runtime_abort_task_channel_transactions_locked(runtime, id);
    runtime_release_task_channel_refs_locked(runtime, id);
    task->state = FLAT_TASK_CANCELLED;
    runtime_event_handler_task_finished_locked(runtime, task);
    task->cleanup_owned = 0;
    runtime_wake_waiters_locked(runtime, id);
    runtime_unlock(runtime);
    return 0;
}

/* Caller holds scheduler_mutex. A serial subscription advances only after the
 * previous handler task reaches a terminal state (including cancellation). */
static void runtime_event_handler_task_finished_locked(FlatRuntime *runtime,
                                                        FlatTaskRecord *task) {
    FlatRuntimeEventSubscription *sub;
    if (!runtime || !task || !task->event_handler_accounted ||
        task->event_subscription_id == 0) return;
    for (sub = runtime->event_subscriptions; sub; sub = sub->next)
        if (sub->id == task->event_subscription_id) break;
    if (sub && sub->active_handlers) sub->active_handlers--;
    task->event_handler_accounted = 0;
    task->event_subscription_id = 0;
}

/* Caller holds scheduler_mutex. Cancellation becomes terminal only after VM
 * teardown and task-owned Channel cleanup have completed. */
static void runtime_cancel_commit_locked(FlatRuntime *runtime, FlatTaskRecord *task,
                                         const FlatVmResult *cancel_result) {
    task->result = *cancel_result;
    task->waiting_on = 0;
    task->waiting_channel_id = 0;
    runtime_dispatch_dequeue_locked(runtime, task);
    runtime_abort_task_channel_transactions_locked(runtime, task->id);
    runtime_release_task_channel_refs_locked(runtime, task->id);
    task->state = FLAT_TASK_CANCELLED;
    runtime_event_handler_task_finished_locked(runtime, task);
    task->cancel_requested = 0;
    task->cleanup_owned = 0;
    runtime_wake_waiters_locked(runtime, task->id);
}

static int runtime_task_is_terminal(const FlatTaskRecord *task) {
    return task && (task->state == FLAT_TASK_COMPLETED ||
                    task->state == FLAT_TASK_FAILED ||
                    task->state == FLAT_TASK_CANCELLED);
}

/* Convert one pending event into a normal runnable task. */
static int runtime_dispatch_one_event_handler(FlatRuntime *runtime) {
    FlatRuntimeEventSubscription *sub;
    FlatEventSubscriptionId id = 0;
    const FlatBytecode *bc = NULL;
    char label[FGIR_MAX_LABEL];
    FlatCollectionPool *snap_coll = NULL;
    FlatStringPool *snap_pool = NULL;
    FlatValue *snap_args = NULL;
    size_t arg_count = 0, i;
    uint64_t task_id = 0;
    int rc;
    runtime_lock(runtime);
    if (runtime->lifecycle != FLAT_RUNTIME_RUNNING) {
        runtime_unlock(runtime);
        return 0;
    }
    for (sub = runtime->event_subscriptions; sub; sub = sub->next) {
        if (sub->handler_bc && sub->head && !sub->dispatching && !sub->handler_dispatching &&
            sub->active_handlers < sub->max_active) break;
    }
    if (!sub) { runtime_unlock(runtime); return 0; }
    sub->handler_dispatching = 1;
    id = sub->id;
    bc = sub->handler_bc;
    snprintf(label, sizeof label, "%s", sub->handler_label);
    arg_count = sub->bound_arg_count;
    if (arg_count) {
        snap_coll = flat_coll_create();
        snap_pool = flat_pool_create();
        snap_args = (FlatValue *)calloc(arg_count, sizeof *snap_args);
        if (!snap_coll || !snap_pool || !snap_args) {
            sub->handler_dispatching = 0;
            runtime_unlock(runtime);
            rc = -1;
            goto done;
        }
        for (i = 0; i < arg_count; ++i) {
            if (flat_value_deep_copy_between(&sub->bound_args[i], &snap_args[i],
                                             sub->bound_coll, sub->bound_pool, NULL,
                                             snap_coll, snap_pool, NULL) != 0) {
                sub->handler_dispatching = 0;
                runtime_unlock(runtime);
                rc = -1;
                goto done;
            }
        }
    }
    runtime_unlock(runtime);
    rc = flat_runtime_event_spawn_next(runtime, id, bc, label, snap_args, arg_count,
                                       snap_coll, snap_pool, NULL, &task_id);
    runtime_lock(runtime);
    for (sub = runtime->event_subscriptions; sub; sub = sub->next)
        if (sub->id == id) break;
    if (sub) sub->handler_dispatching = 0;
    runtime_unlock(runtime);
done:
    if (snap_args) for (i = 0; i < arg_count; ++i)
        flat_value_release(snap_coll, snap_pool, &snap_args[i]);
    free(snap_args);
    if (snap_coll) flat_coll_destroy(snap_coll);
    if (snap_pool) flat_pool_destroy(snap_pool);
    return rc < 0 ? -1 : rc;
}

static int runtime_schedule_internal(FlatRuntime *runtime, uint64_t quantum,
                                     uint64_t max_quanta, uint64_t *out_quanta,
                                     int worker_mode) {
    uint64_t turns = 0;
    int scheduler_error = 0;
    if (out_quanta) *out_quanta = 0;
    if (!runtime || quantum == 0) return -1;

    runtime_lock(runtime);
    if (runtime->lifecycle == FLAT_RUNTIME_STOPPED ||
        (worker_mode ? (!runtime->workers_started || runtime->worker_stop) :
                       (runtime->schedule_active != 0 || runtime->workers_started))) {
        runtime_unlock(runtime);
        return -1;
    }
    runtime->schedule_active++;
    runtime->active_runs++;
    runtime_unlock(runtime);

    while (max_quanta == 0 || turns < max_quanta) {
        FlatTaskRecord *task;
        FlatVmResult result;
        uint64_t dispatch_token = 0;
        int rc;
        int requeue = 0;
        int cancellation_cleanup = 0;

        /* Convert one pending external event into a normal runnable task. */
        rc = runtime_dispatch_one_event_handler(runtime);
        if (rc < 0) { scheduler_error = 1; break; }

        /* Queue pop and dispatch claim share scheduler_mutex with every state
         * transition. This prevents a popped stale ID from being reused after a
         * WAITING -> RUNNABLE cycle. The queue mutex is always released before
         * any worker would acquire scheduler_mutex in the future. */
        runtime_lock(runtime);
        /* CREATED, RUNNABLE and WAITING tasks can be claimed for cleanup without
         * executing another bytecode instruction. A RUNNING task is cleaned by
         * its current owner after flat_vm_step() returns below. */
        for (task = runtime->tasks; task; task = task->next) {
            if (task->cancel_requested && !task->cleanup_owned &&
                !task->dispatch.worker_owned && !runtime_task_is_terminal(task)) {
                task->cleanup_owned = 1;
                runtime_dispatch_dequeue_locked(runtime, task);
                cancellation_cleanup = 1;
                break;
            }
        }
        if (cancellation_cleanup) {
            runtime_unlock(runtime);
            memset(&result, 0, sizeof result);
            rc = flat_vm_cancel(task->vm, &result);
            runtime_lock(runtime);
            if (rc == 0) {
                runtime_cancel_commit_locked(runtime, task, &result);
            } else {
                if (runtime->lifecycle != FLAT_RUNTIME_CANCELLING)
                    task->cancel_requested = 0;
                task->cleanup_owned = 0;
                if (task->state == FLAT_TASK_RUNNABLE)
                    (void)runtime_dispatch_enqueue_locked(runtime, task);
                scheduler_error = 1;
            }
            runtime_unlock(runtime);
            if (scheduler_error) break;
            turns++;
            continue;
        }
        task = runtime_next_transaction_runnable_locked(runtime);
        if (task) {
            (void)flat_ready_queue_remove(&runtime->ready_queue, task->id);
            if (flat_task_dispatch_claim(&task->dispatch, &dispatch_token) != 0) {
                scheduler_error = 1;
                runtime_unlock(runtime);
                break;
            }
        } else {
            for (;;) {
                uint64_t queued_id = 0;
                int pop_rc = flat_ready_queue_try_pop(&runtime->ready_queue, &queued_id);
                if (pop_rc < 0) {
                    scheduler_error = 1;
                    break;
                }
                if (pop_rc == 0) {
                    FlatTaskRecord *unpublished = runtime_find_unqueued_runnable_locked(runtime);
                    if (unpublished) {
                        if (runtime_dispatch_enqueue_locked(runtime, unpublished) < 0)
                            scheduler_error = 1;
                        else continue;
                    }
                    break;
                }
                task = runtime_find_task(runtime, queued_id);
                if (!task || task->cleanup_owned || task->state != FLAT_TASK_RUNNABLE ||
                    !task->dispatch.queued) {
                    /* Defensive stale entry: state remains authoritative. */
                    continue;
                }
                if (flat_task_dispatch_claim(&task->dispatch, &dispatch_token) != 0) {
                    scheduler_error = 1;
                    break;
                }
                break;
            }
        }
        if (scheduler_error || !task) {
            runtime_unlock(runtime);
            if (scheduler_error) break;
            break;
        }
        task->state = FLAT_TASK_RUNNING;
        runtime->last_scheduled_id = task->id;
        runtime_unlock(runtime);

        memset(&result, 0, sizeof result);
        rc = flat_vm_step(task->vm, quantum, &result);

        runtime_lock(runtime);
        task->result = result;
        if (task->state == FLAT_TASK_WAITING && rc == 0 && result.state == FLAT_VM_YIELDED) {
            /* task.wait()/Channel contention changed RUNNING -> WAITING in the native call. */
        } else if (rc == 0 && result.state == FLAT_VM_YIELDED) {
            task->state = FLAT_TASK_RUNNABLE;
        } else if (rc == 0 && result.state == FLAT_VM_COMPLETED) {
            task->state = FLAT_TASK_COMPLETED;
        } else {
            task->state = FLAT_TASK_FAILED;
            if (task->result.termination == FLAT_TERM_UNKNOWN)
                task->result.termination = FLAT_TERM_ERROR;
            task->result.state = FLAT_VM_FAILED;
        }

        /* Completion/failure wins if the VM reached a terminal result during
         * this quantum. Otherwise the current dispatch owner performs cancel. */
        if (task->cancel_requested && !runtime_task_is_terminal(task)) {
            FlatVmResult cancel_result;
            task->cleanup_owned = 1;
            runtime_unlock(runtime);
            memset(&cancel_result, 0, sizeof cancel_result);
            rc = flat_vm_cancel(task->vm, &cancel_result);
            runtime_lock(runtime);
            if (rc == 0) {
                runtime_cancel_commit_locked(runtime, task, &cancel_result);
            } else {
                if (runtime->lifecycle != FLAT_RUNTIME_CANCELLING)
                    task->cancel_requested = 0;
                task->cleanup_owned = 0;
                scheduler_error = 1;
            }
        } else if (runtime_task_is_terminal(task)) {
            task->cancel_requested = 0;
        }
        if (runtime_task_is_terminal(task))
            runtime_event_handler_task_finished_locked(runtime, task);
        if (task->state == FLAT_TASK_COMPLETED || task->state == FLAT_TASK_FAILED) {
            runtime_abort_task_channel_transactions_locked(runtime, task->id);
            runtime_release_task_channel_refs_locked(runtime, task->id);
            runtime_wake_waiters_locked(runtime, task->id);
        }
        if (task->state == FLAT_TASK_COMPLETED || task->state == FLAT_TASK_FAILED ||
            task->state == FLAT_TASK_CANCELLED || task->state == FLAT_TASK_WAITING)
            task->dispatch.wake_pending = 0;
        if (flat_task_dispatch_release(&task->dispatch, dispatch_token, &requeue) != 0) {
            scheduler_error = 1;
            runtime_unlock(runtime);
            break;
        }
        if (task->state == FLAT_TASK_RUNNABLE) {
            int publish_rc;
            if (requeue) {
                publish_rc = flat_ready_queue_push(&runtime->ready_queue, task->id);
                if (publish_rc != 0) task->dispatch.queued = 0;
            } else {
                publish_rc = runtime_dispatch_enqueue_locked(runtime, task);
            }
            if (publish_rc < 0) scheduler_error = 1;
        }
        runtime_unlock(runtime);
        if (scheduler_error) break;
        turns++;
    }

    runtime_lock(runtime);
    if (runtime->active_runs > 0) runtime->active_runs--;
    if (runtime->schedule_active > 0) runtime->schedule_active--;
    runtime_refresh_lifecycle_locked(runtime);
    runtime_unlock(runtime);
    if (out_quanta) *out_quanta = turns;
    return scheduler_error ? -1 : 0;
}

int flat_runtime_schedule(FlatRuntime *runtime, uint64_t quantum,
                          uint64_t max_quanta, uint64_t *out_quanta) {
    return runtime_schedule_internal(runtime, quantum, max_quanta, out_quanta, 0);
}

static void *runtime_worker_main(void *opaque) {
    FlatRuntimeWorker *worker = (FlatRuntimeWorker *)opaque;
    FlatRuntime *runtime;
    if (!worker || !worker->runtime) return NULL;
    runtime = worker->runtime;
    /* Startup barrier: no task can execute until every requested worker has
     * been created successfully. Epoch capture prevents a lost launch/abort wake. */
    for (;;) {
        uint64_t epoch = flat_ready_queue_wake_epoch(&runtime->ready_queue);
        int launch, stop;
        runtime_lock(runtime);
        launch = runtime->worker_launch;
        stop = runtime->worker_stop;
        runtime_unlock(runtime);
        if (stop) return NULL;
        if (launch) break;
        if (flat_ready_queue_wait_since(&runtime->ready_queue, epoch) < 0) return NULL;
    }
    for (;;) {
        uint64_t quanta = 0;
        uint64_t wake_epoch = flat_ready_queue_wake_epoch(&runtime->ready_queue);
        FlatRuntimeLifecycle lifecycle;
        int wait_rc;
        runtime_lock(runtime);
        lifecycle = runtime->lifecycle;
        if (runtime->worker_stop || lifecycle == FLAT_RUNTIME_STOPPED) {
            runtime_unlock(runtime);
            break;
        }
        runtime_unlock(runtime);

        if (runtime_schedule_internal(runtime, worker->quantum, 1, &quanta, 1) != 0) {
            int intentional_stop;
            runtime_lock(runtime);
            intentional_stop = runtime->worker_stop ||
                               runtime->lifecycle == FLAT_RUNTIME_STOPPED;
            if (!intentional_stop) runtime->worker_error = 1;
            runtime_unlock(runtime);
            if (!intentional_stop)
                (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
            break;
        }
        if (quanta != 0) continue;

        /* Wait without consuming the queue item. Queue lock is released before
         * the next scheduler_mutex acquisition, preserving lock order. */
        wait_rc = flat_ready_queue_wait_since(&runtime->ready_queue, wake_epoch);
        if (wait_rc < 0) {
            runtime_lock(runtime);
            runtime->worker_error = 1;
            runtime_unlock(runtime);
            (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
            break;
        }
        if (wait_rc == 0) break;
        /* An explicit wake may mean cancellation or STOPPED; re-check state. */
    }
    return NULL;
}

int flat_runtime_workers_start(FlatRuntime *runtime, size_t worker_count,
                               uint64_t quantum) {
    size_t i;
    int worker_mode_entered = 0;
    if (!runtime || !flat_threads_supported() || worker_count == 0 ||
        worker_count > 256 || quantum == 0) return -1;
    runtime_lock(runtime);
    if (runtime->workers_started || runtime->workers_joined ||
        runtime->lifecycle != FLAT_RUNTIME_RUNNING || runtime->schedule_active != 0 ||
        runtime->active_runs != 0) {
        runtime_unlock(runtime);
        return -1;
    }
    runtime->workers = (FlatRuntimeWorker *)calloc(worker_count, sizeof *runtime->workers);
    if (!runtime->workers) {
        runtime_unlock(runtime);
        return -1;
    }
    runtime->worker_count = worker_count;
    runtime->workers_started = 1;
    runtime->worker_launch = 0;
    runtime->worker_starting = 1;
    runtime->workers_joined = 0;
    runtime->worker_mode_entered = 0;
    runtime->worker_start_failed = 0;
    runtime->worker_stop = 0;
    runtime->worker_error = 0;
    for (i = 0; i < worker_count; ++i) {
        runtime->workers[i].runtime = runtime;
        runtime->workers[i].quantum = quantum;
    }
    /* Preflight every currently registered VM while task destruction and new
     * admission are excluded. This loads permitted optional plugins before freeze. */
    {
        FlatTaskRecord *task;
        char preflight_error[256];
        for (task = runtime->tasks; task; task = task->next) {
            if (flat_vm_worker_preflight(task->vm, preflight_error,
                                         sizeof preflight_error) != 0) {
                free(runtime->workers);
                runtime->workers = NULL;
                runtime->worker_count = 0;
                runtime->workers_started = 0;
                runtime->worker_starting = 0;
                runtime_unlock(runtime);
                return -1;
            }
        }
    }
    runtime_unlock(runtime);

    /* Freeze plugin/capability metadata before any worker can invoke native code. */
    if (flat_native_runtime_freeze_worker_capabilities() != 0 ||
        flat_native_worker_mode_enter() != 0) goto fail;
    worker_mode_entered = 1;
    runtime_lock(runtime);
    runtime->worker_mode_entered = 1;
    runtime_unlock(runtime);
    for (i = 0; i < worker_count; ++i) {
        if (runtime_worker_thread_create(&runtime->workers[i].thread, runtime_worker_main,
                                         &runtime->workers[i]) != 0) goto fail;
        runtime->workers[i].started = 1;
    }
    runtime_lock(runtime);
    runtime->worker_launch = 1;
    runtime->worker_starting = 0;
    runtime_unlock(runtime);
    (void)flat_ready_queue_wake_all(&runtime->ready_queue);
    return 0;

fail:
    runtime_lock(runtime);
    runtime->worker_stop = 1;
    runtime->worker_launch = 0;
    runtime_unlock(runtime);
    (void)flat_ready_queue_wake_all(&runtime->ready_queue);
    {
        int join_failed = 0;
        for (i = 0; i < worker_count; ++i) {
            if (runtime->workers[i].started) {
                if (runtime_worker_thread_join(&runtime->workers[i].thread) != 0) join_failed = 1;
                else runtime->workers[i].started = 0;
            }
        }
        if (join_failed) {
            /* Keep every handle and runtime-owned worker argument alive. The
             * caller may retry workers_join(); destroy remains rejected. */
            runtime_lock(runtime);
            runtime->worker_start_failed = 1;
            runtime_unlock(runtime);
            return -1;
        }
    }
    runtime_lock(runtime);
    free(runtime->workers);
    runtime->workers = NULL;
    runtime->worker_count = 0;
    runtime->workers_started = 0;
    runtime->worker_launch = 0;
    runtime->worker_starting = 0;
    runtime->worker_stop = 0;
    runtime->worker_mode_entered = 0;
    runtime->worker_start_failed = 0;
    runtime_unlock(runtime);
    if (worker_mode_entered) (void)flat_native_worker_mode_leave();
    return -1;
}

int flat_runtime_workers_join(FlatRuntime *runtime) {
    size_t i;
    int join_error = 0;
    int worker_error;
    int mode_entered;
    if (!runtime) return -1;
    runtime_lock(runtime);
    runtime_refresh_lifecycle_locked(runtime);
    if (runtime->workers_joining || !runtime->workers_started || runtime->workers_joined ||
        (!runtime->worker_start_failed && runtime->lifecycle != FLAT_RUNTIME_STOPPED &&
         !runtime->worker_error) || runtime->schedule_active != 0) {
        runtime_unlock(runtime);
        return -1;
    }
    /* Exactly one host caller owns this runtime's worker handles at a time. */
    runtime->workers_joining = 1;
    runtime_unlock(runtime);
    for (i = 0; i < runtime->worker_count; ++i) {
        if (runtime->workers[i].started) {
            if (runtime_worker_thread_join(&runtime->workers[i].thread) != 0) join_error = 1;
            else runtime->workers[i].started = 0;
        }
    }
    if (join_error) {
        /* Preserve failed handles and pool state for retry, but release the
         * operation claim so a later caller can retry the remaining handles. */
        runtime_lock(runtime);
        runtime->workers_joining = 0;
        runtime_unlock(runtime);
        return -1;
    }

    runtime_lock(runtime);
    worker_error = runtime->worker_error;
    mode_entered = runtime->worker_mode_entered;
    free(runtime->workers);
    runtime->workers = NULL;
    runtime->worker_count = 0;
    runtime->workers_started = 0;
    runtime->workers_joined = 1;
    runtime->workers_joining = 0;
    runtime->worker_launch = 0;
    runtime->worker_starting = 0;
    runtime->worker_stop = 0;
    runtime->worker_mode_entered = 0;
    runtime->worker_start_failed = 0;
    runtime_unlock(runtime);
    if (mode_entered && flat_native_worker_mode_leave() != 0) worker_error = 1;
    return worker_error ? -1 : 0;
}

FlatTaskState flat_runtime_task_state(const FlatRuntime *runtime_const, FlatTaskId id) {
    FlatRuntime *runtime = (FlatRuntime *)runtime_const;
    FlatTaskRecord *task;
    FlatTaskState state = (FlatTaskState)0;
    if (!runtime || id == 0) return state;
    runtime_lock(runtime);
    task = runtime_find_task(runtime, id);
    if (task) state = task->state;
    runtime_unlock(runtime);
    return state;
}

int flat_runtime_task_result(const FlatRuntime *runtime_const, FlatTaskId id,
                             FlatVmResult *out_result) {
    FlatRuntime *runtime = (FlatRuntime *)runtime_const;
    FlatTaskRecord *task;
    if (!runtime || !out_result || id == 0) return -1;
    runtime_lock(runtime);
    task = runtime_find_task(runtime, id);
    if (!task || task->cleanup_owned ||
        (task->state != FLAT_TASK_COMPLETED && task->state != FLAT_TASK_FAILED &&
         task->state != FLAT_TASK_CANCELLED)) {
        runtime_unlock(runtime);
        return -1;
    }
    *out_result = task->result;
    runtime_unlock(runtime);
    return 0;
}

int flat_runtime_task_destroy(FlatRuntime *runtime, FlatTaskId id) {
    FlatTaskRecord *task;
    FlatTaskRecord *prev;
    if (!runtime || id == 0) return -1;
    runtime_lock(runtime);
    for (prev = NULL, task = runtime->tasks; task; prev = task, task = task->next) {
        if (task->id == id) break;
    }
    if (!task || runtime->worker_starting || task->cleanup_owned || task->cancel_requested || runtime_has_waiters(runtime, id) ||
        (task->state != FLAT_TASK_CREATED && task->state != FLAT_TASK_COMPLETED &&
         task->state != FLAT_TASK_FAILED && task->state != FLAT_TASK_CANCELLED) ||
        task->dispatch.worker_owned) {
        runtime_unlock(runtime);
        return -1;
    }
    task->cleanup_owned = 1;
    runtime_unlock(runtime);

    /* VM teardown may enter serialized native-module gates (notably DOM). */
    if (flat_vm_destroy(task->vm) != 0) {
        runtime_lock(runtime);
        task = runtime_find_task(runtime, id);
        if (task) task->cleanup_owned = 0;
        runtime_unlock(runtime);
        return -1;
    }

    runtime_lock(runtime);
    for (prev = NULL, task = runtime->tasks; task; prev = task, task = task->next) {
        if (task->id == id) break;
    }
    if (!task || !task->cleanup_owned) {
        runtime_unlock(runtime);
        return -1;
    }
    runtime_dispatch_dequeue_locked(runtime, task);
    runtime_abort_task_channel_transactions_locked(runtime, id);
    runtime_release_task_channel_refs_locked(runtime, id);
    if (prev) prev->next = task->next;
    else runtime->tasks = task->next;
    if (runtime->tasks_tail == task) runtime->tasks_tail = prev;
    if (runtime->task_count > 0) runtime->task_count--;
    free(task);
    runtime_unlock(runtime);
    return 0;
}

static int runtime_channel_create_owned(FlatRuntime *runtime, FlatTaskId owner_id,
                                        FlatChannelId *out_id) {
    FlatChannel *channel;
    FlatChannelId id;
    if (!runtime || !out_id) return -1;
    channel = flat_channel_create(0);
    if (!channel) return -1;
    runtime_lock(runtime);
    if (runtime->next_channel_id == 0) {
        runtime_unlock(runtime);
        flat_channel_destroy(channel);
        return -1;
    }
    id = runtime->next_channel_id++;
    channel->id = id;
    channel->next = runtime->channels;
    runtime->channels = channel;
    runtime->channel_count++;
    runtime_unlock(runtime);
    *out_id = id;
    runtime_log_channel(owner_id, id, "create", NULL, NULL, "created", NULL);
    return 0;
}

int flat_runtime_channel_create(FlatRuntime *runtime, FlatChannelId *out_id) {
    return runtime_channel_create_owned(runtime, 0, out_id);
}

static int runtime_channel_add_user_locked(FlatChannel *channel, FlatTaskId task_id) {
    FlatChannelTaskRef *user;
    if (!channel || task_id == 0) return -1;
    for (user = channel->users; user; user = user->next)
        if (user->task_id == task_id) return 0;
    user = (FlatChannelTaskRef *)calloc(1, sizeof *user);
    if (!user) return -1;
    user->task_id = task_id;
    user->next = channel->users;
    channel->users = user;
    return 0;
}

/* Caller holds scheduler_mutex. */
static void runtime_release_task_channel_refs_locked(FlatRuntime *runtime, FlatTaskId task_id) {
    FlatChannel *channel;
    if (!runtime || task_id == 0) return;
    for (channel = runtime->channels; channel; channel = channel->next) {
        FlatChannelTaskRef **link;
        flat_channel_lock(channel);
        for (link = &channel->users; *link; ) {
            FlatChannelTaskRef *user = *link;
            if (user->task_id == task_id) {
                *link = user->next;
                free(user);
            } else {
                link = &user->next;
            }
        }
        flat_channel_unlock(channel);
    }
}

static void runtime_release_task_channel_refs(FlatRuntime *runtime, FlatTaskId task_id) {
    if (!runtime || task_id == 0) return;
    runtime_lock(runtime);
    runtime_release_task_channel_refs_locked(runtime, task_id);
    runtime_unlock(runtime);
}

static FlatChannel *runtime_channel_acquire(FlatRuntime *runtime, FlatChannelId id) {
    FlatChannel *channel;
    runtime_lock(runtime);
    channel = runtime_find_channel(runtime, id);
    if (channel) flat_channel_lock(channel);
    runtime_unlock(runtime);
    return channel;
}

/* Atomically validate a task reference and pin the Channel while the registry is locked. */
static FlatChannel *runtime_channel_acquire_for_owner(FlatRuntime *runtime, FlatChannelId id,
                                                       FlatTaskId owner_id) {
    FlatChannel *channel;
    FlatTaskRecord *owner;
    int active;
    if (!runtime || id == 0) return NULL;
    runtime_lock(runtime);
    owner = owner_id ? runtime_find_task(runtime, owner_id) : NULL;
    if (owner && owner->cleanup_owned) {
        runtime_unlock(runtime);
        return NULL;
    }
    active = owner && owner->state != FLAT_TASK_COMPLETED &&
             owner->state != FLAT_TASK_FAILED && owner->state != FLAT_TASK_CANCELLED;
    channel = runtime_find_channel(runtime, id);
    if (channel) {
        flat_channel_lock(channel);
        if (active && runtime_channel_add_user_locked(channel, owner_id) != 0) {
            flat_channel_unlock(channel);
            channel = NULL;
        }
    }
    runtime_unlock(runtime);
    return channel;
}

/* Register a scheduler dependency on the task that currently owns a Channel
 * transaction. Return 1 when the caller must yield/retry, 0 if the lock was
 * released before registration (caller may retry immediately), or -1 on an
 * invalid owner/dependency/cycle. */
static int runtime_task_wait_channel(FlatRuntime *runtime, FlatTaskId task_id,
                                     FlatChannelId channel_id) {
    FlatTaskRecord *task;
    FlatTaskRecord *transaction_owner;
    FlatChannel *channel;
    FlatTaskId owner_id;
    int rc = -1;
    if (!runtime || task_id == 0 || channel_id == 0) return -1;
    /* A retry is a suspension just like task.wait(): never park while this
     * task owns another Channel transaction. The scheduler mutex protects the
     * Channel registry while the ownership scan acquires individual Channel gates. */
    runtime_lock(runtime);
    if (runtime_task_owns_channel_transaction(runtime, task_id)) {
        runtime_unlock(runtime);
        return -1;
    }
    task = runtime_find_task(runtime, task_id);
    channel = runtime_find_channel(runtime, channel_id);
    if (!task || task->cleanup_owned || task->state != FLAT_TASK_RUNNING || !channel) {
        runtime_unlock(runtime);
        return -1;
    }
    flat_channel_lock(channel);
    if (!channel->transaction_locked) {
        rc = 0;
    } else {
        owner_id = channel->transaction_owner;
        transaction_owner = runtime_find_task(runtime, owner_id);
        if (owner_id != 0 && owner_id != task_id && transaction_owner &&
            !transaction_owner->cleanup_owned &&
            !runtime_wait_would_cycle(runtime, task_id, owner_id) &&
            runtime_channel_add_user_locked(channel, task_id) == 0) {
            task->waiting_on = owner_id;
            task->waiting_channel_id = channel_id;
            task->state = FLAT_TASK_WAITING;
            runtime_dispatch_dequeue_locked(runtime, task);
            rc = 1;
        }
    }
    flat_channel_unlock(channel);
    runtime_unlock(runtime);
    return rc;
}

static int runtime_channel_destroy_owned(FlatRuntime *runtime, FlatChannelId id,
                                         FlatTaskId owner_id) {
    FlatChannel **link;
    FlatChannel *channel;
    FlatChannelTaskRef *user;
    if (!runtime || id == 0) return -1;
    runtime_lock(runtime);
    for (link = &runtime->channels; *link && (*link)->id != id; link = &(*link)->next) { }
    channel = *link;
    if (!channel) {
        runtime_unlock(runtime);
        runtime_log_channel(owner_id, id, "destroy", NULL, NULL, "rejected", "unknown-channel");
        return -1;
    }
    flat_channel_lock(channel);
    if (channel->transaction_locked) {
        flat_channel_unlock(channel);
        runtime_unlock(runtime);
        runtime_log_channel(owner_id, id, "destroy", NULL, NULL, "rejected", "transaction-active");
        return -1;
    }
    for (user = channel->users; user; user = user->next) {
        if (user->task_id != owner_id) {
            flat_channel_unlock(channel);
            runtime_unlock(runtime);
            runtime_log_channel(owner_id, id, "destroy", NULL, NULL, "rejected", "active-task-reference");
            return -1;
        }
    }
    *link = channel->next;
    if (runtime->channel_count > 0) runtime->channel_count--;
    flat_channel_unlock(channel);
    runtime_unlock(runtime);
    runtime_log_channel(owner_id, id, "destroy", NULL, NULL, "destroyed", NULL);
    flat_channel_destroy(channel);
    return 0;
}

int flat_runtime_channel_set(FlatRuntime *runtime, FlatChannelId id, FlatTaskId owner_id,
                             const char *name, const FlatValue *value,
                             FlatCollectionPool *source_coll, FlatStringPool *source_pool,
                             FlatFileTable *source_files) {
    FlatChannel *channel;
    int rc;
    if (!runtime) return -1;
    channel = runtime_channel_acquire_for_owner(runtime, id, owner_id);
    if (!channel) return -1;
    rc = flat_channel_set(channel, owner_id, name, value, source_coll, source_pool, source_files);
    flat_channel_unlock(channel);
    runtime_log_channel(owner_id, id, "set", name, runtime_value_type_name(value),
                        rc == 0 ? "stored" : (rc == -2 ? "busy" : "rejected"),
                        rc == -2 ? "transaction-owned" : (rc ? "invalid-or-copy-failed" : NULL));
    return rc;
}

int flat_runtime_channel_var(FlatRuntime *runtime, FlatChannelId id, FlatTaskId owner_id,
                             const char *name, FlatCollectionPool *destination_coll,
                             FlatStringPool *destination_pool, FlatFileTable *destination_files,
                             FlatValue *out_value) {
    FlatChannel *channel;
    int rc;
    if (!runtime) return -1;
    channel = runtime_channel_acquire_for_owner(runtime, id, owner_id);
    if (!channel) return -1;
    rc = flat_channel_var(channel, owner_id, name, destination_coll, destination_pool,
                          destination_files, out_value);
    flat_channel_unlock(channel);
    runtime_log_channel(owner_id, id, "var", name, rc == 0 ? runtime_value_type_name(out_value) : NULL,
                        rc == 0 ? "read" : (rc == -2 ? "busy" : "rejected"),
                        rc == -2 ? "transaction-owned" : (rc ? "missing-or-copy-failed" : NULL));
    return rc;
}

int flat_runtime_channel_lock(FlatRuntime *runtime, FlatChannelId id, FlatTaskId owner_id) {
    FlatChannel *channel;
    FlatTaskRecord *owner;
    int rc;
    int owns_transaction;
    if (!runtime || owner_id == 0) return -1;
    /* Avoid a cooperative wait while holding a different Channel transaction.
     * Compute this before acquiring runtime/channel gates (the scan locks each
     * Channel itself). */
    runtime_lock(runtime);
    owns_transaction = runtime_task_owns_channel_transaction(runtime, owner_id);
    owner = runtime_find_task(runtime, owner_id);
    channel = runtime_find_channel(runtime, id);
    if (!owner || owner->cleanup_owned || owner->state == FLAT_TASK_COMPLETED ||
        owner->state == FLAT_TASK_FAILED || owner->state == FLAT_TASK_CANCELLED || !channel) {
        runtime_unlock(runtime);
        return -1;
    }
    flat_channel_lock(channel);
    if (channel->transaction_locked && channel->transaction_owner != owner_id) {
        FlatTaskId transaction_owner = channel->transaction_owner;
        if (owner->state == FLAT_TASK_RUNNING && !owns_transaction &&
            transaction_owner != 0 && runtime_find_task(runtime, transaction_owner) &&
            !runtime_find_task(runtime, transaction_owner)->cleanup_owned &&
            !runtime_wait_would_cycle(runtime, owner_id, transaction_owner)) {
            if (runtime_channel_add_user_locked(channel, owner_id) != 0) {
                flat_channel_unlock(channel);
                runtime_unlock(runtime);
                return -1;
            }
            owner->waiting_on = transaction_owner;
            owner->waiting_channel_id = id;
            owner->state = FLAT_TASK_WAITING;
            flat_channel_unlock(channel);
            runtime_unlock(runtime);
            runtime_log_channel(owner_id, id, "lock", NULL, NULL, "waiting", "transaction-owned");
            return 1;
        }
        flat_channel_unlock(channel);
        runtime_unlock(runtime);
        runtime_log_channel(owner_id, id, "lock", NULL, NULL, "rejected", "transaction-owned-or-cycle");
        return -1;
    }
    runtime_unlock(runtime);
    rc = runtime_channel_add_user_locked(channel, owner_id);
    if (rc == 0) rc = flat_channel_tx_lock(channel, owner_id);
    flat_channel_unlock(channel);
    runtime_log_channel(owner_id, id, "lock", NULL, NULL,
                        rc == 0 ? "locked" : "rejected", rc ? "transaction-unavailable" : NULL);
    return rc;
}

int flat_runtime_channel_unlock(FlatRuntime *runtime, FlatChannelId id, FlatTaskId owner_id) {
    FlatChannel *channel;
    int rc;
    if (!runtime) return -1;
    channel = runtime_channel_acquire(runtime, id);
    if (!channel) return -1;
    rc = flat_channel_tx_unlock(channel, owner_id);
    flat_channel_unlock(channel);
    if (rc == 0) runtime_wake_channel_waiters(runtime, id);
    runtime_log_channel(owner_id, id, "unlock", NULL, NULL,
                        rc == 0 ? "committed" : "rejected", rc ? "not-owner" : NULL);
    return rc;
}

int flat_runtime_channel_revert(FlatRuntime *runtime, FlatChannelId id, FlatTaskId owner_id) {
    FlatChannel *channel;
    int rc;
    if (!runtime) return -1;
    channel = runtime_channel_acquire(runtime, id);
    if (!channel) return -1;
    rc = flat_channel_tx_revert(channel, owner_id);
    flat_channel_unlock(channel);
    runtime_log_channel(owner_id, id, "revert", NULL, NULL,
                        rc == 0 ? "rolled-back" : "rejected", rc ? "not-owner-or-copy-failed" : NULL);
    return rc;
}

int flat_runtime_channel_destroy(FlatRuntime *runtime, FlatChannelId id) {
    return runtime_channel_destroy_owned(runtime, id, 0);
}

static int runtime_task_native_socket_supported(void *user) {
    (void)user;
    return flat_runtime_ws_supported();
}
static int runtime_task_native_socket_connect(void *user, const char *url, long connect_timeout_ms,
                                              size_t max_frame_bytes, uint64_t *out_id) {
    char error[256];
    return flat_runtime_ws_connect((FlatRuntime *)user, url, connect_timeout_ms, max_frame_bytes,
                                   out_id, error, sizeof error);
}
static int runtime_task_native_socket_send(void *user, uint64_t socket_id, int opcode,
                                           const void *data, size_t length) {
    char error[256];
    return flat_runtime_ws_send((FlatRuntime *)user, socket_id, opcode, data, length,
                                10000, error, sizeof error);
}
static int runtime_task_native_socket_close(void *user, uint64_t socket_id) {
    return flat_runtime_ws_close((FlatRuntime *)user, socket_id);
}

#if FLAT_WITH_WEBSOCKET
int flat_runtime_ws_supported(void) { return flat_ws_supported(); }

int flat_runtime_ws_connect(FlatRuntime *runtime, const char *url,
                            long connect_timeout_ms, size_t max_frame_bytes,
                            uint64_t *out_socket_id, char *error,
                            size_t error_capacity) {
    FlatRuntimeSocketEntry *entry;
    uint64_t id;
    if (out_socket_id) *out_socket_id = 0;
    if (!runtime || !url || !out_socket_id || connect_timeout_ms <= 0 ||
        max_frame_bytes == 0) return -1;
    runtime_lock(runtime);
    if (runtime->lifecycle != FLAT_RUNTIME_RUNNING) {
        runtime_unlock(runtime);
        return -1;
    }
    runtime_unlock(runtime);
    entry = (FlatRuntimeSocketEntry *)calloc(1, sizeof *entry);
    if (!entry) return -1;
    flat_mutex_lock(&runtime->socket_mutex);
    if (runtime->next_socket_id == 0 || runtime->next_socket_id > 9007199254740991ULL) {
        flat_mutex_unlock(&runtime->socket_mutex);
        free(entry);
        return -1;
    }
    id = runtime->next_socket_id++;
    if (flat_ws_event_client_start(runtime, id, url, connect_timeout_ms,
                                   max_frame_bytes, &entry->client,
                                   error, error_capacity) != 0) {
        flat_mutex_unlock(&runtime->socket_mutex);
        free(entry);
        return -1;
    }
    entry->id = id;
    entry->next = runtime->sockets;
    runtime->sockets = entry;
    runtime->socket_count++;
    flat_mutex_unlock(&runtime->socket_mutex);
    *out_socket_id = id;
    return 0;
}

int flat_runtime_ws_send(FlatRuntime *runtime, uint64_t socket_id,
                         int opcode, const void *data, size_t length,
                         long timeout_ms, char *error, size_t error_capacity) {
    FlatRuntimeSocketEntry *entry;
    int rc = -1;
    if (!runtime || !socket_id || timeout_ms <= 0) return -1;
    flat_mutex_lock(&runtime->socket_mutex);
    for (entry = runtime->sockets; entry && entry->id != socket_id; entry = entry->next) { }
    if (entry)
        rc = flat_ws_event_client_send(entry->client, (FlatWsOpcode)opcode,
                                       data, length, timeout_ms, error, error_capacity);
    flat_mutex_unlock(&runtime->socket_mutex);
    return rc;
}

int flat_runtime_ws_close(FlatRuntime *runtime, uint64_t socket_id) {
    FlatRuntimeSocketEntry **link, *entry;
    if (!runtime || !socket_id) return -1;
    flat_mutex_lock(&runtime->socket_mutex);
    for (link = &runtime->sockets; *link && (*link)->id != socket_id;
         link = &(*link)->next) { }
    entry = *link;
    if (!entry) {
        flat_mutex_unlock(&runtime->socket_mutex);
        return -1;
    }
    *link = entry->next;
    if (runtime->socket_count) runtime->socket_count--;
    flat_mutex_unlock(&runtime->socket_mutex);
    flat_ws_event_client_stop(entry->client);
    free(entry);
    return 0;
}

size_t flat_runtime_socket_count(FlatRuntime *runtime) {
    size_t count;
    if (!runtime) return 0;
    flat_mutex_lock(&runtime->socket_mutex);
    count = runtime->socket_count;
    flat_mutex_unlock(&runtime->socket_mutex);
    return count;
}
#else
int flat_runtime_ws_supported(void) { return 0; }
int flat_runtime_ws_connect(FlatRuntime *runtime, const char *url, long timeout,
                            size_t max_frame, uint64_t *out_id, char *error, size_t cap) {
    (void)runtime; (void)url; (void)timeout; (void)max_frame; (void)error; (void)cap;
    if (out_id) *out_id = 0;
    return -1;
}
int flat_runtime_ws_send(FlatRuntime *runtime, uint64_t id, int opcode,
                         const void *data, size_t length, long timeout,
                         char *error, size_t cap) {
    (void)runtime; (void)id; (void)opcode; (void)data; (void)length;
    (void)timeout; (void)error; (void)cap; return -1;
}
int flat_runtime_ws_close(FlatRuntime *runtime, uint64_t id) {
    (void)runtime; (void)id; return -1;
}
size_t flat_runtime_socket_count(FlatRuntime *runtime) { (void)runtime; return 0; }
#endif

#if FLAT_WITH_WS_SERVER
int flat_runtime_ws_server_listen(FlatRuntime *runtime, const char *host,
                                  uint16_t port, int backlog,
                                  uint64_t *out_listener_id, uint16_t *out_bound_port,
                                  char *error, unsigned long error_capacity) {
    if (!runtime || !runtime->ws_server_registry ||
        flat_runtime_lifecycle(runtime) != FLAT_RUNTIME_RUNNING) return -1;
    return flat_ws_server_registry_listen(runtime->ws_server_registry, host, port,
        backlog, out_listener_id, out_bound_port, error, error_capacity);
}
int flat_runtime_ws_server_accept(FlatRuntime *runtime, uint64_t listener_id,
                                  long timeout_ms, uint64_t *out_peer_id,
                                  char *error, unsigned long error_capacity) {
    if (!runtime || !runtime->ws_server_registry ||
        flat_runtime_lifecycle(runtime) != FLAT_RUNTIME_RUNNING) return -1;
    return flat_ws_server_registry_accept(runtime->ws_server_registry, listener_id,
        timeout_ms, out_peer_id, error, error_capacity);
}
int flat_runtime_ws_server_upgrade(FlatRuntime *runtime, uint64_t peer_id,
                                   long timeout_ms, char *error,
                                   unsigned long error_capacity) {
    if (!runtime || !runtime->ws_server_registry ||
        flat_runtime_lifecycle(runtime) != FLAT_RUNTIME_RUNNING) return -1;
    return flat_ws_server_registry_upgrade(runtime->ws_server_registry, peer_id,
        timeout_ms, error, error_capacity);
}
int flat_runtime_ws_server_send(FlatRuntime *runtime, uint64_t peer_id,
                                FlatWsServerOpcode opcode, const void *data,
                                unsigned long length, char *error,
                                unsigned long error_capacity) {
    if (!runtime || !runtime->ws_server_registry ||
        flat_runtime_lifecycle(runtime) != FLAT_RUNTIME_RUNNING) return -1;
    return flat_ws_server_registry_send(runtime->ws_server_registry, peer_id,
        opcode, data, length, error, error_capacity);
}
int flat_runtime_ws_server_receive(FlatRuntime *runtime, uint64_t peer_id,
                                   long timeout_ms, unsigned long max_message_bytes,
                                   FlatWsServerMessage *out_message, char *error,
                                   unsigned long error_capacity) {
    if (!runtime || !runtime->ws_server_registry ||
        flat_runtime_lifecycle(runtime) != FLAT_RUNTIME_RUNNING) return -1;
    return flat_ws_server_registry_receive(runtime->ws_server_registry, peer_id,
        timeout_ms, max_message_bytes, out_message, error, error_capacity);
}
int flat_runtime_ws_server_close_peer(FlatRuntime *runtime, uint64_t peer_id) {
    if (!runtime || !runtime->ws_server_registry) return -1;
    return flat_ws_server_registry_close_peer(runtime->ws_server_registry, peer_id);
}
int flat_runtime_ws_server_close_listener(FlatRuntime *runtime, uint64_t listener_id) {
    if (!runtime || !runtime->ws_server_registry) return -1;
    return flat_ws_server_registry_close_listener(runtime->ws_server_registry, listener_id);
}
#else
int flat_runtime_ws_server_listen(FlatRuntime *runtime, const char *host,
                                  uint16_t port, int backlog,
                                  uint64_t *out_listener_id, uint16_t *out_bound_port,
                                  char *error, unsigned long error_capacity) {
    (void)runtime; (void)host; (void)port; (void)backlog;
    (void)out_bound_port; (void)error; (void)error_capacity;
    if (out_listener_id) *out_listener_id = 0;
    return -1;
}
int flat_runtime_ws_server_accept(FlatRuntime *runtime, uint64_t listener_id,
                                  long timeout_ms, uint64_t *out_peer_id,
                                  char *error, unsigned long error_capacity) {
    (void)runtime; (void)listener_id; (void)timeout_ms;
    (void)error; (void)error_capacity;
    if (out_peer_id) *out_peer_id = 0;
    return -1;
}
int flat_runtime_ws_server_upgrade(FlatRuntime *runtime, uint64_t peer_id,
                                   long timeout_ms, char *error,
                                   unsigned long error_capacity) {
    (void)runtime; (void)peer_id; (void)timeout_ms; (void)error; (void)error_capacity;
    return -1;
}
int flat_runtime_ws_server_send(FlatRuntime *runtime, uint64_t peer_id,
                                FlatWsServerOpcode opcode, const void *data,
                                unsigned long length, char *error,
                                unsigned long error_capacity) {
    (void)runtime; (void)peer_id; (void)opcode; (void)data; (void)length;
    (void)error; (void)error_capacity; return -1;
}
int flat_runtime_ws_server_receive(FlatRuntime *runtime, uint64_t peer_id,
                                   long timeout_ms, unsigned long max_message_bytes,
                                   FlatWsServerMessage *out_message, char *error,
                                   unsigned long error_capacity) {
    (void)runtime; (void)peer_id; (void)timeout_ms; (void)max_message_bytes;
    (void)error; (void)error_capacity;
    if (out_message) memset(out_message, 0, sizeof *out_message);
    return -1;
}
int flat_runtime_ws_server_close_peer(FlatRuntime *runtime, uint64_t peer_id) {
    (void)runtime; (void)peer_id; return -1;
}
int flat_runtime_ws_server_close_listener(FlatRuntime *runtime, uint64_t listener_id) {
    (void)runtime; (void)listener_id; return -1;
}
#endif

int flat_runtime_destroy(FlatRuntime *runtime) {
    int busy;
#if FLAT_WITH_WEBSOCKET
    size_t active_socket_count;
#endif
    if (!runtime) return -1;
#if FLAT_WITH_WEBSOCKET
    active_socket_count = flat_runtime_socket_count(runtime);
#endif
    runtime_lock(runtime);
    runtime_refresh_lifecycle_locked(runtime);
    busy = runtime->active_runs != 0 || runtime->task_count != 0 || runtime->channel_count != 0 ||
           runtime->event_subscription_count != 0 ||
#if FLAT_WITH_WEBSOCKET
           active_socket_count != 0 ||
#endif
           runtime->workers_started || runtime->worker_starting || runtime->workers_joining ||
           runtime->worker_count != 0 ||
           (runtime->lifecycle != FLAT_RUNTIME_RUNNING &&
            runtime->lifecycle != FLAT_RUNTIME_STOPPED);
    runtime_unlock(runtime);
    if (busy) return -1;

    if (flat_ready_queue_close(&runtime->ready_queue) != 0) return -1;
    for (;;) {
        uint64_t ignored_id = 0;
        int pop_rc = flat_ready_queue_try_pop(&runtime->ready_queue, &ignored_id);
        if (pop_rc < 0) return -1;
        if (pop_rc == 0) break;
    }
    if (flat_ready_queue_destroy(&runtime->ready_queue) != 0) return -1;
    while (runtime->open_socket_events) {
        FlatRuntimeOpenSocketEvent *next = runtime->open_socket_events->next;
        free(runtime->open_socket_events);
        runtime->open_socket_events = next;
    }
    while (runtime->pending_socket_events) {
        FlatRuntimePendingSocketEvents *entry = runtime->pending_socket_events;
        runtime->pending_socket_events = entry->next;
        while (entry->head) {
            FlatRuntimeEventNode *next = entry->head->next;
            runtime_event_node_destroy(entry->head);
            entry->head = next;
        }
        free(entry);
    }
#if FLAT_WITH_WS_SERVER
    if (runtime->ws_server_registry) {
        if (flat_ws_server_registry_destroy(runtime->ws_server_registry) != 0) return -1;
        runtime->ws_server_registry = NULL;
    }
    if (runtime->io_reactor) {
        if (flat_io_reactor_destroy(runtime->io_reactor) != 0) return -1;
        runtime->io_reactor = NULL;
    }
#endif
    flat_host_config_destroy(&runtime->host);
#if FLAT_WITH_WEBSOCKET
    if (flat_mutex_destroy(&runtime->socket_mutex) != 0) return -1;
#endif
    if (flat_mutex_destroy(&runtime->scheduler_mutex) != 0) return -1;
    memset(runtime, 0, sizeof *runtime);
    free(runtime);
    return 0;
}

#ifdef FLAT_WASM_BUILD
/* A FLEditor run may finish its root task while event subscriptions are still
 * serving browser callbacks. Keep the one-shot runtime alive until subscriptions,
 * sockets and their handler tasks have all been explicitly drained. */
static int runtime_wasm_has_async_work(FlatRuntime *runtime) {
    FlatTaskRecord *task;
    FlatRuntimeEventSubscription *sub;
    int pending = 0;
    if (!runtime) return 0;
    runtime_lock(runtime);
    for (sub = runtime->event_subscriptions; !pending && sub; sub = sub->next) {
        if (!sub->source_closed || sub->queued != 0 || sub->dispatching ||
            sub->handler_dispatching || sub->active_handlers != 0) pending = 1;
    }
    for (task = runtime->tasks; !pending && task; task = task->next) {
        if (!runtime_task_is_terminal(task)) pending = 1;
    }
    runtime_unlock(runtime);
#if FLAT_WITH_WEBSOCKET
    if (!pending && flat_runtime_socket_count(runtime) != 0) pending = 1;
#endif
    return pending;
}

/* Closed-source subscriptions are no longer useful once their queued close
 * event and all dispatched handlers have completed. Remove them before the
 * one-shot runtime teardown so persistent subscriptions cannot keep WASM alive. */
static int runtime_wasm_prune_closed_subscriptions(FlatRuntime *runtime) {
    for (;;) {
        FlatEventSubscriptionId id = 0;
        FlatRuntimeEventSubscription *sub;
        runtime_lock(runtime);
        for (sub = runtime->event_subscriptions; sub; sub = sub->next) {
            if (sub->source_closed && sub->queued == 0 && !sub->dispatching &&
                !sub->handler_dispatching && sub->active_handlers == 0) {
                id = sub->id;
                break;
            }
        }
        runtime_unlock(runtime);
        if (!id) return 0;
        if (flat_runtime_event_unsubscribe(runtime, id) != 0) return -1;
    }
}
#endif

/* One-shot host helper used by environments (such as FLEditor WASM) that need
 * FlatL Task/Channel natives without exposing the full runtime lifecycle API. */
int flat_runtime_run_scheduled(const FlatBytecode *bc, const FlatVmOptions *opts,
                               FlatVmResult *out_result) {
    enum { FLAT_RUNTIME_RUN_QUANTUM = 8 };
    FlatRuntime *runtime = NULL;
    FlatTaskId root_id = 0;
    FlatVmResult root_result;
    FlatTaskRecord *task;
    int schedule_rc = 0;
    int cleanup_rc = 0;
    int rc = 1;
    int have_root_result = 0;

    memset(&root_result, 0, sizeof root_result);
    root_result.exit_code = 1;
    root_result.termination = FLAT_TERM_ERROR;
    root_result.state = FLAT_VM_FAILED;
    snprintf(root_result.diagnostic, sizeof root_result.diagnostic,
             "runtime task execution was not started");
    if (out_result) *out_result = root_result;
    if (!bc) return 1;

    runtime = flat_runtime_create(NULL);
    if (!runtime) {
        snprintf(root_result.diagnostic, sizeof root_result.diagnostic,
                 "runtime initialization failed");
        goto done;
    }
    if (flat_runtime_task_create(runtime, bc, opts, &root_id) != 0) {
        snprintf(root_result.diagnostic, sizeof root_result.diagnostic,
                 "root task initialization failed");
        goto cleanup;
    }
    if (flat_runtime_task_start(runtime, root_id) != 0) {
        snprintf(root_result.diagnostic, sizeof root_result.diagnostic,
                 "root task start failed");
        goto cleanup;
    }

#ifdef FLAT_WASM_BUILD
    /* Browser transports (WebSocket, Fetch completion, timers) publish events
     * on the JS thread. Keep the runtime alive while the root task is waiting;
     * Asyncify yields to the browser so those callbacks can enqueue work. */
    for (;;) {
        schedule_rc = flat_runtime_schedule(runtime, FLAT_RUNTIME_RUN_QUANTUM, 0, NULL);
        if (schedule_rc != 0) break;
        if (!have_root_result && flat_runtime_task_result(runtime, root_id, &root_result) == 0) {
            have_root_result = 1;
            rc = root_result.exit_code;
        }
        if (runtime_wasm_prune_closed_subscriptions(runtime) != 0) {
            schedule_rc = -1;
            break;
        }
        if (have_root_result && !runtime_wasm_has_async_work(runtime)) break;
        emscripten_sleep(10);
    }
#else
    schedule_rc = flat_runtime_schedule(runtime, FLAT_RUNTIME_RUN_QUANTUM, 0, NULL);
    if (schedule_rc == 0 &&
        flat_runtime_task_result(runtime, root_id, &root_result) == 0) {
        have_root_result = 1;
        rc = root_result.exit_code;
    }
#endif
    if (!have_root_result) {
        if (schedule_rc != 0) {
            snprintf(root_result.diagnostic, sizeof root_result.diagnostic,
                     "cooperative scheduler failed");
        } else {
            snprintf(root_result.diagnostic, sizeof root_result.diagnostic,
                     "root task stopped before reaching a terminal state");
        }
        root_result.exit_code = 1;
        root_result.termination = FLAT_TERM_ERROR;
        root_result.state = FLAT_VM_FAILED;
        rc = 1;
    }

    /* Event-driven browser runs may outlive the root task. At this point all
     * registered subscriptions, sockets and handler tasks have drained. Cancel
     * only unrelated leftovers. */
    runtime_lock(runtime);
    for (task = runtime->tasks; task; task = task->next) {
        if (!runtime_task_is_terminal(task)) break;
    }
    runtime_unlock(runtime);
    if (task) {
        if (flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL) != 0 ||
            flat_runtime_schedule(runtime, FLAT_RUNTIME_RUN_QUANTUM, 0, NULL) != 0) {
            cleanup_rc = -1;
        }
        if (!have_root_result && root_id != 0 &&
            flat_runtime_task_result(runtime, root_id, &root_result) == 0) {
            have_root_result = 1;
            rc = root_result.exit_code;
        }
    }

cleanup:
    /* At this point all tasks should be terminal. Destroy them before Channels
     * so task-held Channel references and uncommitted transactions are released. */
    while (runtime->tasks) {
        FlatTaskId id = runtime->tasks->id;
        if (flat_runtime_task_destroy(runtime, id) != 0) {
            cleanup_rc = -1;
            break;
        }
    }
    while (runtime->channels) {
        FlatChannelId id = runtime->channels->id;
        if (flat_runtime_channel_destroy(runtime, id) != 0) {
            cleanup_rc = -1;
            break;
        }
    }
    if (flat_runtime_destroy(runtime) != 0) {
        cleanup_rc = -1;
    }
    if (cleanup_rc != 0) {
        rc = 1;
        root_result.exit_code = 1;
        root_result.termination = FLAT_TERM_ERROR;
        root_result.state = FLAT_VM_FAILED;
        snprintf(root_result.diagnostic, sizeof root_result.diagnostic,
                 "runtime task cleanup failed");
    }

done:
    if (opts && opts->out_termination)
        *opts->out_termination = (int)root_result.termination;
    if (opts && opts->out_panic_msg && opts->out_panic_msg_sz) {
        snprintf(opts->out_panic_msg, opts->out_panic_msg_sz, "%s",
                 root_result.diagnostic);
    }
    if (out_result) *out_result = root_result;
    return rc;
}

