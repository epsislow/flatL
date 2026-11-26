#include "flat_channel_internal.h"
#include "flat_file.h"
#include "flat_vcx.h"

#include <stdlib.h>
#include <string.h>

static void entries_destroy(FlatChannelEntry *entry, FlatCollectionPool *coll,
                           FlatStringPool *pool) {
    while (entry) {
        FlatChannelEntry *next = entry->next;
        flat_value_release(coll, pool, &entry->value);
        free(entry);
        entry = next;
    }
}

static void storage_destroy(FlatChannelEntry *entries, FlatCollectionPool *coll,
                            FlatStringPool *pool) {
    entries_destroy(entries, coll, pool);
    flat_coll_destroy(coll);
    flat_pool_destroy(pool);
}

static int entries_clone(const FlatChannelEntry *source, FlatCollectionPool **out_coll,
                         FlatStringPool **out_pool, FlatCollectionPool *source_coll,
                         FlatStringPool *source_pool, FlatChannelEntry **out_entries) {
    FlatCollectionPool *coll = NULL;
    FlatStringPool *pool = NULL;
    FlatChannelEntry *head = NULL;
    FlatChannelEntry **tail = &head;
    const FlatChannelEntry *entry;
    if (!out_coll || !out_pool || !out_entries || !source_coll || !source_pool) return -1;
    *out_coll = NULL;
    *out_pool = NULL;
    *out_entries = NULL;
    coll = flat_coll_create();
    pool = flat_pool_create();
    if (!coll || !pool) goto fail;
    for (entry = source; entry; entry = entry->next) {
        FlatChannelEntry *copy = (FlatChannelEntry *)calloc(1, sizeof *copy);
        if (!copy) goto fail;
        memcpy(copy->name, entry->name, sizeof copy->name);
        if (flat_value_deep_copy_between(&entry->value, &copy->value, source_coll, source_pool, NULL,
                                         coll, pool, NULL) != 0) {
            free(copy);
            goto fail;
        }
        flat_value_retain(coll, pool, &copy->value);
        *tail = copy;
        tail = &copy->next;
    }
    *out_coll = coll;
    *out_pool = pool;
    *out_entries = head;
    return 0;
fail:
    storage_destroy(head, coll, pool);
    return -1;
}

static int valid_name(const char *name) {
    size_t n;
    if (!name || !name[0]) return 0;
    n = strlen(name);
    return n <= FLAT_VAR_NAME_MAX;
}

static int owner_allowed(const FlatChannel *channel, uint64_t owner_id) {
    return !channel->transaction_locked || channel->transaction_owner == owner_id;
}

FlatChannel *flat_channel_create(FlatChannelId id) {
    FlatChannel *channel = (FlatChannel *)calloc(1, sizeof *channel);
    if (!channel) return NULL;
    channel->id = id;
    if (flat_mutex_init(&channel->gate) != 0) {
        free(channel);
        return NULL;
    }
    channel->gate_initialized = 1;
    channel->coll = flat_coll_create();
    channel->pool = flat_pool_create();
    if (!channel->coll || !channel->pool) {
        flat_channel_destroy(channel);
        return NULL;
    }
    return channel;
}

void flat_channel_lock(FlatChannel *channel) {
    if (!channel) return;
    (void)flat_mutex_lock(&channel->gate);
}

void flat_channel_unlock(FlatChannel *channel) {
    if (channel) (void)flat_mutex_unlock(&channel->gate);
}

void flat_channel_destroy(FlatChannel *channel) {
    FlatChannelTaskRef *user;
    if (!channel) return;
    storage_destroy(channel->entries, channel->coll, channel->pool);
    storage_destroy(channel->snapshot_entries, channel->snapshot_coll, channel->snapshot_pool);
    user = channel->users;
    while (user) { FlatChannelTaskRef *next = user->next; free(user); user = next; }
    if (channel->gate_initialized) (void)flat_mutex_destroy(&channel->gate);
    free(channel);
}

