#include "flat_mem_stats.h"
#include "flat_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FlatValue *items;
    int len;
    int cap;
} ListBody;

typedef struct {
    char key[FLAT_OBJ_KEY_MAX + 1];
    FlatValue val;
} ObjEntry;

typedef struct {
    ObjEntry *entries;
    int len;
    int cap;
} ObjBody;

struct FlatCollectionPool {
    ListBody lists[FLAT_COLL_MAX_LISTS];
    int list_bind[FLAT_COLL_MAX_LISTS];
    unsigned char list_active[FLAT_COLL_MAX_LISTS];
    int list_free[FLAT_COLL_MAX_LISTS];
    int list_free_n;

    ObjBody objects[FLAT_COLL_MAX_OBJECTS];
    int obj_bind[FLAT_COLL_MAX_OBJECTS];
    unsigned char obj_active[FLAT_COLL_MAX_OBJECTS];
    int obj_free[FLAT_COLL_MAX_OBJECTS];
    int obj_free_n;

    FlatMemLiveStats *live;
};

static int list_slot_valid(const FlatCollectionPool *pool, uint32_t id) {
    return pool && id < (uint32_t)FLAT_COLL_MAX_LISTS && pool->list_active[id];
}

static int obj_slot_valid(const FlatCollectionPool *pool, uint32_t id) {
    return pool && id < (uint32_t)FLAT_COLL_MAX_OBJECTS && pool->obj_active[id];
}

static void elem_bind(FlatCollectionPool *pool, FlatStringPool *str_pool, const FlatValue *v) {
    flat_value_retain(pool, str_pool, v);
    flat_value_live_bind(pool ? pool->live : NULL, v);
}

static void elem_unbind(FlatCollectionPool *pool, FlatStringPool *str_pool, const FlatValue *v) {
    flat_value_live_unbind(pool ? pool->live : NULL, v);
    flat_value_release(pool, str_pool, v);
}

void flat_coll_set_live_stats(FlatCollectionPool *pool, FlatMemLiveStats *stats) {
    if (pool) {
        pool->live = stats;
    }
}

static int coll_live_list_slots(const FlatCollectionPool *pool) {
    int i;
    int n = 0;
    if (!pool) {
        return 0;
    }
    for (i = 0; i < FLAT_COLL_MAX_LISTS; i++) {
        if (pool->list_active[i] && pool->list_bind[i] > 0) {
            n++;
        }
    }
    return n;
}

static int coll_live_obj_slots(const FlatCollectionPool *pool) {
    int i;
    int n = 0;
    if (!pool) {
        return 0;
    }
    for (i = 0; i < FLAT_COLL_MAX_OBJECTS; i++) {
        if (pool->obj_active[i] && pool->obj_bind[i] > 0) {
            n++;
        }
    }
    return n;
}

static void list_body_release_elements(FlatCollectionPool *coll, FlatStringPool *pool,
                                         const ListBody *lb) {
    int i;
    if (!lb) {
        return;
    }
    for (i = 0; i < lb->len; i++) {
        elem_unbind(coll, pool, &lb->items[i]);
    }
}

static void obj_body_release_entries(FlatCollectionPool *coll, FlatStringPool *pool,
                                     const ObjBody *ob) {
    int i;
    if (!ob) {
        return;
    }
    for (i = 0; i < ob->len; i++) {
        elem_unbind(coll, pool, &ob->entries[i].val);
    }
}

static void list_slot_destroy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t id) {
    ListBody *lb;

    if (!pool || !list_slot_valid(pool, id)) {
        return;
    }
    lb = &pool->lists[id];
    list_body_release_elements(pool, str_pool, lb);
    free(lb->items);
    memset(lb, 0, sizeof *lb);
    pool->list_bind[id] = 0;
    pool->list_active[id] = 0;
    if (pool->list_free_n < FLAT_COLL_MAX_LISTS) {
        pool->list_free[pool->list_free_n++] = (int)id;
    }
}

