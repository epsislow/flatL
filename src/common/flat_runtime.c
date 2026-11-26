#include "flat_runtime.h"
#include "flat_task.h"
#include "flat_task_native.h"
#include "flat_channel_internal.h"
#include "flat_vm_internal.h"
#include "flat_runtime_log.h"
#include "flat_number.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

typedef struct FlatTaskRecord {
    FlatTaskId id;
    FlatTaskState state;
    FlatVm *vm;
    FlatVmResult result;
    FlatTaskId waiting_on;
    FlatChannelId waiting_channel_id;
    struct FlatTaskRecord *next;
} FlatTaskRecord;

struct FlatRuntime {
    FlatHostConfig host;
    atomic_bool lock;
    unsigned active_runs;
    FlatTaskRecord *tasks;
    FlatChannel *channels;
    size_t channel_count;
    FlatChannelId next_channel_id;
    FlatTaskRecord *tasks_tail;
    size_t task_count;
    FlatTaskId next_task_id;
    FlatTaskId last_scheduled_id;
};

static int runtime_task_create_internal(FlatRuntime *runtime, const FlatBytecode *bc,
                                        const FlatVmOptions *opts, const char *entry_label,
                                        FlatTaskState initial_state, const FlatValue *args,
                                        size_t arg_count, FlatCollectionPool *source_coll,
                                        FlatStringPool *source_pool, FlatFileTable *source_files,
                                        FlatTaskId *out_id);
static int runtime_task_native_spawn(void *user, uint64_t parent_id, const FlatBytecode *bc,
                                     const char *entry_label, const FlatValue *args,
                                     size_t arg_count, FlatCollectionPool *source_coll,
                                     FlatStringPool *source_pool, FlatFileTable *source_files,
                                     uint64_t *out_id);
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
static FlatChannel *runtime_channel_acquire(FlatRuntime *runtime, FlatChannelId id);
static int runtime_task_wait_channel(FlatRuntime *runtime, FlatTaskId task_id,
                                     FlatChannelId channel_id);
static int runtime_channel_add_user_locked(FlatChannel *channel, FlatTaskId task_id);
static void runtime_release_task_channel_refs(FlatRuntime *runtime, FlatTaskId task_id);
static int runtime_channel_destroy_owned(FlatRuntime *runtime, FlatChannelId id,
                                         FlatTaskId owner_id);
static int runtime_channel_create_owned(FlatRuntime *runtime, FlatTaskId owner_id,
                                        FlatChannelId *out_id);

static void runtime_lock(FlatRuntime *runtime) {
    while (atomic_exchange_explicit(&runtime->lock, 1, memory_order_acquire)) { }
}

static void runtime_unlock(FlatRuntime *runtime) {
    atomic_store_explicit(&runtime->lock, 0, memory_order_release);
}

static FlatTaskRecord *runtime_find_task(const FlatRuntime *runtime, FlatTaskId id);

static int runtime_has_waiters(const FlatRuntime *runtime, FlatTaskId id) {
    const FlatTaskRecord *task;
    for (task = runtime ? runtime->tasks : NULL; task; task = task->next) {
        if (task->state == FLAT_TASK_WAITING && task->waiting_on == id) return 1;
    }
    return 0;
}

static void runtime_wake_waiters(FlatRuntime *runtime, FlatTaskId target_id) {
    FlatTaskRecord *task;
    for (task = runtime->tasks; task; task = task->next) {
        if (task->state == FLAT_TASK_WAITING && task->waiting_on == target_id) {
            task->waiting_on = 0;
            task->waiting_channel_id = 0;
            task->state = FLAT_TASK_RUNNABLE;
        }
    }
}

