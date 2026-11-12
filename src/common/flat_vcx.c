#include "flat_vcx.h"
#include "flat_schema.h"
#include "flat_file.h"
#include "flat_mem_stats.h"
#include "flat_value.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct FlatVcxContext {
    uint32_t id;
    uint32_t capacity;
    FlatVcxVar *vars;
    int var_count;
    int auto_grow;
    uint8_t dschema_allow_schemas;
    uint8_t dschema_list_active;
    int dschema_list_count;
    char dschema_list[FLAT_VCX_DSCHEMA_LIST_MAX][FLAT_SCHEMA_NAME_MAX + 1];
} FlatVcxContext;

static void ctx_init_policy(FlatVcxContext *c) {
    if (!c) {
        return;
    }
    c->dschema_allow_schemas = 1;
    c->dschema_list_active = 0;
    c->dschema_list_count = 0;
}

struct FlatVcxRuntime {
    FlatVcxContext *contexts;
    int context_count;
    int active_ix;
    uint32_t next_id;
    FlatVcxVmBind bind;
    int bind_attached;
};

static FlatVcxRuntime *g_vcx_active;
static FlatVcxCallInstrResolver g_call_instr_resolver;

void flat_vcx_runtime_set_active(FlatVcxRuntime *rt) {
    g_vcx_active = rt;
}

void flat_vcx_set_call_instr_resolver(FlatVcxCallInstrResolver fn) {
    g_call_instr_resolver = fn;
}

uint32_t flat_vcx_call_instr_ctx(void) {
    if (g_call_instr_resolver) {
        return g_call_instr_resolver();
    }
    return 0u;
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
    *rt->bind.var_slots_max = act->capacity;
}

