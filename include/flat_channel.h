#ifndef FLAT_CHANNEL_H
#define FLAT_CHANNEL_H

#include "flat_runtime.h"
#include "flat_value.h"

typedef struct FlatFileTable FlatFileTable;

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t FlatChannelId;

/* Create an empty Channel owned by runtime. IDs are monotone and never reused. */
int flat_runtime_channel_create(FlatRuntime *runtime, FlatChannelId *out_id);
/* Store a deep copy of value. owner_id is the calling task ID, or zero for host calls.
 * Returns -2 when another task owns an active transaction on this Channel. */
int flat_runtime_channel_set(FlatRuntime *runtime, FlatChannelId id, uint64_t owner_id,
                             const char *name, const FlatValue *value,
                             FlatCollectionPool *source_coll, FlatStringPool *source_pool,
                             FlatFileTable *source_files);
/* Return a deep copy into destination pools. Caller owns it and must flat_value_release().
 * Returns -2 when another task owns an active transaction on this Channel. */
int flat_runtime_channel_var(FlatRuntime *runtime, FlatChannelId id, uint64_t owner_id,
                             const char *name, FlatCollectionPool *destination_coll,
                             FlatStringPool *destination_pool, FlatFileTable *destination_files,
                             FlatValue *out_value);
/* Begin an explicit transaction owned by an existing nonterminal task ID.
 * Returns 1 when a scheduler task was placed in WAITING on the current owner;
 * its native instruction must be retried after wakeup. */
int flat_runtime_channel_lock(FlatRuntime *runtime, FlatChannelId id, uint64_t owner_id);
/* Commit the owner's transaction and release its logical lock. */
int flat_runtime_channel_unlock(FlatRuntime *runtime, FlatChannelId id, uint64_t owner_id);
/* Restore the transaction snapshot but keep the transaction lock held. */
int flat_runtime_channel_revert(FlatRuntime *runtime, FlatChannelId id, uint64_t owner_id);
/* Destroy an unlocked Channel. */
int flat_runtime_channel_destroy(FlatRuntime *runtime, FlatChannelId id);

#ifdef __cplusplus
}
#endif

#endif