static void obj_slot_destroy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t id) {
    ObjBody *ob;

    if (!pool || !obj_slot_valid(pool, id)) {
        return;
    }
    ob = &pool->objects[id];
    obj_body_release_entries(pool, str_pool, ob);
    free(ob->entries);
    memset(ob, 0, sizeof *ob);
    pool->obj_bind[id] = 0;
    pool->obj_active[id] = 0;
    if (pool->obj_free_n < FLAT_COLL_MAX_OBJECTS) {
        pool->obj_free[pool->obj_free_n++] = (int)id;
    }
}

static void coll_retain_list(FlatCollectionPool *pool, uint32_t id) {
    if (list_slot_valid(pool, id)) {
        pool->list_bind[id]++;
    }
}

static void coll_release_list(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t id) {
    if (!list_slot_valid(pool, id)) {
        return;
    }
    if (pool->list_bind[id] > 0) {
        pool->list_bind[id]--;
    }
    if (pool->list_bind[id] == 0) {
        list_slot_destroy(pool, str_pool, id);
    }
}

static void coll_retain_obj(FlatCollectionPool *pool, uint32_t id) {
    if (obj_slot_valid(pool, id)) {
        pool->obj_bind[id]++;
    }
}

static void coll_release_obj(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t id) {
    if (!obj_slot_valid(pool, id)) {
        return;
    }
    if (pool->obj_bind[id] > 0) {
        pool->obj_bind[id]--;
    }
    if (pool->obj_bind[id] == 0) {
        obj_slot_destroy(pool, str_pool, id);
    }
}

static void list_reap_idle_slots(FlatCollectionPool *pool, FlatStringPool *str_pool) {
    int i;
    if (!pool) {
        return;
    }
    for (i = 0; i < FLAT_COLL_MAX_LISTS; i++) {
        if (pool->list_active[i] && pool->list_bind[i] == 0) {
            list_slot_destroy(pool, str_pool, (uint32_t)i);
        }
    }
}

static void obj_reap_idle_slots(FlatCollectionPool *pool, FlatStringPool *str_pool) {
    int i;
    if (!pool) {
        return;
    }
    for (i = 0; i < FLAT_COLL_MAX_OBJECTS; i++) {
        if (pool->obj_active[i] && pool->obj_bind[i] == 0) {
            obj_slot_destroy(pool, str_pool, (uint32_t)i);
        }
    }
}

static int list_slot_alloc(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t *out_id) {
    uint32_t id;

    if (!pool || !out_id) {
        return -1;
    }
    list_reap_idle_slots(pool, str_pool);
    if (coll_live_list_slots(pool) >= FLAT_COLL_MAX_LISTS) {
        return -1;
    }
    if (pool->list_free_n > 0) {
        id = (uint32_t)pool->list_free[--pool->list_free_n];
    } else {
        for (id = 0; id < (uint32_t)FLAT_COLL_MAX_LISTS; id++) {
            if (!pool->list_active[id]) {
                break;
            }
        }
        if (id >= (uint32_t)FLAT_COLL_MAX_LISTS) {
            return -1;
        }
    }
    memset(&pool->lists[id], 0, sizeof pool->lists[id]);
    pool->list_bind[id] = 0;
    pool->list_active[id] = 1;
    *out_id = id;
    return 0;
}

static int obj_slot_alloc(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t *out_id) {
    uint32_t id;

    if (!pool || !out_id) {
        return -1;
    }
    obj_reap_idle_slots(pool, str_pool);
    if (coll_live_obj_slots(pool) >= FLAT_COLL_MAX_OBJECTS) {
        return -1;
    }
    if (pool->obj_free_n > 0) {
        id = (uint32_t)pool->obj_free[--pool->obj_free_n];
    } else {
        for (id = 0; id < (uint32_t)FLAT_COLL_MAX_OBJECTS; id++) {
            if (!pool->obj_active[id]) {
                break;
            }
        }
        if (id >= (uint32_t)FLAT_COLL_MAX_OBJECTS) {
            return -1;
        }
    }
    memset(&pool->objects[id], 0, sizeof pool->objects[id]);
    pool->obj_bind[id] = 0;
    pool->obj_active[id] = 1;
    *out_id = id;
    return 0;
}