int flat_channel_set(FlatChannel *channel, uint64_t owner_id, const char *name,
                     const FlatValue *value, FlatCollectionPool *source_coll,
                     FlatStringPool *source_pool, FlatFileTable *source_files) {
    FlatValue copy;
    FlatChannelEntry *entry;
    FlatChannelEntry *created = NULL;
    if (!channel || !valid_name(name) || !value || !source_coll || !source_pool) return -1;
    if (!owner_allowed(channel, owner_id)) return -2;
    memset(&copy, 0, sizeof copy);
    if (flat_value_deep_copy_between(value, &copy, source_coll, source_pool, source_files,
                                     channel->coll, channel->pool, NULL) != 0) return -1;
    flat_value_retain(channel->coll, channel->pool, &copy);
    for (entry = channel->entries; entry; entry = entry->next) {
        if (strcmp(entry->name, name) == 0) break;
    }
    if (!entry) {
        created = (FlatChannelEntry *)calloc(1, sizeof *created);
        if (!created) {
            flat_value_release(channel->coll, channel->pool, &copy);
            return -1;
        }
        memcpy(created->name, name, strlen(name) + 1);
        created->next = channel->entries;
        channel->entries = created;
        entry = created;
    } else {
        flat_value_release(channel->coll, channel->pool, &entry->value);
    }
    entry->value = copy;
    return 0;
}

int flat_channel_var_unretained(FlatChannel *channel, uint64_t owner_id, const char *name,
                                FlatCollectionPool *destination_coll,
                                FlatStringPool *destination_pool,
                                FlatFileTable *destination_files, FlatValue *out_value) {
    FlatChannelEntry *entry;
    if (!channel || !valid_name(name) || !destination_coll || !destination_pool || !out_value)
        return -1;
    if (!owner_allowed(channel, owner_id)) return -2;
    for (entry = channel->entries; entry; entry = entry->next) {
        if (strcmp(entry->name, name) == 0) break;
    }
    if (!entry) return -1;
    memset(out_value, 0, sizeof *out_value);
    return flat_value_deep_copy_between(&entry->value, out_value, channel->coll, channel->pool,
                                        NULL, destination_coll, destination_pool,
                                        destination_files);
}

int flat_channel_var(FlatChannel *channel, uint64_t owner_id, const char *name,
                     FlatCollectionPool *destination_coll, FlatStringPool *destination_pool,
                     FlatFileTable *destination_files, FlatValue *out_value) {
    int rc = flat_channel_var_unretained(channel, owner_id, name, destination_coll,
                                         destination_pool, destination_files, out_value);
    if (rc != 0) return rc;
    flat_value_retain(destination_coll, destination_pool, out_value);
    return 0;
}

int flat_channel_tx_lock(FlatChannel *channel, uint64_t owner_id) {
    if (!channel || owner_id == 0 || channel->transaction_locked) return -1;
    if (entries_clone(channel->entries, &channel->snapshot_coll, &channel->snapshot_pool,
                      channel->coll, channel->pool, &channel->snapshot_entries) != 0) return -1;
    channel->transaction_locked = 1;
    channel->transaction_owner = owner_id;
    return 0;
}

int flat_channel_tx_unlock(FlatChannel *channel, uint64_t owner_id) {
    if (!channel || !channel->transaction_locked || channel->transaction_owner != owner_id)
        return -1;
    storage_destroy(channel->snapshot_entries, channel->snapshot_coll, channel->snapshot_pool);
    channel->snapshot_entries = NULL;
    channel->snapshot_coll = NULL;
    channel->snapshot_pool = NULL;
    channel->transaction_locked = 0;
    channel->transaction_owner = 0;
    return 0;
}

int flat_channel_tx_revert(FlatChannel *channel, uint64_t owner_id) {
    FlatCollectionPool *coll = NULL;
    FlatStringPool *pool = NULL;
    FlatChannelEntry *entries = NULL;
    if (!channel || !channel->transaction_locked || channel->transaction_owner != owner_id)
        return -1;
    if (entries_clone(channel->snapshot_entries, &coll, &pool, channel->snapshot_coll,
                      channel->snapshot_pool, &entries) != 0) return -1;
    storage_destroy(channel->entries, channel->coll, channel->pool);
    channel->entries = entries;
    channel->coll = coll;
    channel->pool = pool;
    return 0;
}

int flat_channel_tx_abort(FlatChannel *channel, uint64_t owner_id) {
    FlatChannelEntry *old_entries;
    FlatCollectionPool *old_coll;
    FlatStringPool *old_pool;
    if (!channel || !channel->transaction_locked || channel->transaction_owner != owner_id)
        return -1;
    old_entries = channel->entries;
    old_coll = channel->coll;
    old_pool = channel->pool;
    channel->entries = channel->snapshot_entries;
    channel->coll = channel->snapshot_coll;
    channel->pool = channel->snapshot_pool;
    channel->snapshot_entries = NULL;
    channel->snapshot_coll = NULL;
    channel->snapshot_pool = NULL;
    channel->transaction_locked = 0;
    channel->transaction_owner = 0;
    storage_destroy(old_entries, old_coll, old_pool);
    return 0;
}
