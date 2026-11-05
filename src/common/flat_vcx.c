#include "flat_vcx.h"
#include "flat_file.h"
#include "flat_mem_stats.h"
#include "flat_value.h"

#include <stdlib.h>
#include <string.h>

typedef struct FlatVcxContext {
    uint32_t id;
    uint32_t capacity;
    FlatVcxVar *vars;
    int var_count;
} FlatVcxContext;

struct FlatVcxRuntime {
    FlatVcxContext *contexts;
    int context_count;
    int active_ix;
    uint32_t next_id;
    FlatVcxVmBind bind;
    int bind_attached;
};

static FlatVcxRuntime *g_vcx_active;

void flat_vcx_runtime_set_active(FlatVcxRuntime *rt) {
    g_vcx_active = rt;
}

FlatVcxRuntime *flat_vcx_runtime_active(void) {
    return g_vcx_active;
}

static int ctx_index_by_id(const FlatVcxRuntime *rt, uint32_t id) {
    int i;
    if (!rt) {
        return -1;
    }
    for (i = 0; i < rt->context_count; i++) {
        if (rt->contexts[i].id == id) {
            return i;
        }
    }
    return -1;
}

static void sync_bind_var_count_to_active(FlatVcxRuntime *rt) {
    FlatVcxContext *act;
    if (!rt || !rt->bind_attached || !rt->bind.var_count) {
        return;
    }
    act = &rt->contexts[rt->active_ix];
    act->var_count = *rt->bind.var_count;
}

static void refresh_bind_from_active(FlatVcxRuntime *rt) {
    FlatVcxContext *act;
    if (!rt || !rt->bind_attached || !rt->bind.vars || !rt->bind.var_count ||
        !rt->bind.var_slots_max) {
        return;
    }
    act = &rt->contexts[rt->active_ix];
    *rt->bind.vars = act->vars;
    *rt->bind.var_count = act->var_count;
    *rt->bind.var_slots_max = act->capacity;
}

static int value_same_storage(const FlatValue *a, const FlatValue *b) {
    if (!a || !b || a->tag != b->tag) {
        return 0;
    }
    switch (a->tag) {
    case VAL_STR_REF:
        return a->u.ref_id == b->u.ref_id;
    case VAL_LIST:
    case VAL_OBJECT:
        return a->u.coll_id == b->u.coll_id;
    case VAL_FILE:
        return a->u.file_id == b->u.file_id;
    default:
        return 0;
    }
}

