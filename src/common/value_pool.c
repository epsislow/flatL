#include "flat_value.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *data;
    size_t len;
    int parent_id;
    size_t offset;
    int refcnt;
} PoolSlot;

struct FlatStringPool {
    PoolSlot slots[FLAT_POOL_MAX_SLOTS];
    int slot_count;
    size_t total_bytes;
};

FlatStringPool *flat_pool_create(void) {
    return calloc(1, sizeof(FlatStringPool));
}

void flat_pool_destroy(FlatStringPool *pool) {
    int i;
    if (!pool) {
        return;
    }
    for (i = 0; i < pool->slot_count; i++) {
        PoolSlot *s = &pool->slots[i];
        if (s->parent_id < 0 && s->data) {
            free(s->data);
        }
    }
    free(pool);
}

static const uint8_t *pool_resolve(const FlatStringPool *pool, int id, size_t *out_len) {
    const PoolSlot *s;
    if (!pool || id < 0 || id >= pool->slot_count) {
        return NULL;
    }
    s = &pool->slots[id];
    if (s->parent_id >= 0) {
        const uint8_t *base = pool_resolve(pool, s->parent_id, NULL);
        if (!base || s->offset + s->len > pool->slots[s->parent_id].len) {
            return NULL;
        }
        if (out_len) {
            *out_len = s->len;
        }
        return base + s->offset;
    }
    if (out_len) {
        *out_len = s->len;
    }
    return s->data;
}

const uint8_t *flat_pool_read(const FlatStringPool *pool, uint32_t id, size_t *out_len) {
    return pool_resolve(pool, (int)id, out_len);
}

int flat_pool_alloc_copy(FlatStringPool *pool, const void *data, size_t len, uint32_t *out_id) {
    PoolSlot *s;
    if (!pool || !data || !out_id || len == 0) {
        return -1;
    }
    if (len > FLAT_POOL_MAX_CHUNK || pool->total_bytes + len > FLAT_POOL_MAX_TOTAL) {
        return -1;
    }
    if (pool->slot_count >= FLAT_POOL_MAX_SLOTS) {
        return -1;
    }
    s = &pool->slots[pool->slot_count];
    s->data = malloc(len);
    if (!s->data) {
        return -1;
    }
    memcpy(s->data, data, len);
    s->len = len;
    s->parent_id = -1;
    s->offset = 0;
    s->refcnt = 1;
    pool->total_bytes += len;
    *out_id = (uint32_t)pool->slot_count;
    pool->slot_count++;
    return 0;
}

int flat_pool_slice(FlatStringPool *pool, uint32_t parent_id, size_t offset, size_t len,
                    uint32_t *out_id) {
    PoolSlot *parent;
    PoolSlot *s;
    size_t plen;

    if (!pool || !out_id || parent_id >= (uint32_t)pool->slot_count) {
        return -1;
    }
    parent = &pool->slots[parent_id];
    plen = parent->len;
    if (offset + len > plen) {
        return -1;
    }
    if (pool->slot_count >= FLAT_POOL_MAX_SLOTS) {
        return -1;
    }
    parent->refcnt++;
    s = &pool->slots[pool->slot_count];
    s->data = NULL;
    s->len = len;
    s->parent_id = (int)parent_id;
    s->offset = offset;
    s->refcnt = 1;
    *out_id = (uint32_t)pool->slot_count;
    pool->slot_count++;
    return 0;
}

int flat_value_set_bool(FlatValue *v, int b) {
    if (!v) {
        return -1;
    }
    v->tag = VAL_BOOL;
    v->u.boolean = b ? 1 : 0;
    return 0;
}

int flat_value_set_number(FlatValue *v, double n) {
    if (!v) {
        return -1;
    }
    v->tag = VAL_NUMBER;
    v->u.number = n;
    return 0;
}

int flat_value_set_str_inline(FlatValue *v, const char *s, size_t len) {
    if (!v || !s) {
        return -1;
    }
    if (len > FLAT_INLINE_STR_MAX) {
        return -1;
    }
    v->tag = VAL_STR_INLINE;
    memcpy(v->u.ins.bytes, s, len);
    v->u.ins.bytes[len] = '\0';
    v->u.ins.len = len;
    return 0;
}