static void runtime_wake_channel_waiters(FlatRuntime *runtime, FlatChannelId channel_id) {
    FlatTaskRecord *task;
    if (!runtime || channel_id == 0) return;
    for (task = runtime->tasks; task; task = task->next) {
        if (task->state == FLAT_TASK_WAITING && task->waiting_channel_id == channel_id) {
            task->waiting_on = 0;
            task->waiting_channel_id = 0;
            task->state = FLAT_TASK_RUNNABLE;
        }
    }
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

static void runtime_abort_task_channel_transactions(FlatRuntime *runtime, FlatTaskId task_id) {
    FlatChannel *channel;
    if (!runtime || task_id == 0) return;
    runtime_lock(runtime);
    for (channel = runtime->channels; channel; channel = channel->next) {
        flat_channel_lock(channel);
        if (channel->transaction_locked && channel->transaction_owner == task_id)
            (void)flat_channel_tx_abort(channel, task_id);
        flat_channel_unlock(channel);
    }
    runtime_unlock(runtime);
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

FlatRuntime *flat_runtime_create(const FlatHostConfig *config) {
    FlatRuntime *runtime = (FlatRuntime *)calloc(1, sizeof *runtime);
    if (!runtime) return NULL;
    atomic_init(&runtime->lock, 0);
    runtime->next_task_id = 1;
    runtime->next_channel_id = 1;

    if (config) {
        if (flat_env_context_clone(&runtime->host.env, &config->env) != 0) {
            free(runtime);
            return NULL;
        }
        runtime->host.path = config->path;
        if (runtime->host.path.sep != '/' && runtime->host.path.sep != '\\') {
            flat_host_config_destroy(&runtime->host);
            free(runtime);
            return NULL;
        }
    } else if (flat_host_config_capture_staging(&runtime->host) != 0) {
        flat_host_config_destroy(&runtime->host);
        free(runtime);
        return NULL;
    }
    return runtime;
}

int flat_runtime_run(FlatRuntime *runtime, const FlatBytecode *bc,
                     const FlatVmOptions *opts) {
    FlatVmOptions run_opts;
    int rc;
    if (!runtime || !bc) return 1;

    runtime_lock(runtime);
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
                                        FlatTaskId *out_id) {
    FlatTaskRecord *task;
    FlatVmOptions task_opts;
    FlatTaskNativeBridge bridge;
    FlatTaskId id;
    if (!runtime || !bc || !out_id || runtime->next_task_id == 0) return -1;

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

    id = runtime->next_task_id++;
    task->id = id;
    task->state = initial_state;
    task->result.state = FLAT_VM_READY;
    memset(&bridge, 0, sizeof bridge);
    bridge.user = runtime;
    bridge.task_id = id;
    bridge.spawn = runtime_task_native_spawn;
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
    if (runtime->tasks_tail) runtime->tasks_tail->next = task;
    else runtime->tasks = task;
    runtime->tasks_tail = task;
    runtime->task_count++;
    *out_id = id;
    return 0;
}

int flat_runtime_task_create(FlatRuntime *runtime, const FlatBytecode *bc,
                             const FlatVmOptions *opts, FlatTaskId *out_id) {
    return runtime_task_create_internal(runtime, bc, opts, NULL, FLAT_TASK_CREATED, NULL, 0,
                                        NULL, NULL, NULL, out_id);
}

static int runtime_task_native_spawn(void *user, uint64_t parent_id, const FlatBytecode *bc,
                                     const char *entry_label, const FlatValue *args,
                                     size_t arg_count, FlatCollectionPool *source_coll,
                                     FlatStringPool *source_pool, FlatFileTable *source_files,
                                     uint64_t *out_id) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    FlatTaskRecord *parent = runtime_find_task(runtime, parent_id);
    size_t bi;
    unsigned expected = 0;
    if (!runtime || !parent || parent->state != FLAT_TASK_RUNNING || !bc || !entry_label ||
        !entry_label[0]) return -1;
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
                                        out_id);
}

static int runtime_task_native_wait(void *user, uint64_t waiter_id, uint64_t target_id) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    FlatTaskRecord *waiter;
    FlatTaskRecord *target;
    if (!runtime || waiter_id == 0 || target_id == 0 || waiter_id == target_id) return -1;
    waiter = runtime_find_task(runtime, waiter_id);
    target = runtime_find_task(runtime, target_id);
    if (!waiter || !target || waiter->state != FLAT_TASK_RUNNING ||
        runtime_task_owns_channel_transaction(runtime, waiter_id) ||
        runtime_wait_would_cycle(runtime, waiter_id, target_id)) return -1;
    if (target->state == FLAT_TASK_COMPLETED || target->state == FLAT_TASK_FAILED ||
        target->state == FLAT_TASK_CANCELLED) return 0;
    waiter->waiting_on = target_id;
    waiter->waiting_channel_id = 0;
    waiter->state = FLAT_TASK_WAITING;
    return 1;
}