static int ctx_find_var(const FlatVcxContext *ctx, const char *name) {
    int i;
    if (!ctx || !name) {
        return -1;
    }
    for (i = 0; i < ctx->var_count; i++) {
        if (strcmp(ctx->vars[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int var_name_forbidden(const char *name) {
    return strcmp(name, "null") == 0 || strcmp(name, "true") == 0 || strcmp(name, "false") == 0;
}

static int ctx_set_var(FlatVcxContext *ctx, const char *name, const FlatValue *val,
                       FlatCollectionPool *coll, FlatStringPool *pool, FlatFileTable *files,
                       FlatMemLiveStats *live) {
    int i;

    if (!ctx || !name || !val) {
        return -1;
    }
    i = ctx_find_var(ctx, name);
    if (i >= 0) {
        if (value_same_storage(&ctx->vars[i].val, val)) {
            return 0;
        }
        if (ctx->vars[i].val.tag == VAL_FILE && files) {
            flat_file_release(files, ctx->vars[i].val.u.file_id);
        }
        flat_value_retain(coll, pool, val);
        if (val->tag == VAL_FILE && files) {
            flat_file_retain(files, val->u.file_id);
        }
        if (live) {
            flat_value_live_replace(live, &ctx->vars[i].val, val);
        }
        flat_value_release(coll, pool, &ctx->vars[i].val);
        ctx->vars[i].val = *val;
        return 0;
    }
    if ((uint32_t)ctx->var_count >= ctx->capacity) {
        return -1;
    }
    if (var_name_forbidden(name)) {
        return -1;
    }
    flat_value_retain(coll, pool, val);
    if (val->tag == VAL_FILE && files) {
        flat_file_retain(files, val->u.file_id);
    }
    if (live) {
        flat_value_live_bind(live, val);
    }
    snprintf(ctx->vars[ctx->var_count].name, sizeof ctx->vars[ctx->var_count].name, "%s", name);
    ctx->vars[ctx->var_count].val = *val;
    ctx->var_count++;
    return 0;
}

static int glob_match(const char *pat, const char *name) {
    if (!pat || !name) {
        return 0;
    }
    if (*pat == '\0') {
        return *name == '\0';
    }
    if (*pat == '*') {
        if (glob_match(pat + 1, name)) {
            return 1;
        }
        if (*name != '\0') {
            return glob_match(pat, name + 1);
        }
        return 0;
    }
    if (*pat == '?') {
        if (*name == '\0') {
            return 0;
        }
        return glob_match(pat + 1, name + 1);
    }
    if (*pat != *name) {
        return 0;
    }
    return glob_match(pat + 1, name + 1);
}

int flat_vcx_deep_copy_value(const FlatValue *src, FlatValue *dst, FlatCollectionPool *coll,
                             FlatStringPool *pool, FlatFileTable *files) {
    int len;
    int i;
    uint32_t new_list;
    uint32_t new_obj;
    FlatValue item;
    FlatValue elem;

    if (!src || !dst || !coll || !pool) {
        return -1;
    }
    switch (src->tag) {
    case VAL_EMPTY:
        dst->tag = VAL_EMPTY;
        return 0;
    case VAL_BOOL:
        return flat_value_set_bool(dst, src->u.boolean);
    case VAL_NUMBER:
        return flat_value_set_number(dst, src->u.number);
    case VAL_NULL:
        return flat_value_set_null(dst);
    case VAL_STR_INLINE:
        return flat_value_set_str_inline(dst, src->u.ins.bytes, src->u.ins.len);
    case VAL_STR_REF: {
        size_t slen = 0;
        const uint8_t *data = flat_pool_read(pool, src->u.ref_id, &slen);
        if (!data) {
            return -1;
        }
        uint32_t rid;
        if (flat_pool_alloc_copy(pool, data, slen, &rid) != 0) {
            return -1;
        }
        return flat_value_set_str_ref(dst, rid);
    }
    case VAL_FILE:
        if (!files) {
            return -1;
        }
        flat_file_retain(files, src->u.file_id);
        dst->tag = VAL_FILE;
        dst->u.file_id = src->u.file_id;
        return 0;
    case VAL_LIST: {
        FlatValue *items = NULL;

        if (flat_list_len(coll, src->u.coll_id, &len) != 0 || len < 0) {
            return -1;
        }
        if (len > 0) {
            items = malloc((size_t)len * sizeof *items);
            if (!items) {
                return -1;
            }
            for (i = 0; i < len; i++) {
                if (flat_list_get(coll, src->u.coll_id, i, &item) != 0) {
                    while (--i >= 0) {
                        flat_value_release(coll, pool, &items[i]);
                    }
                    free(items);
                    return -1;
                }
                if (flat_vcx_deep_copy_value(&item, &items[i], coll, pool, files) != 0) {
                    while (i >= 0) {
                        flat_value_release(coll, pool, &items[i]);
                        i--;
                    }
                    free(items);
                    return -1;
                }
            }
        }
        if (flat_list_build(coll, pool, items, len, &new_list) != 0) {
            for (i = 0; i < len; i++) {
                flat_value_release(coll, pool, &items[i]);
            }
            free(items);
            return -1;
        }
        free(items);
        return flat_value_set_list(dst, new_list);
    }
    case VAL_OBJECT: {
        int olen = 0;
        const char *key;

        if (flat_obj_empty(coll, pool, &new_obj) != 0) {
            return -1;
        }
        if (flat_obj_len(coll, src->u.coll_id, &olen) != 0) {
            return -1;
        }
        for (i = 0; i < olen; i++) {
            if (flat_obj_entry_at(coll, src->u.coll_id, i, &key, &item) != 0) {
                return -1;
            }
            if (flat_vcx_deep_copy_value(&item, &elem, coll, pool, files) != 0) {
                return -1;
            }
            if (flat_obj_set_copy(coll, pool, new_obj, key, &elem, &new_obj) != 0) {
                flat_value_release(coll, pool, &elem);
                return -1;
            }
            flat_value_release(coll, pool, &elem);
        }
        return flat_value_set_object(dst, new_obj);
    }
    default:
        return -1;
    }
}

FlatVcxRuntime *flat_vcx_runtime_create(uint32_t global_capacity) {
    FlatVcxRuntime *rt;
    FlatVcxContext *g;

    if (global_capacity == 0) {
        global_capacity = 1;
    }
    rt = calloc(1, sizeof *rt);
    if (!rt) {
        return NULL;
    }
    rt->contexts = calloc(1, sizeof *rt->contexts);
    if (!rt->contexts) {
        free(rt);
        return NULL;
    }
    rt->context_count = 1;
    rt->active_ix = 0;
    rt->next_id = 2;
    g = &rt->contexts[0];
    g->id = 1;
    g->capacity = global_capacity;
    g->vars = calloc(global_capacity, sizeof *g->vars);
    g->var_count = 0;
    if (!g->vars) {
        free(rt->contexts);
        free(rt);
        return NULL;
    }
    return rt;
}

void flat_vcx_runtime_attach_vm(FlatVcxRuntime *rt, FlatVcxVmBind bind) {
    if (!rt) {
        return;
    }
    rt->bind = bind;
    rt->bind_attached = 1;
    refresh_bind_from_active(rt);
}

void flat_vcx_runtime_sync_bind_var_count(FlatVcxRuntime *rt) {
    sync_bind_var_count_to_active(rt);
}

static void release_context_vars(FlatVcxContext *ctx, FlatCollectionPool *coll,
                                 FlatStringPool *pool, FlatFileTable *files) {
    int i;
    if (!ctx || !ctx->vars) {
        return;
    }
    for (i = 0; i < ctx->var_count; i++) {
        if (ctx->vars[i].val.tag == VAL_FILE && files) {
            flat_file_release(files, ctx->vars[i].val.u.file_id);
        }
        flat_value_release(coll, pool, &ctx->vars[i].val);
    }
}

void flat_vcx_runtime_destroy(FlatVcxRuntime *rt, FlatCollectionPool *coll, FlatStringPool *pool,
                              FlatFileTable *files) {
    int i;
    if (!rt) {
        return;
    }
    sync_bind_var_count_to_active(rt);
    for (i = 0; i < rt->context_count; i++) {
        release_context_vars(&rt->contexts[i], coll, pool, files);
        free(rt->contexts[i].vars);
        rt->contexts[i].vars = NULL;
    }
    free(rt->contexts);
    free(rt);
}

uint32_t flat_vcx_current_id(const FlatVcxRuntime *rt) {
    if (!rt || rt->context_count <= 0) {
        return 0;
    }
    return rt->contexts[rt->active_ix].id;
}

int flat_vcx_set(FlatVcxRuntime *rt, uint32_t ctx_id, char *panic_detail, size_t panic_sz) {
    int ix;
    if (!rt) {
        return -1;
    }
    if (rt->contexts[rt->active_ix].id == ctx_id) {
        return 0;
    }
    ix = ctx_index_by_id(rt, ctx_id);
    if (ix < 0) {
        snprintf(panic_detail, panic_sz, "vcx.set.invalid");
        return -1;
    }
    sync_bind_var_count_to_active(rt);
    rt->active_ix = ix;
    refresh_bind_from_active(rt);
    return 0;
}

int flat_vcx_create(FlatVcxRuntime *rt, uint32_t capacity, uint32_t *out_id, char *panic_detail,
                    size_t panic_sz) {
    FlatVcxContext *c;
    FlatVcxContext *grown;

    if (!rt || !out_id) {
        return -1;
    }
    if (capacity < 1 || capacity > (uint32_t)FLAT_MAX_VARS) {
        snprintf(panic_detail, panic_sz, "vcx.create.arg");
        return -1;
    }
    if (rt->context_count >= FLAT_VCX_MAX_CONTEXTS) {
        snprintf(panic_detail, panic_sz, "vcx.create.limit");
        return -1;
    }
    if (!rt->contexts) {
        snprintf(panic_detail, panic_sz, "vcx.create");
        return -1;
    }
    grown = realloc(rt->contexts, (size_t)(rt->context_count + 1) * sizeof *rt->contexts);
    if (!grown) {
        snprintf(panic_detail, panic_sz, "vcx.create");
        return -1;
    }
    rt->contexts = grown;
    c = &rt->contexts[rt->context_count];
    memset(c, 0, sizeof *c);
    c->id = rt->next_id++;
    c->capacity = capacity;
    c->vars = calloc(capacity, sizeof *c->vars);
    c->var_count = 0;
    if (!c->vars) {
        snprintf(panic_detail, panic_sz, "vcx.create");
        return -1;
    }
    rt->context_count++;
    *out_id = c->id;
    return 0;
}

static int panic_ctx(char *panic_detail, size_t panic_sz, const char *fn) {
    snprintf(panic_detail, panic_sz, "%s.ctx", fn);
    return -1;
}

int flat_vcx_get_var(const FlatVcxRuntime *rt, uint32_t ctx_id, const char *name, FlatValue *out,
                     FlatCollectionPool *coll, FlatStringPool *pool, FlatFileTable *files,
                     char *panic_detail, size_t panic_sz) {
    int ix;
    int vi;
    const FlatVcxContext *ctx;
    FlatValue tmp;

    if (!rt || !name || !out) {
        return -1;
    }
    ix = ctx_index_by_id(rt, ctx_id);
    if (ix < 0) {
        return panic_ctx(panic_detail, panic_sz, "vcx.var");
    }
    ctx = &rt->contexts[ix];
    vi = ctx_find_var(ctx, name);
    if (vi < 0) {
        snprintf(panic_detail, panic_sz, "vcx.var.missing");
        return -1;
    }
    if (flat_vcx_deep_copy_value(&ctx->vars[vi].val, &tmp, coll, pool, files) != 0) {
        snprintf(panic_detail, panic_sz, "vcx.var.copy");
        return -1;
    }
    *out = tmp;
    return 0;
}

int flat_vcx_has_var(const FlatVcxRuntime *rt, uint32_t ctx_id, const char *name, int *out_ok,
                     char *panic_detail, size_t panic_sz) {
    int ix;
    const FlatVcxContext *ctx;

    if (!rt || !name || !out_ok) {
        return -1;
    }
    ix = ctx_index_by_id(rt, ctx_id);
    if (ix < 0) {
        return panic_ctx(panic_detail, panic_sz, "vcx.hasVar");
    }
    ctx = &rt->contexts[ix];
    *out_ok = ctx_find_var(ctx, name) >= 0 ? 1 : 0;
    return 0;
}

int flat_vcx_vars_used(const FlatVcxRuntime *rt, uint32_t ctx_id, int *out_n, char *panic_detail,
                       size_t panic_sz) {
    int ix;
    if (!rt || !out_n) {
        return -1;
    }
    ix = ctx_index_by_id(rt, ctx_id);
    if (ix < 0) {
        return panic_ctx(panic_detail, panic_sz, "vcx.varsUsed");
    }
    *out_n = rt->contexts[ix].var_count;
    return 0;
}

int flat_vcx_vars_max(const FlatVcxRuntime *rt, uint32_t ctx_id, int *out_n, char *panic_detail,
                      size_t panic_sz) {
    int ix;
    if (!rt || !out_n) {
        return -1;
    }
    ix = ctx_index_by_id(rt, ctx_id);
    if (ix < 0) {
        return panic_ctx(panic_detail, panic_sz, "vcx.varsMax");
    }
    *out_n = (int)rt->contexts[ix].capacity;
    return 0;
}

int flat_vcx_vars_avail(const FlatVcxRuntime *rt, uint32_t ctx_id, int *out_n,
                          char *panic_detail, size_t panic_sz) {
    int used;
    int max;
    if (flat_vcx_vars_max(rt, ctx_id, &max, panic_detail, panic_sz) != 0) {
        return -1;
    }
    if (flat_vcx_vars_used(rt, ctx_id, &used, panic_detail, panic_sz) != 0) {
        return -1;
    }
    *out_n = max - used;
    if (*out_n < 0) {
        *out_n = 0;
    }
    return 0;
}

typedef struct {
    char name[FLAT_VAR_NAME_MAX + 1];
    FlatValue val;
    int valid;
} VcxCopyStage;

static int ctx_backup(FlatVcxContext *ctx, FlatCollectionPool *coll, FlatStringPool *pool,
                      FlatVcxVar **out_vars, int *out_count) {
    FlatVcxVar *bak;
    int i;
    if (!ctx || !out_vars || !out_count) {
        return -1;
    }
    *out_vars = NULL;
    *out_count = 0;
    if (ctx->var_count <= 0) {
        return 0;
    }
    bak = malloc((size_t)ctx->var_count * sizeof *bak);
    if (!bak) {
        return -1;
    }
    for (i = 0; i < ctx->var_count; i++) {
        bak[i] = ctx->vars[i];
        flat_value_retain(coll, pool, &ctx->vars[i].val);
    }
    *out_vars = bak;
    *out_count = ctx->var_count;
    return 0;
}

static void ctx_restore(FlatVcxContext *ctx, FlatCollectionPool *coll, FlatStringPool *pool,
                        FlatFileTable *files, FlatMemLiveStats *live, FlatVcxVar *bak, int bak_n) {
    int i;
    release_context_vars(ctx, coll, pool, files);
    ctx->var_count = 0;
    for (i = 0; i < bak_n; i++) {
        (void)ctx_set_var(ctx, bak[i].name, &bak[i].val, coll, pool, files, live);
        flat_value_release(coll, pool, &bak[i].val);
    }
    free(bak);
}

static int copy_selected(FlatVcxRuntime *rt, uint32_t src_id, uint32_t dst_id,
                         int (*select)(const char *name, void *ud), void *select_ud,
                         FlatCollectionPool *coll, FlatStringPool *pool, FlatFileTable *files,
                         FlatMemLiveStats *live, char *panic_detail, size_t panic_sz,
                         const char *panic_prefix) {
    int src_ix;
    int dst_ix;
    FlatVcxContext *src;
    FlatVcxContext *dst;
    VcxCopyStage *stage = NULL;
    int stage_n = 0;
    int i;
    int new_names = 0;
    FlatVcxVar *bak = NULL;
    int bak_n = 0;
    int rc = -1;

    if (!rt || src_id == dst_id) {
        return 0;
    }
    sync_bind_var_count_to_active(rt);
    src_ix = ctx_index_by_id(rt, src_id);
    dst_ix = ctx_index_by_id(rt, dst_id);
    if (src_ix < 0 || dst_ix < 0) {
        snprintf(panic_detail, panic_sz, "%s.ctx", panic_prefix);
        return -1;
    }
    src = &rt->contexts[src_ix];
    dst = &rt->contexts[dst_ix];

    for (i = 0; i < src->var_count; i++) {
        if (!select(src->vars[i].name, select_ud)) {
            continue;
        }
        if (ctx_find_var(dst, src->vars[i].name) < 0) {
            new_names++;
        }
    }
    if (dst->var_count + new_names > (int)dst->capacity) {
        snprintf(panic_detail, panic_sz, "%s.capacity", panic_prefix);
        return -1;
    }

    for (i = 0; i < src->var_count; i++) {
        if (!select(src->vars[i].name, select_ud)) {
            continue;
        }
        stage = realloc(stage, (size_t)(stage_n + 1) * sizeof *stage);
        if (!stage) {
            snprintf(panic_detail, panic_sz, "%s.copy", panic_prefix);
            goto fail;
        }
        memset(&stage[stage_n], 0, sizeof stage[stage_n]);
        snprintf(stage[stage_n].name, sizeof stage[stage_n].name, "%s", src->vars[i].name);
        if (flat_vcx_deep_copy_value(&src->vars[i].val, &stage[stage_n].val, coll, pool, files) !=
            0) {
            snprintf(panic_detail, panic_sz, "%s.copy", panic_prefix);
            goto fail;
        }
        stage[stage_n].valid = 1;
        stage_n++;
    }

    if (ctx_backup(dst, coll, pool, &bak, &bak_n) != 0) {
        snprintf(panic_detail, panic_sz, "%s.copy", panic_prefix);
        goto fail;
    }

    for (i = 0; i < stage_n; i++) {
        if (ctx_set_var(dst, stage[i].name, &stage[i].val, coll, pool, files, live) != 0) {
            ctx_restore(dst, coll, pool, files, live, bak, bak_n);
            snprintf(panic_detail, panic_sz, "%s.copy", panic_prefix);
            goto fail;
        }
    }
    free(bak);
    bak = NULL;
    if (stage) {
        free(stage);
        stage = NULL;
    }
    rc = 0;

fail:
    if (stage) {
        for (i = 0; i < stage_n; i++) {
            if (stage[i].valid) {
                flat_value_release(coll, pool, &stage[i].val);
            }
        }
        free(stage);
    }
    if (bak) {
        for (i = 0; i < bak_n; i++) {
            flat_value_release(coll, pool, &bak[i].val);
        }
        free(bak);
    }
    if (rc == 0 && rt->active_ix == dst_ix && rt->bind_attached) {
        refresh_bind_from_active(rt);
    }
    return rc;
}

static int select_all(const char *name, void *ud) {
    (void)name;
    (void)ud;
    return 1;
}

typedef struct {
    const char *pattern;
} PatternUd;

static int select_pattern(const char *name, void *ud) {
    PatternUd *p = ud;
    if (!p || !p->pattern) {
        return 0;
    }
    return glob_match(p->pattern, name);
}

int flat_vcx_vars_copy(FlatVcxRuntime *rt, uint32_t src_id, uint32_t dst_id,
                       FlatCollectionPool *coll, FlatStringPool *pool, FlatFileTable *files,
                       FlatMemLiveStats *live, char *panic_detail, size_t panic_sz) {
    return copy_selected(rt, src_id, dst_id, select_all, NULL, coll, pool, files, live,
                         panic_detail, panic_sz, "vcx.varsCopy");
}

int flat_vcx_vars_copy_like(FlatVcxRuntime *rt, uint32_t src_id, uint32_t dst_id,
                            const char *pattern, FlatCollectionPool *coll, FlatStringPool *pool,
                            FlatFileTable *files, FlatMemLiveStats *live, char *panic_detail,
                            size_t panic_sz) {
    PatternUd ud;
    if (!pattern || pattern[0] == '\0') {
        snprintf(panic_detail, panic_sz, "vcx.varsCopyLike.pattern");
        return -1;
    }
    ud.pattern = pattern;
    return copy_selected(rt, src_id, dst_id, select_pattern, &ud, coll, pool, files, live,
                         panic_detail, panic_sz, "vcx.varsCopyLike");
}