static void adopt_active_var_count_to_bind(FlatVcxRuntime *rt) {
    if (!rt || !rt->bind_attached || !rt->bind.var_count) {
        return;
    }
    *rt->bind.var_count = rt->contexts[rt->active_ix].var_count;
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

static int ctx_grow_capacity(FlatVcxRuntime *rt, FlatVcxContext *ctx, uint32_t need_capacity,
                             char *panic_detail, size_t panic_sz) {
    uint32_t new_cap;
    FlatVcxVar *grown;

    if (!ctx || need_capacity == 0) {
        return -1;
    }
    if (need_capacity <= ctx->capacity) {
        return 0;
    }
    if (!ctx->auto_grow) {
        return -1;
    }
    new_cap = ctx->capacity > 0 ? ctx->capacity : FLAT_VCX_AUTO_DEFAULT_SEED;
    while (new_cap < need_capacity) {
        uint32_t next;

        if (new_cap >= (uint32_t)FLAT_MAX_VARS) {
            if (panic_detail && panic_sz > 0) {
                snprintf(panic_detail, panic_sz, "vcx.auto.limit");
            }
            return -1;
        }
        if (new_cap < FLAT_VCX_AUTO_GROW_DOUBLE_CAP) {
            next = new_cap * 2u;
            if (next < new_cap) {
                next = (uint32_t)FLAT_MAX_VARS;
            }
        } else if (new_cap + FLAT_VCX_AUTO_GROW_STEP <= (uint32_t)FLAT_MAX_VARS) {
            next = new_cap + FLAT_VCX_AUTO_GROW_STEP;
        } else {
            next = (uint32_t)FLAT_MAX_VARS;
        }
        new_cap = next;
    }
    grown = realloc(ctx->vars, (size_t)new_cap * sizeof *ctx->vars);
    if (!grown) {
        if (panic_detail && panic_sz > 0) {
            snprintf(panic_detail, panic_sz, "vcx.auto.alloc");
        }
        return -1;
    }
    if (new_cap > ctx->capacity) {
        memset(grown + ctx->capacity, 0, (size_t)(new_cap - ctx->capacity) * sizeof *grown);
    }
    ctx->vars = grown;
    ctx->capacity = new_cap;
    if (rt && rt->bind_attached && rt->active_ix >= 0 && rt->active_ix < rt->context_count &&
        &rt->contexts[rt->active_ix] == ctx) {
        refresh_bind_from_active(rt);
    }
    return 0;
}

static int ctx_reserve_slots(FlatVcxRuntime *rt, FlatVcxContext *ctx, uint32_t need_count,
                             const char *fixed_panic, char *panic_detail, size_t panic_sz) {
    if (!ctx) {
        return -1;
    }
    if (need_count <= ctx->capacity) {
        return 0;
    }
    if (!ctx->auto_grow) {
        if (panic_detail && panic_sz > 0) {
            if (fixed_panic) {
                snprintf(panic_detail, panic_sz, "%s", fixed_panic);
            } else {
                snprintf(panic_detail, panic_sz, "vcx.slots.limit");
            }
        }
        return -1;
    }
    return ctx_grow_capacity(rt, ctx, need_count, panic_detail, panic_sz);
}

static int ctx_set_var(FlatVcxRuntime *rt, FlatVcxContext *ctx, const char *name,
                       const FlatValue *val, FlatCollectionPool *coll, FlatStringPool *pool,
                       FlatFileTable *files, FlatMemLiveStats *live, char *panic_detail,
                       size_t panic_sz) {
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
        if (ctx_reserve_slots(rt, ctx, (uint32_t)ctx->var_count + 1u, NULL, panic_detail,
                              panic_sz) != 0) {
            return -1;
        }
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
        uint32_t rid;

        if (!flat_pool_slot_in_use(pool, src->u.ref_id)) {
            return -1;
        }
        if (slen == 0) {
            if (flat_pool_alloc_empty(pool, &rid) != 0) {
                return -1;
            }
        } else {
            if (!data) {
                return -1;
            }
            if (flat_pool_alloc_copy(pool, data, slen, &rid) != 0) {
                return -1;
            }
        }
        return flat_value_set_str_ref(dst, rid);
    }
    case VAL_FILE:
        if (!files) {
            return -1;
        }
        /* File refcount: binder (e.g. ctx_set_var) retains once; do not retain here. */
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
        /* list_build takes ownership via elem_bind; do not release items[] again */
        free(items);
        return flat_value_set_list(dst, new_list);
    }
    case VAL_OBJECT: {
        int olen = 0;
        const char *key;
        char(*keys)[FLAT_OBJ_KEY_MAX + 1];
        FlatValue *ovals;

        if (flat_obj_len(coll, src->u.coll_id, &olen) != 0) {
            return -1;
        }
        if (olen == 0) {
            if (flat_obj_empty(coll, pool, &new_obj) != 0) {
                return -1;
            }
            return flat_value_set_object(dst, new_obj);
        }
        if (olen > FLAT_OBJ_MAX_KEYS) {
            return -1;
        }
        keys = malloc((size_t)olen * sizeof *keys);
        ovals = malloc((size_t)olen * sizeof *ovals);
        if (!keys || !ovals) {
            free(keys);
            free(ovals);
            return -1;
        }
        for (i = 0; i < olen; i++) {
            if (flat_obj_entry_at(coll, src->u.coll_id, i, &key, &item) != 0 || !key) {
                while (--i >= 0) {
                    flat_value_release(coll, pool, &ovals[i]);
                }
                free(keys);
                free(ovals);
                return -1;
            }
            snprintf(keys[i], sizeof keys[i], "%s", key);
            if (flat_vcx_deep_copy_value(&item, &ovals[i], coll, pool, files) != 0) {
                while (i >= 0) {
                    flat_value_release(coll, pool, &ovals[i]);
                    i--;
                }
                free(keys);
                free(ovals);
                return -1;
            }
        }
        if (flat_obj_build(coll, pool, keys, ovals, olen, &new_obj) != 0) {
            for (i = 0; i < olen; i++) {
                flat_value_release(coll, pool, &ovals[i]);
            }
            free(keys);
            free(ovals);
            return -1;
        }
        /* obj_build binds entries; stack copies must not be released again */
        free(keys);
        free(ovals);
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
    ctx_init_policy(g);
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
    adopt_active_var_count_to_bind(rt);
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
    ctx_init_policy(c);
    rt->context_count++;
    *out_id = c->id;
    return 0;
}