static int runtime_task_native_state(void *user, uint64_t task_id, int *out_state) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    FlatTaskRecord *task = runtime_find_task(runtime, task_id);
    if (!task || !out_state) return -1;
    *out_state = (int)task->state;
    return 0;
}

static int runtime_task_native_result(void *user, uint64_t task_id,
                                      FlatVmResult *out_result, int *out_task_state) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    FlatTaskRecord *task = runtime_find_task(runtime, task_id);
    if (!task || !out_result || !out_task_state ||
        (task->state != FLAT_TASK_COMPLETED && task->state != FLAT_TASK_FAILED &&
         task->state != FLAT_TASK_CANCELLED)) return -1;
    *out_result = task->result;
    *out_task_state = (int)task->state;
    return 0;
}

static int runtime_task_native_channel_create(void *user, uint64_t task_id,
                                              uint64_t *out_channel_id) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    FlatTaskRecord *task = runtime_find_task(runtime, task_id);
    if (!task || task->state != FLAT_TASK_RUNNING || !out_channel_id) return -1;
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
    FlatTaskRecord *task = runtime_find_task(runtime, task_id);
    int rc;
    if (!task || task->state != FLAT_TASK_RUNNING) return -1;
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
    FlatTaskRecord *task = runtime_find_task(runtime, task_id);
    FlatChannel *channel;
    int rc;
    if (!task || task->state != FLAT_TASK_RUNNING) return -1;
    channel = runtime_channel_acquire(runtime, channel_id);
    if (!channel) return -1;
    rc = runtime_channel_add_user_locked(channel, task_id);
    if (rc == 0)
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
            channel = runtime_channel_acquire(runtime, channel_id);
            if (!channel) return -1;
            rc = runtime_channel_add_user_locked(channel, task_id);
            if (rc == 0)
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
    FlatTaskRecord *task = runtime_find_task(runtime, task_id);
    if (!task || task->state != FLAT_TASK_RUNNING) return -1;
    return flat_runtime_channel_lock(runtime, channel_id, task_id);
}

static int runtime_task_native_channel_unlock(void *user, uint64_t task_id, uint64_t channel_id) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    FlatTaskRecord *task = runtime_find_task(runtime, task_id);
    if (!task || task->state != FLAT_TASK_RUNNING) return -1;
    return flat_runtime_channel_unlock(runtime, channel_id, task_id);
}

static int runtime_task_native_channel_revert(void *user, uint64_t task_id, uint64_t channel_id) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    FlatTaskRecord *task = runtime_find_task(runtime, task_id);
    if (!task || task->state != FLAT_TASK_RUNNING) return -1;
    return flat_runtime_channel_revert(runtime, channel_id, task_id);
}

static int runtime_task_native_channel_destroy(void *user, uint64_t task_id, uint64_t channel_id) {
    FlatRuntime *runtime = (FlatRuntime *)user;
    FlatTaskRecord *task = runtime_find_task(runtime, task_id);
    if (!task || task->state != FLAT_TASK_RUNNING) return -1;
    return runtime_channel_destroy_owned(runtime, channel_id, task_id);
}

int flat_runtime_task_start(FlatRuntime *runtime, FlatTaskId id) {
    FlatTaskRecord *task = runtime_find_task(runtime, id);
    if (!task || task->state != FLAT_TASK_CREATED) return -1;
    task->state = FLAT_TASK_RUNNABLE;
    return 0;
}

static FlatTaskRecord *runtime_next_runnable(FlatRuntime *runtime) {
    FlatTaskRecord *task;
    FlatTaskRecord *first = NULL;
    FlatTaskRecord *first_transaction = NULL;
    FlatTaskRecord *next_transaction = NULL;
    for (task = runtime->tasks; task; task = task->next) {
        if (task->state != FLAT_TASK_RUNNABLE) continue;
        if (!first) first = task;
        if (runtime_task_owns_channel_transaction(runtime, task->id)) {
            if (!first_transaction) first_transaction = task;
            if (task->id > runtime->last_scheduled_id && !next_transaction)
                next_transaction = task;
        }
    }
    if (next_transaction) return next_transaction;
    if (first_transaction) return first_transaction;
    for (task = runtime->tasks; task; task = task->next) {
        if (task->state == FLAT_TASK_RUNNABLE && task->id > runtime->last_scheduled_id)
            return task;
    }
    return first;
}