void flat_coll_reap_idle(FlatCollectionPool *pool, FlatStringPool *str_pool) {
    list_reap_idle_slots(pool, str_pool);
    obj_reap_idle_slots(pool, str_pool);
}

int flat_coll_live_list_count(const FlatCollectionPool *pool) {
    return coll_live_list_slots(pool);
}

int flat_coll_live_object_count(const FlatCollectionPool *pool) {
    return coll_live_obj_slots(pool);
}

FlatCollectionPool *flat_coll_create(void) {
    return calloc(1, sizeof(FlatCollectionPool));
}

void flat_coll_destroy(FlatCollectionPool *pool) {
    int i;
    if (!pool) {
        return;
    }
    for (i = 0; i < FLAT_COLL_MAX_LISTS; i++) {
        if (pool->list_active[i]) {
            pool->list_bind[i] = 1;
            list_slot_destroy(pool, NULL, (uint32_t)i);
        }
    }
    for (i = 0; i < FLAT_COLL_MAX_OBJECTS; i++) {
        if (pool->obj_active[i]) {
            pool->obj_bind[i] = 1;
            obj_slot_destroy(pool, NULL, (uint32_t)i);
        }
    }
    free(pool);
}

static int list_grow(ListBody *lb, int need) {
    FlatValue *n;
    int ncap;
    if (need <= lb->cap) {
        return 0;
    }
    ncap = lb->cap ? lb->cap * 2 : 8;
    while (ncap < need) {
        ncap *= 2;
    }
    if (ncap > FLAT_LIST_MAX_ELEMS) {
        return -1;
    }
    n = realloc(lb->items, (size_t)ncap * sizeof(FlatValue));
    if (!n) {
        return -1;
    }
    lb->items = n;
    lb->cap = ncap;
    return 0;
}

static int obj_grow(ObjBody *ob, int need) {
    ObjEntry *n;
    int ncap;
    if (need <= ob->cap) {
        return 0;
    }
    ncap = ob->cap ? ob->cap * 2 : 8;
    while (ncap < need) {
        ncap *= 2;
    }
    if (ncap > FLAT_OBJ_MAX_KEYS) {
        return -1;
    }
    n = realloc(ob->entries, (size_t)ncap * sizeof(ObjEntry));
    if (!n) {
        return -1;
    }
    ob->entries = n;
    ob->cap = ncap;
    return 0;
}

int flat_list_empty(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t *out_id) {
    return list_slot_alloc(pool, str_pool, out_id);
}

int flat_list_len(const FlatCollectionPool *pool, uint32_t list_id, int *out_len) {
    if (!pool || !out_len || !list_slot_valid(pool, list_id)) {
        return -1;
    }
    *out_len = pool->lists[list_id].len;
    return 0;
}

int flat_list_get(const FlatCollectionPool *pool, uint32_t list_id, int index, FlatValue *out) {
    const ListBody *lb;
    if (!pool || !out || !list_slot_valid(pool, list_id)) {
        return -1;
    }
    lb = &pool->lists[list_id];
    if (index < 0 || index >= lb->len) {
        return -1;
    }
    *out = lb->items[index];
    return 0;
}

static int list_clone_append(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t src_id,
                             const FlatValue *extra, uint32_t *out_id) {
    const ListBody *src;
    ListBody *dst;
    int i;

    if (!pool || !out_id || !list_slot_valid(pool, src_id)) {
        return -1;
    }
    src = &pool->lists[src_id];
    if (flat_list_empty(pool, str_pool, out_id) != 0) {
        return -1;
    }
    dst = &pool->lists[*out_id];
    if (list_grow(dst, src->len + (extra ? 1 : 0)) != 0) {
        return -1;
    }
    for (i = 0; i < src->len; i++) {
        dst->items[i] = src->items[i];
        elem_bind(pool, str_pool, &dst->items[i]);
    }
    dst->len = src->len;
    if (extra) {
        dst->items[dst->len++] = *extra;
        elem_bind(pool, str_pool, extra);
    }
    return 0;
}