int flat_vcx_create_auto(FlatVcxRuntime *rt, uint32_t initial_capacity, uint32_t *out_id,
                         char *panic_detail, size_t panic_sz) {
    FlatVcxContext *c;
    FlatVcxContext *grown;

    if (!rt || !out_id) {
        return -1;
    }
    if (initial_capacity < 1u) {
        initial_capacity = FLAT_VCX_AUTO_DEFAULT_SEED;
    }
    if (initial_capacity > (uint32_t)FLAT_MAX_VARS) {
        snprintf(panic_detail, panic_sz, "vcx.createAuto.arg");
        return -1;
    }
    if (rt->context_count >= FLAT_VCX_MAX_CONTEXTS) {
        snprintf(panic_detail, panic_sz, "vcx.createAuto.limit");
        return -1;
    }
    if (!rt->contexts) {
        snprintf(panic_detail, panic_sz, "vcx.createAuto");
        return -1;
    }
    grown = realloc(rt->contexts, (size_t)(rt->context_count + 1) * sizeof *rt->contexts);
    if (!grown) {
        snprintf(panic_detail, panic_sz, "vcx.createAuto");
        return -1;
    }
    rt->contexts = grown;
    c = &rt->contexts[rt->context_count];
    memset(c, 0, sizeof *c);
    c->id = rt->next_id++;
    c->capacity = initial_capacity;
    c->auto_grow = 1;
    c->vars = calloc(initial_capacity, sizeof *c->vars);
    c->var_count = 0;
    if (!c->vars) {
        snprintf(panic_detail, panic_sz, "vcx.createAuto");
        return -1;
    }
    ctx_init_policy(c);
    rt->context_count++;
    *out_id = c->id;
    return 0;
}

static int parse_schema_list_entry(const FlatValue *v, const FlatStringPool *pool,
                                   char *name_out, size_t name_sz) {
    char buf[FLAT_SCHEMA_NAME_MAX + 4];
    const char *p;
    const char *end;

    if (flat_value_get_cstr(v, pool, buf, sizeof buf) != 0 || buf[0] == '\0') {
        return -1;
    }
    p = buf;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '<') {
        return -1;
    }
    p++;
    end = strchr(p, '>');
    if (!end || end == p) {
        return -1;
    }
    if ((size_t)(end - p) >= name_sz) {
        return -1;
    }
    memcpy(name_out, p, (size_t)(end - p));
    name_out[end - p] = '\0';
    if (end[1] != '\0') {
        const char *t = end + 1;
        while (*t && isspace((unsigned char)*t)) {
            t++;
        }
        if (*t != '\0') {
            return -1;
        }
    }
    return 0;
}

int flat_vcx_dynamic_schema_permit(FlatVcxRuntime *rt, const char *schema_name, char *panic,
                                   size_t panic_sz) {
    FlatVcxContext *act;
    int i;

    if (!schema_name || !schema_name[0]) {
        snprintf(panic, panic_sz, "type.assign.schema.unknown");
        return -1;
    }
    if (!rt || rt->context_count <= 0) {
        return 0;
    }
    act = &rt->contexts[rt->active_ix];
    if (!act->dschema_allow_schemas) {
        snprintf(panic, panic_sz, "type.assign.dynamic.denied");
        return -1;
    }
    if (!act->dschema_list_active) {
        return 0;
    }
    if (act->dschema_list_count <= 0) {
        snprintf(panic, panic_sz, "type.assign.dynamic.denied");
        return -1;
    }
    for (i = 0; i < act->dschema_list_count; i++) {
        if (strcmp(act->dschema_list[i], schema_name) == 0) {
            return 0;
        }
    }
    snprintf(panic, panic_sz, "type.assign.dynamic.denied");
    return -1;
}

int flat_vcx_dschema_allow(FlatVcxRuntime *rt, uint32_t ctx_id, int allow, char *panic_detail,
                           size_t panic_sz) {
    int ix;
    FlatVcxContext *c;

    if (!rt) {
        return -1;
    }
    ix = ctx_index_by_id(rt, ctx_id);
    if (ix < 0) {
        snprintf(panic_detail, panic_sz, "vcx.dSchemaAllow.invalid");
        return -1;
    }
    c = &rt->contexts[ix];
    c->dschema_allow_schemas = allow ? 1u : 0u;
    return 0;
}