int flat_runtime_task_wait(FlatRuntime *runtime, FlatTaskId waiter_id, FlatTaskId target_id) {
    FlatTaskRecord *waiter;
    FlatTaskRecord *target;
    if (!runtime || waiter_id == 0 || target_id == 0 || waiter_id == target_id) return -1;
    waiter = runtime_find_task(runtime, waiter_id);
    target = runtime_find_task(runtime, target_id);
    if (!waiter || !target || waiter->state != FLAT_TASK_RUNNABLE ||
        runtime_task_owns_channel_transaction(runtime, waiter_id) ||
        runtime_wait_would_cycle(runtime, waiter_id, target_id)) return -1;
    if (target->state == FLAT_TASK_COMPLETED || target->state == FLAT_TASK_FAILED ||
        target->state == FLAT_TASK_CANCELLED) {
        return 0; /* The dependency has already resolved. */
    }
    waiter->waiting_on = target_id;
    waiter->waiting_channel_id = 0;
    waiter->state = FLAT_TASK_WAITING;
    return 0;
}

int flat_runtime_task_cancel(FlatRuntime *runtime, FlatTaskId id) {
    FlatTaskRecord *task;
    if (!runtime || id == 0) return -1;
    task = runtime_find_task(runtime, id);
    if (!task || (task->state != FLAT_TASK_CREATED &&
                  task->state != FLAT_TASK_RUNNABLE &&
                  task->state != FLAT_TASK_WAITING)) return -1;
    if (flat_vm_cancel(task->vm, &task->result) != 0) return -1;
    task->waiting_on = 0;
    task->waiting_channel_id = 0;
    runtime_abort_task_channel_transactions(runtime, id);
    runtime_release_task_channel_refs(runtime, id);
    task->state = FLAT_TASK_CANCELLED;
    runtime_wake_waiters(runtime, id);
    return 0;
}

