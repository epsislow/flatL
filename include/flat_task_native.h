#ifndef FLAT_TASK_NATIVE_H
#define FLAT_TASK_NATIVE_H

#include "fb_format.h"
#include "flat_value.h"
#include "vm.h"
#include <stdint.h>

#include "flat_runtime_event.h"

struct FlatFileTable;

typedef struct FlatTaskNativeBridge {
    void *user;
    uint64_t task_id;
    int yield_requested;
    /* Retry the current native instruction after a scheduler wakeup. Unlike
     * yield_requested alone, this keeps the VM IP on the OP_NATIVE instruction. */
    int retry_requested;
    int (*spawn)(void *user, uint64_t parent_id, const FlatBytecode *bc,
                 const char *entry_label, const FlatValue *args, size_t arg_count,
                 FlatCollectionPool *source_coll, FlatStringPool *source_pool,
                 struct FlatFileTable *source_files, uint64_t *out_id);
    int (*spawn_for)(void *user, uint64_t parent_id, const FlatBytecode *bc,
                     uint64_t source_id, const char *source_kind, const char *event_name,
                     const char *handler_label, const FlatValue *args, size_t arg_count,
                     FlatCollectionPool *source_coll, FlatStringPool *source_pool,
                     struct FlatFileTable *source_files,
                     const FlatEventSubscriptionOptions *options,
                     uint64_t *out_subscription_id);
    int (*stop_for)(void *user, uint64_t parent_id, uint64_t subscription_id);
    int (*wait)(void *user, uint64_t waiter_id, uint64_t target_id);
    int (*state)(void *user, uint64_t task_id, int *out_state);
    int (*result)(void *user, uint64_t task_id, FlatVmResult *out_result,
                  int *out_task_state);
    int (*channel_create)(void *user, uint64_t task_id, uint64_t *out_channel_id);
    int (*channel_set)(void *user, uint64_t task_id, uint64_t channel_id,
                       const char *name, const FlatValue *value,
                       FlatCollectionPool *source_coll, FlatStringPool *source_pool,
                       struct FlatFileTable *source_files);
    int (*channel_var)(void *user, uint64_t task_id, uint64_t channel_id,
                       const char *name, FlatCollectionPool *destination_coll,
                       FlatStringPool *destination_pool,
                       struct FlatFileTable *destination_files, FlatValue *out_value);
    int (*channel_lock)(void *user, uint64_t task_id, uint64_t channel_id);
    int (*channel_unlock)(void *user, uint64_t task_id, uint64_t channel_id);
    int (*channel_revert)(void *user, uint64_t task_id, uint64_t channel_id);
    int (*channel_destroy)(void *user, uint64_t task_id, uint64_t channel_id);
} FlatTaskNativeBridge;

#endif