int flat_vcx_dschema_list(FlatVcxRuntime *rt, uint32_t ctx_id, const FlatValue *list_val,
                          FlatCollectionPool *coll, FlatStringPool *pool, char *panic_detail,
                          size_t panic_sz) {
    int ix;
    FlatVcxContext *c;
    int len;
    int i;

    if (!rt || !list_val || !coll || !pool) {
        return -1;
    }
    if (list_val->tag != VAL_LIST) {
        snprintf(panic_detail, panic_sz, "vcx.dSchemaList.arg");
        return -1;
    }
    ix = ctx_index_by_id(rt, ctx_id);
    if (ix < 0) {
        snprintf(panic_detail, panic_sz, "vcx.dSchemaList.invalid");
        return -1;
    }
    c = &rt->contexts[ix];
    if (flat_list_len(coll, list_val->u.coll_id, &len) != 0) {
        snprintf(panic_detail, panic_sz, "vcx.dSchemaList.arg");
        return -1;
    }
    {
        char parsed[FLAT_VCX_DSCHEMA_LIST_MAX][FLAT_SCHEMA_NAME_MAX + 1];

        if (len > FLAT_VCX_DSCHEMA_LIST_MAX) {
            snprintf(panic_detail, panic_sz, "vcx.dSchemaList.limit");
            return -1;
        }
        /* Build the replacement policy off to the side. A failed update must not
         * weaken or partially overwrite the currently active whitelist. */
        for (i = 0; i < len; i++) {
            FlatValue el;
            if (flat_list_get(coll, list_val->u.coll_id, i, &el) != 0) {
                snprintf(panic_detail, panic_sz, "vcx.dSchemaList.arg");
                return -1;
            }
            if (parse_schema_list_entry(&el, pool, parsed[i], sizeof parsed[i]) != 0) {
                snprintf(panic_detail, panic_sz, "vcx.dSchemaList.entry");
                return -1;
            }
        }
        if (len > 0) {
            memcpy(c->dschema_list, parsed, (size_t)len * sizeof parsed[0]);
        }
        c->dschema_list_count = len;
        c->dschema_list_active = 1;
    }
    return 0;
}

int flat_vcx_prepare_active_new_bind(FlatVcxRuntime *rt, char *panic_detail, size_t panic_sz) {
    FlatVcxContext *act;
  int need;

    if (!rt || !rt->bind_attached || rt->context_count <= 0) {
        if (panic_detail && panic_sz > 0) {
            snprintf(panic_detail, panic_sz, "vcx.bind.prepare");
        }
        return -1;
    }
    act = &rt->contexts[rt->active_ix];
    need = act->var_count + 1;
    if ((uint32_t)need <= act->capacity) {
        return 0;
    }
    return ctx_reserve_slots(rt, act, (uint32_t)need, NULL, panic_detail, panic_sz);
}

static int panic_ctx(char *panic_detail, size_t panic_sz, const char *fn) {
    snprintf(panic_detail, panic_sz, "%s.ctx", fn);
    return -1;
}