int flat_list_push_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t list_id,
                        const FlatValue *item, uint32_t *out_id) {
    return list_clone_append(pool, str_pool, list_id, item, out_id);
}

int flat_list_insert_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t list_id,
                          int index, const FlatValue *item, uint32_t *out_id) {
    const ListBody *src;
    ListBody *dst;
    int i;

    if (!pool || !item || !out_id || !list_slot_valid(pool, list_id)) {
        return -1;
    }
    src = &pool->lists[list_id];
    if (index < 0 || index > src->len) {
        return -1;
    }
    if (flat_list_empty(pool, str_pool, out_id) != 0) {
        return -1;
    }
    dst = &pool->lists[*out_id];
    if (list_grow(dst, src->len + 1) != 0) {
        return -1;
    }
    for (i = 0; i < index; i++) {
        dst->items[i] = src->items[i];
        elem_bind(pool, str_pool, &dst->items[i]);
    }
    dst->items[index] = *item;
    elem_bind(pool, str_pool, item);
    for (i = index; i < src->len; i++) {
        dst->items[i + 1] = src->items[i];
        elem_bind(pool, str_pool, &dst->items[i + 1]);
    }
    dst->len = src->len + 1;
    return 0;
}

int flat_list_prepend_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t list_id,
                           const FlatValue *item, uint32_t *out_id) {
    return flat_list_insert_copy(pool, str_pool, list_id, 0, item, out_id);
}

int flat_list_set_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t list_id,
                       int index, const FlatValue *val, uint32_t *out_id) {
    const ListBody *src;
    ListBody *dst;
    int i;

    if (!pool || !val || !out_id || !list_slot_valid(pool, list_id)) {
        return -1;
    }
    src = &pool->lists[list_id];
    if (index < 0 || index >= src->len) {
        return -1;
    }
    if (flat_list_empty(pool, str_pool, out_id) != 0) {
        return -1;
    }
    dst = &pool->lists[*out_id];
    if (list_grow(dst, src->len) != 0) {
        return -1;
    }
    for (i = 0; i < src->len; i++) {
        dst->items[i] = src->items[i];
        elem_bind(pool, str_pool, &dst->items[i]);
    }
    dst->len = src->len;
    elem_unbind(pool, str_pool, &dst->items[index]); /* drop the copy made above */
    dst->items[index] = *val;
    elem_bind(pool, str_pool, val);
    return 0;
}

int flat_list_remove_at_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t list_id,
                             int index, uint32_t *out_id) {
    const ListBody *src;
    ListBody *dst;
    int i;

    if (!pool || !out_id || !list_slot_valid(pool, list_id)) {
        return -1;
    }
    src = &pool->lists[list_id];
    if (index < 0 || index >= src->len) {
        return -1;
    }
    if (flat_list_empty(pool, str_pool, out_id) != 0) {
        return -1;
    }
    dst = &pool->lists[*out_id];
    if (src->len - 1 > 0 && list_grow(dst, src->len - 1) != 0) {
        return -1;
    }
    for (i = 0; i < index; i++) {
        dst->items[i] = src->items[i];
        elem_bind(pool, str_pool, &dst->items[i]);
    }
    for (i = index + 1; i < src->len; i++) {
        dst->items[i - 1] = src->items[i];
        elem_bind(pool, str_pool, &dst->items[i - 1]);
    }
    dst->len = src->len - 1;
    return 0;
}

int flat_list_slice_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t list_id,
                         int start, int end, uint32_t *out_id) {
    const ListBody *src;
    ListBody *dst;
    int n;
    int i;

    if (!pool || !out_id || !list_slot_valid(pool, list_id)) {
        return -1;
    }
    src = &pool->lists[list_id];
    if (start < 0 || end < start || end > src->len) {
        return -1;
    }
    n = end - start;
    if (flat_list_empty(pool, str_pool, out_id) != 0) {
        return -1;
    }
    dst = &pool->lists[*out_id];
    if (n > 0 && list_grow(dst, n) != 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        dst->items[i] = src->items[start + i];
        elem_bind(pool, str_pool, &dst->items[i]);
    }
    dst->len = n;
    return 0;
}

