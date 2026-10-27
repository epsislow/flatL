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
    int list_count;
    ObjBody objects[FLAT_COLL_MAX_OBJECTS];
    int obj_count;
};

FlatCollectionPool *flat_coll_create(void) {
    return calloc(1, sizeof(FlatCollectionPool));
}

void flat_coll_destroy(FlatCollectionPool *pool) {
    int i;
    if (!pool) {
        return;
    }
    for (i = 0; i < pool->list_count; i++) {
        free(pool->lists[i].items);
    }
    for (i = 0; i < pool->obj_count; i++) {
        free(pool->objects[i].entries);
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

int flat_list_empty(FlatCollectionPool *pool, uint32_t *out_id) {
    ListBody *lb;
    if (!pool || !out_id || pool->list_count >= FLAT_COLL_MAX_LISTS) {
        return -1;
    }
    lb = &pool->lists[pool->list_count];
    memset(lb, 0, sizeof *lb);
    *out_id = (uint32_t)pool->list_count;
    pool->list_count++;
    return 0;
}

int flat_list_len(const FlatCollectionPool *pool, uint32_t list_id, int *out_len) {
    if (!pool || !out_len || list_id >= (uint32_t)pool->list_count) {
        return -1;
    }
    *out_len = pool->lists[list_id].len;
    return 0;
}

int flat_list_get(const FlatCollectionPool *pool, uint32_t list_id, int index, FlatValue *out) {
    const ListBody *lb;
    if (!pool || !out || list_id >= (uint32_t)pool->list_count) {
        return -1;
    }
    lb = &pool->lists[list_id];
    if (index < 0 || index >= lb->len) {
        return -1;
    }
    *out = lb->items[index];
    return 0;
}

static int list_clone_append(FlatCollectionPool *pool, uint32_t src_id, const FlatValue *extra,
                             uint32_t *out_id) {
    const ListBody *src;
    ListBody *dst;
    int i;

    if (!pool || !out_id || src_id >= (uint32_t)pool->list_count) {
        return -1;
    }
    src = &pool->lists[src_id];
    if (flat_list_empty(pool, out_id) != 0) {
        return -1;
    }
    dst = &pool->lists[*out_id];
    if (list_grow(dst, src->len + (extra ? 1 : 0)) != 0) {
        return -1;
    }
    for (i = 0; i < src->len; i++) {
        dst->items[i] = src->items[i];
    }
    dst->len = src->len;
    if (extra) {
        dst->items[dst->len++] = *extra;
    }
    return 0;
}

int flat_list_push_copy(FlatCollectionPool *pool, uint32_t list_id, const FlatValue *item,
                        uint32_t *out_id) {
    return list_clone_append(pool, list_id, item, out_id);
}

int flat_list_build(FlatCollectionPool *pool, const FlatValue *items, int count, uint32_t *out_id) {
    ListBody *lb;
    int i;

    if (!pool || !out_id || count < 0 || count > FLAT_LIST_MAX_ELEMS) {
        return -1;
    }
    if (flat_list_empty(pool, out_id) != 0) {
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
    }
    lb->len = count;
    return 0;
}

int flat_obj_empty(FlatCollectionPool *pool, uint32_t *out_id) {
    ObjBody *ob;
    if (!pool || !out_id || pool->obj_count >= FLAT_COLL_MAX_OBJECTS) {
        return -1;
    }
    ob = &pool->objects[pool->obj_count];
    memset(ob, 0, sizeof *ob);
    *out_id = (uint32_t)pool->obj_count;
    pool->obj_count++;
    return 0;
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
    if (!pool || !out_len || obj_id >= (uint32_t)pool->obj_count) {
        return -1;
    }
    *out_len = pool->objects[obj_id].len;
    return 0;
}

int flat_obj_entry_at(const FlatCollectionPool *pool, uint32_t obj_id, int index,
                      const char **out_key, FlatValue *out_val) {
    const ObjBody *ob;
    if (!pool || index < 0 || obj_id >= (uint32_t)pool->obj_count) {
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

    if (!pool || !key || !out || obj_id >= (uint32_t)pool->obj_count) {
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

int flat_obj_set_copy(FlatCollectionPool *pool, uint32_t obj_id, const char *key,
                      const FlatValue *val, uint32_t *out_id) {
    const ObjBody *src;
    ObjBody *dst;
    int i;
    int ix;

    if (!pool || !key || !val || !out_id || obj_id >= (uint32_t)pool->obj_count) {
        return -1;
    }
    if (strlen(key) > FLAT_OBJ_KEY_MAX) {
        return -1;
    }
    src = &pool->objects[obj_id];
    if (flat_obj_empty(pool, out_id) != 0) {
        return -1;
    }
    dst = &pool->objects[*out_id];
    if (obj_grow(dst, src->len + 1) != 0) {
        return -1;
    }
    for (i = 0; i < src->len; i++) {
        dst->entries[i] = src->entries[i];
    }
    dst->len = src->len;
    ix = obj_find(dst, key);
    if (ix >= 0) {
        dst->entries[ix].val = *val;
    } else {
        if (dst->len >= FLAT_OBJ_MAX_KEYS) {
            return -1;
        }
        snprintf(dst->entries[dst->len].key, sizeof dst->entries[dst->len].key, "%s", key);
        dst->entries[dst->len].val = *val;
        dst->len++;
    }
    return 0;
}

int flat_obj_build(FlatCollectionPool *pool, const char (*keys)[FLAT_OBJ_KEY_MAX + 1],
                   const FlatValue *vals, int count, uint32_t *out_id) {
    ObjBody *ob;
    int i;

    if (!pool || !keys || !vals || !out_id || count < 0 || count > FLAT_OBJ_MAX_KEYS) {
        return -1;
    }
    if (flat_obj_empty(pool, out_id) != 0) {
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
    }
    ob->len = count;
    return 0;
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