int flat_runtime_schedule(FlatRuntime *runtime, uint64_t quantum,
                          uint64_t max_quanta, uint64_t *out_quanta) {
    uint64_t turns = 0;
    if (out_quanta) *out_quanta = 0;
    if (!runtime || quantum == 0) return -1;

    runtime_lock(runtime);
    runtime->active_runs++;
    runtime_unlock(runtime);

    while (max_quanta == 0 || turns < max_quanta) {
        FlatTaskRecord *task = runtime_next_runnable(runtime);
        FlatVmResult result;
        int rc;
        if (!task) break;

        task->state = FLAT_TASK_RUNNING;
        runtime->last_scheduled_id = task->id;
        memset(&result, 0, sizeof result);
        rc = flat_vm_step(task->vm, quantum, &result);
        task->result = result;
        if (task->state == FLAT_TASK_WAITING && rc == 0 && result.state == FLAT_VM_YIELDED) {
            /* task.wait() changed RUNNING -> WAITING inside the native call. */
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
        if (task->state == FLAT_TASK_COMPLETED || task->state == FLAT_TASK_FAILED) {
            runtime_abort_task_channel_transactions(runtime, task->id);
            runtime_release_task_channel_refs(runtime, task->id);
            runtime_wake_waiters(runtime, task->id);
        }
        turns++;
    }

    runtime_lock(runtime);
    if (runtime->active_runs > 0) runtime->active_runs--;
    runtime_unlock(runtime);
    if (out_quanta) *out_quanta = turns;
    return 0;
}

FlatTaskState flat_runtime_task_state(const FlatRuntime *runtime, FlatTaskId id) {
    FlatTaskRecord *task = runtime_find_task(runtime, id);
    return task ? task->state : (FlatTaskState)0;
}

int flat_runtime_task_result(const FlatRuntime *runtime, FlatTaskId id,
                             FlatVmResult *out_result) {
    FlatTaskRecord *task = runtime_find_task(runtime, id);
    if (!task || !out_result ||
        (task->state != FLAT_TASK_COMPLETED && task->state != FLAT_TASK_FAILED &&
         task->state != FLAT_TASK_CANCELLED)) return -1;
    *out_result = task->result;
    return 0;
}

int flat_runtime_task_destroy(FlatRuntime *runtime, FlatTaskId id) {
    FlatTaskRecord *task;
    FlatTaskRecord *prev = NULL;
    if (!runtime || id == 0) return -1;
    for (task = runtime->tasks; task; prev = task, task = task->next) {
        if (task->id == id) break;
    }
    if (!task || runtime_has_waiters(runtime, id) ||
        (task->state != FLAT_TASK_CREATED &&
                  task->state != FLAT_TASK_COMPLETED &&
                  task->state != FLAT_TASK_FAILED &&
                  task->state != FLAT_TASK_CANCELLED)) return -1;
    runtime_abort_task_channel_transactions(runtime, id);
    runtime_release_task_channel_refs(runtime, id);
    if (flat_vm_destroy(task->vm) != 0) return -1;
    if (prev) prev->next = task->next;
    else runtime->tasks = task->next;
    if (runtime->tasks_tail == task) runtime->tasks_tail = prev;
    if (runtime->task_count > 0) runtime->task_count--;
    free(task);
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

static void runtime_release_task_channel_refs(FlatRuntime *runtime, FlatTaskId task_id) {
    FlatChannel *channel;
    if (!runtime || task_id == 0) return;
    runtime_lock(runtime);
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
     * task owns another Channel transaction. Check before taking runtime or
     * Channel gates because the helper traverses and locks the Channel list. */
    if (runtime_task_owns_channel_transaction(runtime, task_id)) return -1;
    runtime_lock(runtime);
    task = runtime_find_task(runtime, task_id);
    channel = runtime_find_channel(runtime, channel_id);
    if (!task || task->state != FLAT_TASK_RUNNING || !channel) {
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
            !runtime_wait_would_cycle(runtime, task_id, owner_id) &&
            runtime_channel_add_user_locked(channel, task_id) == 0) {
            task->waiting_on = owner_id;
            task->waiting_channel_id = channel_id;
            task->state = FLAT_TASK_WAITING;
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
    channel = runtime_channel_acquire(runtime, id);
    if (!channel) return -1;
    if (owner_id && runtime_find_task(runtime, owner_id) &&
        runtime_find_task(runtime, owner_id)->state != FLAT_TASK_COMPLETED &&
        runtime_find_task(runtime, owner_id)->state != FLAT_TASK_FAILED &&
        runtime_find_task(runtime, owner_id)->state != FLAT_TASK_CANCELLED &&
        runtime_channel_add_user_locked(channel, owner_id) != 0) rc = -1;
    else rc = flat_channel_set(channel, owner_id, name, value, source_coll, source_pool, source_files);
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
    channel = runtime_channel_acquire(runtime, id);
    if (!channel) return -1;
    if (owner_id && runtime_find_task(runtime, owner_id) &&
        runtime_find_task(runtime, owner_id)->state != FLAT_TASK_COMPLETED &&
        runtime_find_task(runtime, owner_id)->state != FLAT_TASK_FAILED &&
        runtime_find_task(runtime, owner_id)->state != FLAT_TASK_CANCELLED &&
        runtime_channel_add_user_locked(channel, owner_id) != 0) rc = -1;
    else rc = flat_channel_var(channel, owner_id, name, destination_coll, destination_pool,
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
    owns_transaction = runtime_task_owns_channel_transaction(runtime, owner_id);
    runtime_lock(runtime);
    owner = runtime_find_task(runtime, owner_id);
    channel = runtime_find_channel(runtime, id);
    if (!owner || owner->state == FLAT_TASK_COMPLETED || owner->state == FLAT_TASK_FAILED ||
        owner->state == FLAT_TASK_CANCELLED || !channel) {
        runtime_unlock(runtime);
        return -1;
    }
    flat_channel_lock(channel);
    if (channel->transaction_locked && channel->transaction_owner != owner_id) {
        FlatTaskId transaction_owner = channel->transaction_owner;
        if (owner->state == FLAT_TASK_RUNNING && !owns_transaction &&
            transaction_owner != 0 && runtime_find_task(runtime, transaction_owner) &&
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

int flat_runtime_destroy(FlatRuntime *runtime) {
    int busy;
    if (!runtime) return -1;
    runtime_lock(runtime);
    busy = runtime->active_runs != 0 || runtime->task_count != 0 || runtime->channel_count != 0;
    runtime_unlock(runtime);
    if (busy) return -1;

    flat_host_config_destroy(&runtime->host);
    memset(runtime, 0, sizeof *runtime);
    free(runtime);
    return 0;
}