int flat_list_splice_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t list_id,
                          int start, int count, uint32_t *out_id) {
    const ListBody *src;
    ListBody *dst;
    int new_len;
    int i;
    int j;

    if (!pool || !out_id || !list_slot_valid(pool, list_id)) {
        return -1;
    }
    src = &pool->lists[list_id];
    if (start < 0 || count < 0 || start + count > src->len) {
        return -1;
    }
    new_len = src->len - count;
    if (flat_list_empty(pool, str_pool, out_id) != 0) {
        return -1;
    }
    dst = &pool->lists[*out_id];
    if (new_len > 0 && list_grow(dst, new_len) != 0) {
        return -1;
    }
    j = 0;
    for (i = 0; i < src->len; i++) {
        if (i >= start && i < start + count) {
            continue;
        }
        dst->items[j] = src->items[i];
        elem_bind(pool, str_pool, &dst->items[j]);
        j++;
    }
    dst->len = new_len;
    return 0;
}

int flat_list_index_of(const FlatCollectionPool *pool, const FlatStringPool *str_pool,
                       uint32_t list_id, const FlatValue *needle, int *out_index) {
    const ListBody *lb;
    int i;

    if (!pool || !needle || !out_index || !list_slot_valid(pool, list_id)) {
        return -1;
    }
    lb = &pool->lists[list_id];
    for (i = 0; i < lb->len; i++) {
        if (flat_value_equals(&lb->items[i], needle, str_pool)) {
            *out_index = i;
            return 0;
        }
    }
    *out_index = -1;
    return 0;
}

int flat_list_build(FlatCollectionPool *pool, FlatStringPool *str_pool, const FlatValue *items,
                    int count, uint32_t *out_id) {
    ListBody *lb;
    int i;

    if (!pool || !out_id || count < 0 || count > FLAT_LIST_MAX_ELEMS) {
        return -1;
    }
    if (flat_list_empty(pool, str_pool, out_id) != 0) {
        return -1;
    }
    lb = &pool->lists[*out_id];
    if (count == 0) {
        return 0;
    }
    if (list_grow(lb, count) != 0) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        lb->items[i] = items[i];
        elem_bind(pool, str_pool, &lb->items[i]);
    }
    lb->len = count;
    return 0;
}

int flat_obj_empty(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t *out_id) {
    return obj_slot_alloc(pool, str_pool, out_id);
}

