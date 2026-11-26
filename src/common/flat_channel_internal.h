#ifndef FLAT_CHANNEL_INTERNAL_H
#define FLAT_CHANNEL_INTERNAL_H

#include "flat_channel.h"
#include "flat_sync.h"

typedef struct FlatChannelEntry {
    char name[FLAT_VAR_NAME_MAX + 1];
    FlatValue value;
    struct FlatChannelEntry *next;
} FlatChannelEntry;

typedef struct FlatChannelTaskRef {
    uint64_t task_id;
    struct FlatChannelTaskRef *next;
} FlatChannelTaskRef;

typedef struct FlatChannel {
    FlatChannelId id;
    FlatMutex gate;
    int gate_initialized;
    FlatCollectionPool *coll;
    FlatStringPool *pool;
    FlatChannelEntry *entries;
    int transaction_locked;
    uint64_t transaction_owner;
    FlatCollectionPool *snapshot_coll;
    FlatStringPool *snapshot_pool;
    FlatChannelEntry *snapshot_entries;
    FlatChannelTaskRef *users;
    struct FlatChannel *next;
} FlatChannel;

FlatChannel *flat_channel_create(FlatChannelId id);
void flat_channel_destroy(FlatChannel *channel);
void flat_channel_lock(FlatChannel *channel);
void flat_channel_unlock(FlatChannel *channel);
int flat_channel_set(FlatChannel *channel, uint64_t owner_id, const char *name,
                     const FlatValue *value, FlatCollectionPool *source_coll,
                     FlatStringPool *source_pool, FlatFileTable *source_files);
int flat_channel_var_unretained(FlatChannel *channel, uint64_t owner_id, const char *name,
                                 FlatCollectionPool *destination_coll, FlatStringPool *destination_pool,
                                 FlatFileTable *destination_files, FlatValue *out_value);
int flat_channel_var(FlatChannel *channel, uint64_t owner_id, const char *name,
                     FlatCollectionPool *destination_coll, FlatStringPool *destination_pool,
                     FlatFileTable *destination_files, FlatValue *out_value);
int flat_channel_tx_lock(FlatChannel *channel, uint64_t owner_id);
int flat_channel_tx_unlock(FlatChannel *channel, uint64_t owner_id);
int flat_channel_tx_revert(FlatChannel *channel, uint64_t owner_id);
int flat_channel_tx_abort(FlatChannel *channel, uint64_t owner_id);

#endif