int flat_vcx_is_active(const FlatVcxRuntime *rt, uint32_t ctx_id, int *out_active,
                       char *panic_detail, size_t panic_sz) {
    int ix;

    if (!rt || !out_active) {
        return -1;
    }
    ix = ctx_index_by_id(rt, ctx_id);
    if (ix < 0) {
        return panic_ctx(panic_detail, panic_sz, "vcx.isactive");
    }
    *out_active = (rt->active_ix == ix) ? 1 : 0;
    return 0;
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
    FlatSlotType slot_type;
    uint8_t has_slot_type;
    uint8_t slot_dynamic;
    char slot_dynamic_expr[FLAT_VAR_NAME_MAX + 1];
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

static void ctx_restore(FlatVcxRuntime *rt, FlatVcxContext *ctx, FlatCollectionPool *coll,
                        FlatStringPool *pool, FlatFileTable *files, FlatMemLiveStats *live,
                        FlatVcxVar *bak, int bak_n) {
    int i;
    release_context_vars(ctx, coll, pool, files);
    ctx->var_count = 0;
    for (i = 0; i < bak_n; i++) {
        int vi;
        (void)ctx_set_var(rt, ctx, bak[i].name, &bak[i].val, coll, pool, files, live, NULL, 0);
        vi = ctx_find_var(ctx, bak[i].name);
        if (vi >= 0) {
            ctx->vars[vi].slot_type = bak[i].slot_type;
            ctx->vars[vi].has_slot_type = bak[i].has_slot_type;
            ctx->vars[vi].slot_dynamic = bak[i].slot_dynamic;
            snprintf(ctx->vars[vi].slot_dynamic_expr, sizeof ctx->vars[vi].slot_dynamic_expr, "%s",
                     bak[i].slot_dynamic_expr);
        }
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
    if (ctx_reserve_slots(rt, dst, (uint32_t)dst->var_count + (uint32_t)new_names,
                          "vcx.varsCopy.capacity", panic_detail, panic_sz) != 0) {
        if (panic_detail[0] == '\0') {
            snprintf(panic_detail, panic_sz, "%s.capacity", panic_prefix);
        }
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
        stage[stage_n].slot_type = src->vars[i].slot_type;
        stage[stage_n].has_slot_type = src->vars[i].has_slot_type;
        stage[stage_n].slot_dynamic = src->vars[i].slot_dynamic;
        snprintf(stage[stage_n].slot_dynamic_expr, sizeof stage[stage_n].slot_dynamic_expr, "%s",
                 src->vars[i].slot_dynamic_expr);
        stage[stage_n].valid = 1;
        stage_n++;
    }

    if (ctx_backup(dst, coll, pool, &bak, &bak_n) != 0) {
        snprintf(panic_detail, panic_sz, "%s.copy", panic_prefix);
        goto fail;
    }

    for (i = 0; i < stage_n; i++) {
        int di;
        if (ctx_set_var(rt, dst, stage[i].name, &stage[i].val, coll, pool, files, live,
                        panic_detail, panic_sz) != 0) {
            ctx_restore(rt, dst, coll, pool, files, live, bak, bak_n);
            bak = NULL;
            snprintf(panic_detail, panic_sz, "%s.copy", panic_prefix);
            goto fail;
        }
        di = ctx_find_var(dst, stage[i].name);
        if (di >= 0) {
            dst->vars[di].slot_type = stage[i].slot_type;
            dst->vars[di].has_slot_type = stage[i].has_slot_type;
            dst->vars[di].slot_dynamic = stage[i].slot_dynamic;
            snprintf(dst->vars[di].slot_dynamic_expr, sizeof dst->vars[di].slot_dynamic_expr, "%s",
                     stage[i].slot_dynamic_expr);
        }
    }
    if (bak) {
        for (i = 0; i < bak_n; i++) {
            flat_value_release(coll, pool, &bak[i].val);
        }
        free(bak);
        bak = NULL;
    }
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
        adopt_active_var_count_to_bind(rt);
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

int flat_vcx_destroy_ctx(FlatVcxRuntime *rt, uint32_t ctx_id, FlatCollectionPool *coll,
                         FlatStringPool *pool, FlatFileTable *files, char *panic_detail,
                         size_t panic_sz) {
    int ix;
    int last;

    if (!rt) {
        return -1;
    }
    if (ctx_id == 1u) {
        snprintf(panic_detail, panic_sz, "vcx.destroy.global");
        return -1;
    }
    ix = ctx_index_by_id(rt, ctx_id);
    if (ix < 0) {
        snprintf(panic_detail, panic_sz, "vcx.destroy.notfound");
        return -1;
    }
    if (rt->active_ix == ix) {
        snprintf(panic_detail, panic_sz, "vcx.destroy.isactive");
        return -1;
    }
    sync_bind_var_count_to_active(rt);
    release_context_vars(&rt->contexts[ix], coll, pool, files);
    free(rt->contexts[ix].vars);
    rt->contexts[ix].vars = NULL;
    last = rt->context_count - 1;
    if (ix != last) {
        rt->contexts[ix] = rt->contexts[last];
        if (rt->active_ix == last) {
            rt->active_ix = ix;
        }
    }
    rt->context_count--;
    return 0;
}

int flat_vcx_vars_copy_by_names(FlatVcxRuntime *rt, uint32_t src_id, uint32_t dst_id,
                                const char *const *src_names, const char *const *dst_names, int n,
                                FlatCollectionPool *coll, FlatStringPool *pool,
                                FlatFileTable *files, FlatMemLiveStats *live, char *panic_detail,
                                size_t panic_sz) {
    int src_ix;
    int dst_ix;
    FlatVcxContext *src;
    FlatVcxContext *dst;
    VcxCopyStage *stage = NULL;
    int stage_n = 0;
    int i;
    int new_names = 0;
    int src_vi;
    FlatVcxVar *bak = NULL;
    int bak_n = 0;
    int rc = -1;
    const char *prefix = "vcx.varsCopyByNames";

    if (!rt || !src_names || !dst_names) {
        return -1;
    }
    if (n < 0) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    sync_bind_var_count_to_active(rt);
    src_ix = ctx_index_by_id(rt, src_id);
    dst_ix = ctx_index_by_id(rt, dst_id);
    if (src_ix < 0 || dst_ix < 0) {
        snprintf(panic_detail, panic_sz, "%s.ctx", prefix);
        return -1;
    }
    src = &rt->contexts[src_ix];
    dst = &rt->contexts[dst_ix];

    for (i = 0; i < n; i++) {
        const char *sn = src_names[i];
        const char *dn = dst_names[i];
        if (!sn || !dn || sn[0] == '\0' || dn[0] == '\0') {
            snprintf(panic_detail, panic_sz, "%s.name", prefix);
            return -1;
        }
        if (var_name_forbidden(sn) || var_name_forbidden(dn)) {
            snprintf(panic_detail, panic_sz, "%s.name", prefix);
            return -1;
        }
        src_vi = ctx_find_var(src, sn);
        if (src_vi < 0) {
            snprintf(panic_detail, panic_sz, "%s.missing", prefix);
            return -1;
        }
        if (ctx_find_var(dst, dn) < 0) {
            new_names++;
        }
    }
    if (ctx_reserve_slots(rt, dst, (uint32_t)dst->var_count + (uint32_t)new_names,
                          "vcx.varsCopyByNames.capacity", panic_detail, panic_sz) != 0) {
        return -1;
    }

    stage = malloc((size_t)n * sizeof *stage);
    if (!stage) {
        snprintf(panic_detail, panic_sz, "%s.copy", prefix);
        return -1;
    }
    memset(stage, 0, (size_t)n * sizeof *stage);

    for (i = 0; i < n; i++) {
        src_vi = ctx_find_var(src, src_names[i]);
        snprintf(stage[i].name, sizeof stage[i].name, "%s", dst_names[i]);
        if (flat_vcx_deep_copy_value(&src->vars[src_vi].val, &stage[i].val, coll, pool, files) !=
            0) {
            snprintf(panic_detail, panic_sz, "%s.copy", prefix);
            goto fail;
        }
        stage[i].slot_type = src->vars[src_vi].slot_type;
        stage[i].has_slot_type = src->vars[src_vi].has_slot_type;
        stage[i].slot_dynamic = src->vars[src_vi].slot_dynamic;
        snprintf(stage[i].slot_dynamic_expr, sizeof stage[i].slot_dynamic_expr, "%s",
                 src->vars[src_vi].slot_dynamic_expr);
        stage[i].valid = 1;
        stage_n++;
    }

    if (ctx_backup(dst, coll, pool, &bak, &bak_n) != 0) {
        snprintf(panic_detail, panic_sz, "%s.copy", prefix);
        goto fail;
    }

    for (i = 0; i < stage_n; i++) {
        int di;
        if (ctx_set_var(rt, dst, stage[i].name, &stage[i].val, coll, pool, files, live,
                        panic_detail, panic_sz) != 0) {
            ctx_restore(rt, dst, coll, pool, files, live, bak, bak_n);
            bak = NULL;
            snprintf(panic_detail, panic_sz, "%s.copy", prefix);
            goto fail;
        }
        di = ctx_find_var(dst, stage[i].name);
        if (di >= 0) {
            dst->vars[di].slot_type = stage[i].slot_type;
            dst->vars[di].has_slot_type = stage[i].has_slot_type;
            dst->vars[di].slot_dynamic = stage[i].slot_dynamic;
            snprintf(dst->vars[di].slot_dynamic_expr, sizeof dst->vars[di].slot_dynamic_expr, "%s",
                     stage[i].slot_dynamic_expr);
        }
    }
    if (bak) {
        for (i = 0; i < bak_n; i++) {
            flat_value_release(coll, pool, &bak[i].val);
        }
        free(bak);
        bak = NULL;
    }
    free(stage);
    stage = NULL;
    if (rt->active_ix == dst_ix && rt->bind_attached) {
        refresh_bind_from_active(rt);
        adopt_active_var_count_to_bind(rt);
    }
    return 0;

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
    return rc;
}

int flat_vcx_set_named_var(FlatVcxRuntime *rt, uint32_t ctx_id, const char *name,
                            const FlatValue *val, FlatCollectionPool *coll, FlatStringPool *pool,
                            FlatFileTable *files, FlatMemLiveStats *live, char *panic_detail,
                            size_t panic_sz) {
    int ix;
    FlatVcxContext *ctx;

    (void)panic_detail;
    (void)panic_sz;
    if (!rt || !name || !val) {
        return -1;
    }
    ix = ctx_index_by_id(rt, ctx_id);
    if (ix < 0) {
        return -1;
    }
    ctx = &rt->contexts[ix];
    if (ctx_set_var(rt, ctx, name, val, coll, pool, files, live, panic_detail, panic_sz) != 0) {
        return -1;
    }
    if (rt->contexts[rt->active_ix].id == ctx_id) {
        refresh_bind_from_active(rt);
        adopt_active_var_count_to_bind(rt);
    }
    return 0;
}