static int obj_find(const ObjBody *ob, const char *key) {
    int i;
    for (i = 0; i < ob->len; i++) {
        if (strcmp(ob->entries[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

int flat_obj_len(const FlatCollectionPool *pool, uint32_t obj_id, int *out_len) {
    if (!pool || !out_len || !obj_slot_valid(pool, obj_id)) {
        return -1;
    }
    *out_len = pool->objects[obj_id].len;
    return 0;
}

int flat_obj_entry_at(const FlatCollectionPool *pool, uint32_t obj_id, int index,
                      const char **out_key, FlatValue *out_val) {
    const ObjBody *ob;
    if (!pool || index < 0 || !obj_slot_valid(pool, obj_id)) {
        return -1;
    }
    ob = &pool->objects[obj_id];
    if (index >= ob->len) {
        return -1;
    }
    if (out_key) {
        *out_key = ob->entries[index].key;
    }
    if (out_val) {
        *out_val = ob->entries[index].val;
    }
    return 0;
}

int flat_obj_get(const FlatCollectionPool *pool, uint32_t obj_id, const char *key,
                 FlatValue *out) {
    const ObjBody *ob;
    int ix;

    if (!pool || !key || !out || !obj_slot_valid(pool, obj_id)) {
        return -1;
    }
    ob = &pool->objects[obj_id];
    ix = obj_find(ob, key);
    if (ix < 0) {
        return -1;
    }
    *out = ob->entries[ix].val;
    return 0;
}

int flat_obj_set_copy(FlatCollectionPool *pool, FlatStringPool *str_pool, uint32_t obj_id,
                      const char *key, const FlatValue *val, uint32_t *out_id) {
    const ObjBody *src;
    ObjBody *dst;
    int i;
    int ix;

    if (!pool || !key || !val || !out_id || !obj_slot_valid(pool, obj_id)) {
        return -1;
    }
    if (strlen(key) > FLAT_OBJ_KEY_MAX) {
        return -1;
    }
    src = &pool->objects[obj_id];
    if (flat_obj_empty(pool, str_pool, out_id) != 0) {
        return -1;
    }
    dst = &pool->objects[*out_id];
    if (obj_grow(dst, src->len + 1) != 0) {
        return -1;
    }
    for (i = 0; i < src->len; i++) {
        dst->entries[i] = src->entries[i];
        elem_bind(pool, str_pool, &dst->entries[i].val);
    }
    dst->len = src->len;
    ix = obj_find(dst, key);
    if (ix >= 0) {
        elem_unbind(pool, str_pool, &dst->entries[ix].val); /* drop the copy made above */
        dst->entries[ix].val = *val;
        elem_bind(pool, str_pool, val);
    } else {
        if (dst->len >= FLAT_OBJ_MAX_KEYS) {
            return -1;
        }
        snprintf(dst->entries[dst->len].key, sizeof dst->entries[dst->len].key, "%s", key);
        dst->entries[dst->len].val = *val;
        elem_bind(pool, str_pool, val);
        dst->len++;
    }
    return 0;
}

int flat_obj_build(FlatCollectionPool *pool, FlatStringPool *str_pool,
                   const char (*keys)[FLAT_OBJ_KEY_MAX + 1], const FlatValue *vals, int count,
                   uint32_t *out_id) {
    ObjBody *ob;
    int i;

    if (!pool || !keys || !vals || !out_id || count < 0 || count > FLAT_OBJ_MAX_KEYS) {
        return -1;
    }
    if (flat_obj_empty(pool, str_pool, out_id) != 0) {
        return -1;
    }
    ob = &pool->objects[*out_id];
    if (count == 0) {
        return 0;
    }
    if (obj_grow(ob, count) != 0) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        snprintf(ob->entries[i].key, sizeof ob->entries[i].key, "%s", keys[i]);
        ob->entries[i].val = vals[i];
        elem_bind(pool, str_pool, &vals[i]);
    }
    ob->len = count;
    return 0;
}

void flat_value_retain(FlatCollectionPool *coll, FlatStringPool *pool, const FlatValue *v) {
    if (!v) {
        return;
    }
    switch (v->tag) {
    case VAL_STR_REF:
        flat_pool_retain(pool, v->u.ref_id);
        break;
    case VAL_LIST:
        coll_retain_list(coll, v->u.coll_id);
        break;
    case VAL_OBJECT:
        coll_retain_obj(coll, v->u.coll_id);
        break;
    default:
        break;
    }
}

void flat_value_release(FlatCollectionPool *coll, FlatStringPool *pool, const FlatValue *v) {
    if (!v) {
        return;
    }
    switch (v->tag) {
    case VAL_STR_REF:
        flat_pool_release(pool, v->u.ref_id);
        break;
    case VAL_LIST:
        coll_release_list(coll, pool, v->u.coll_id);
        break;
    case VAL_OBJECT:
        coll_release_obj(coll, pool, v->u.coll_id);
        break;
    default:
        break;
    }
}

int flat_value_set_list(FlatValue *v, uint32_t list_id) {
    if (!v) {
        return -1;
    }
    v->tag = VAL_LIST;
    v->u.coll_id = list_id;
    return 0;
}

int flat_value_set_object(FlatValue *v, uint32_t obj_id) {
    if (!v) {
        return -1;
    }
    v->tag = VAL_OBJECT;
    v->u.coll_id = obj_id;
    return 0;
}