int flat_value_set_str_ref(FlatValue *v, uint32_t ref_id) {
    if (!v) {
        return -1;
    }
    v->tag = VAL_STR_REF;
    v->u.ref_id = ref_id;
    return 0;
}

int flat_value_str_equal_cstr(const FlatValue *v, const FlatStringPool *pool, const char *lit,
                              size_t lit_len) {
    if (!v || !lit) {
        return 0;
    }
    if (v->tag == VAL_STR_INLINE) {
        return v->u.ins.len == lit_len && memcmp(v->u.ins.bytes, lit, lit_len) == 0;
    }
    if (v->tag == VAL_STR_REF && pool) {
        size_t len = 0;
        const uint8_t *p = pool_resolve(pool, (int)v->u.ref_id, &len);
        return p && len == lit_len && memcmp(p, lit, lit_len) == 0;
    }
    return 0;
}

static int str_values_equal(const FlatValue *a, const FlatValue *b, const FlatStringPool *pool) {
    size_t la = 0, lb = 0;
    const uint8_t *pa = NULL;
    const uint8_t *pb = NULL;

    if (a->tag == VAL_STR_INLINE) {
        pa = (const uint8_t *)a->u.ins.bytes;
        la = a->u.ins.len;
    } else if (a->tag == VAL_STR_REF) {
        pa = pool_resolve(pool, (int)a->u.ref_id, &la);
    } else {
        return 0;
    }
    if (b->tag == VAL_STR_INLINE) {
        pb = (const uint8_t *)b->u.ins.bytes;
        lb = b->u.ins.len;
    } else if (b->tag == VAL_STR_REF) {
        pb = pool_resolve(pool, (int)b->u.ref_id, &lb);
    } else {
        return 0;
    }
    return pa && pb && la == lb && memcmp(pa, pb, la) == 0;
}

int flat_value_equals(const FlatValue *a, const FlatValue *b, const FlatStringPool *pool) {
    if (!a || !b) {
        return 0;
    }
    if (a->tag == VAL_STR_INLINE || a->tag == VAL_STR_REF) {
        if (b->tag == VAL_STR_INLINE || b->tag == VAL_STR_REF) {
            return str_values_equal(a, b, pool);
        }
        return 0;
    }
    if (a->tag != b->tag) {
        return 0;
    }
    switch (a->tag) {
    case VAL_BOOL:
        return a->u.boolean == b->u.boolean;
    case VAL_NUMBER:
        return a->u.number == b->u.number;
    case VAL_LIST:
    case VAL_OBJECT:
        return a->u.coll_id == b->u.coll_id;
    default:
        return 0;
    }
}

int flat_value_compare_number(const FlatValue *v, double rhs, int op) {
    double n;
    if (!v || v->tag != VAL_NUMBER) {
        return 0;
    }
    n = v->u.number;
    switch (op) {
    case 0:
        return n == rhs;
    case 1:
        return n >= rhs;
    case 2:
        return n <= rhs;
    case 3:
        return n > rhs;
    case 4:
        return n < rhs;
    default:
        return 0;
    }
}

int flat_value_get_cstr(const FlatValue *v, const FlatStringPool *pool, char *buf, size_t buf_size) {
    if (!v || !buf || buf_size == 0) {
        return -1;
    }
    if (v->tag == VAL_STR_INLINE) {
        size_t n = v->u.ins.len;
        if (n >= buf_size) {
            n = buf_size - 1;
        }
        memcpy(buf, v->u.ins.bytes, n);
        buf[n] = '\0';
        return 0;
    }
    if (v->tag == VAL_STR_REF && pool) {
        size_t len = 0;
        const uint8_t *p = pool_resolve(pool, (int)v->u.ref_id, &len);
        size_t n;
        if (!p) {
            buf[0] = '\0';
            return -1;
        }
        n = len;
        if (n >= buf_size) {
            n = buf_size - 1;
        }
        memcpy(buf, p, n);
        buf[n] = '\0';
        return 0;
    }
    buf[0] = '\0';
    return -1;
}
