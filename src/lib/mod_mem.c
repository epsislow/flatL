#include "flat_memstream.h"
#include "flat_native_mod.h"
#include "flat_mem_stats.h"
#include "flat_native.h"
#include "flat_obj_path.h"
#include "flat_slot_type.h"
#include "flat_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int mem_out_num(FlatNativeCtx *ctx, double n) {
    if (!ctx || !ctx->out || !ctx->io_status) {
        return -1;
    }
    if (ctx->inner && ctx->inner[0] != '\0') {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_number(ctx->out, n);
}

static int avail_nonneg(int max, int used) {
    int a = max - used;
    return a > 0 ? a : 0;
}

int flat_native_mem_vars_max(FlatNativeCtx *ctx) {
    return mem_out_num(ctx, (double)ctx->vars_max);
}

int flat_native_mem_vars_used(FlatNativeCtx *ctx) {
    return mem_out_num(ctx, (double)ctx->var_count);
}

int flat_native_mem_vars_avail(FlatNativeCtx *ctx) {
    return mem_out_num(ctx, (double)avail_nonneg((int)ctx->vars_max, ctx->var_count));
}

int flat_native_mem_list_max(FlatNativeCtx *ctx) {
    return mem_out_num(ctx, (double)FLAT_COLL_MAX_LISTS);
}

int flat_native_mem_list_used(FlatNativeCtx *ctx) {
    if (!ctx->coll) {
        return -1;
    }
    return mem_out_num(ctx, (double)flat_coll_live_list_count(ctx->coll));
}

int flat_native_mem_list_avail(FlatNativeCtx *ctx) {
    int used = ctx->coll ? flat_coll_live_list_count(ctx->coll) : 0;
    return mem_out_num(ctx, (double)avail_nonneg(FLAT_COLL_MAX_LISTS, used));
}

int flat_native_mem_object_max(FlatNativeCtx *ctx) {
    return mem_out_num(ctx, (double)FLAT_COLL_MAX_OBJECTS);
}

int flat_native_mem_object_used(FlatNativeCtx *ctx) {
    if (!ctx->coll) {
        return -1;
    }
    return mem_out_num(ctx, (double)flat_coll_live_object_count(ctx->coll));
}

int flat_native_mem_object_avail(FlatNativeCtx *ctx) {
    int used = ctx->coll ? flat_coll_live_object_count(ctx->coll) : 0;
    return mem_out_num(ctx, (double)avail_nonneg(FLAT_COLL_MAX_OBJECTS, used));
}

int flat_native_mem_strref_max(FlatNativeCtx *ctx) {
    return mem_out_num(ctx, (double)FLAT_POOL_MAX_SLOTS);
}

int flat_native_mem_strref_used(FlatNativeCtx *ctx) {
    if (!ctx->pool) {
        return -1;
    }
    return mem_out_num(ctx, (double)flat_pool_live_slot_count(ctx->pool));
}

int flat_native_mem_strref_avail(FlatNativeCtx *ctx) {
    int used = ctx->pool ? flat_pool_live_slot_count(ctx->pool) : 0;
    return mem_out_num(ctx, (double)avail_nonneg(FLAT_POOL_MAX_SLOTS, used));
}

int flat_native_mem_str_used(FlatNativeCtx *ctx) {
    if (!ctx->mem_live) {
        return -1;
    }
    return mem_out_num(ctx, (double)ctx->mem_live->str_used);
}

int flat_native_mem_bool_used(FlatNativeCtx *ctx) {
    if (!ctx->mem_live) {
        return -1;
    }
    return mem_out_num(ctx, (double)ctx->mem_live->bool_used);
}

int flat_native_mem_num_used(FlatNativeCtx *ctx) {
    if (!ctx->mem_live) {
        return -1;
    }
    return mem_out_num(ctx, (double)ctx->mem_live->num_used);
}

int flat_native_mem_null_used(FlatNativeCtx *ctx) {
    if (!ctx->mem_live) {
        return -1;
    }
    return mem_out_num(ctx, (double)ctx->mem_live->null_used);
}

static const char *mem_logical_type_name(FlatValueTag tag) {
    switch (tag) {
    case VAL_BOOL:
        return "bool";
    case VAL_NUMBER:
        return "number";
    case VAL_STR_INLINE:
    case VAL_STR_REF:
        return "string";
    case VAL_LIST:
        return "list";
    case VAL_OBJECT:
        return "object";
    case VAL_NULL:
        return "null";
    case VAL_FILE:
        return "file";
    case VAL_EMPTY:
        return "empty";
    default:
        return "unknown";
    }
}

int flat_native_mem_type_of(FlatNativeCtx *ctx) {
    FlatValue src;
    const char *kind;

    if (!ctx->out || !ctx->io_status || !ctx->inner || ctx->inner[0] == '\0') {
        if (ctx->io_status) {
            *ctx->io_status = FLAT_IO_ERR;
        }
        return -1;
    }
    if (flat_native_eval_call(ctx, ctx->inner, &src) != 0 &&
        flat_value_parse_assign_rhs(ctx->inner, &src, ctx->pool, ctx->coll, ctx->vars,
                                    ctx->var_count, ctx->var_names) != 0) {
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    kind = mem_logical_type_name(src.tag);
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_str_inline(ctx->out, kind, strlen(kind));
}

static int mem_slot_arg_err(FlatNativeCtx *ctx, const char *code) {
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_ERR;
    }
    if (code && code[0] != '\0') {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", code);
    }
    return -1;
}

static int mem_resolve_slot_lvalue(FlatNativeCtx *ctx, const char *native_name, int *root_vi,
                                   FlatSlotType *contract, int *has_contract) {
    FlatPathTypedCtx ptc;
    FlatSlotLocErr loc = FLAT_SLOT_LOC_OK;

    if (!ctx || !ctx->inner || ctx->inner[0] == '\0') {
        return mem_slot_arg_err(ctx, native_name);
    }
    if (!ctx->vars || !ctx->var_names) {
        return mem_slot_arg_err(ctx, native_name);
    }
    memset(&ptc, 0, sizeof ptc);
    if (ctx->bc) {
        ptc.schemas = &ctx->bc->schemas;
    }
    ptc.var_slot_types = ctx->var_slot_types;
    ptc.var_has_slot_type = ctx->var_has_slot_type;
    if (flat_path_resolve_slot_lvalue(ctx->inner, ctx->pool, ctx->coll, ctx->vars, ctx->var_count,
                                      ctx->var_names, &ptc, root_vi, contract, has_contract,
                                      &loc) != 0) {
        if (loc == FLAT_SLOT_LOC_MISSING) {
            snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s.missing", native_name);
        } else {
            snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s.path", native_name);
        }
        if (ctx->io_status) {
            *ctx->io_status = FLAT_IO_ERR;
        }
        return -1;
    }
    return 0;
}

int flat_native_mem_slot_type(FlatNativeCtx *ctx) {
    int root_vi;
    FlatSlotType contract;
    int has_contract = 0;
    char spec[128];

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    if (mem_resolve_slot_lvalue(ctx, "mem.slot_type", &root_vi, &contract, &has_contract) != 0) {
        return -1;
    }
    if (!has_contract) {
        snprintf(spec, sizeof spec, "none");
    } else if (flat_slot_type_format_spec(
                   &contract, ctx->bc ? &ctx->bc->schemas : NULL, spec, sizeof spec) != 0) {
        return mem_slot_arg_err(ctx, "mem.slot_type");
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_str_inline(ctx->out, spec, strlen(spec));
}

int flat_native_mem_slot_frozen(FlatNativeCtx *ctx) {
    int root_vi;
    FlatSlotType contract;
    int has_contract = 0;
    int frozen = 0;

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    (void)contract;
    (void)has_contract;
    if (mem_resolve_slot_lvalue(ctx, "mem.slot_frozen", &root_vi, &contract, &has_contract) != 0) {
        return -1;
    }
    if (ctx->var_slot_frozen && ctx->var_slot_frozen[root_vi]) {
        frozen = 1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_bool(ctx->out, frozen);
}

int flat_native_mem_slot_const(FlatNativeCtx *ctx) {
    int root_vi;
    FlatSlotType contract;
    int has_contract = 0;
    int is_const = 0;

    if (!ctx->out || !ctx->io_status) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    (void)contract;
    (void)has_contract;
    if (mem_resolve_slot_lvalue(ctx, "mem.slot_const", &root_vi, &contract, &has_contract) != 0) {
        return -1;
    }
    if (ctx->var_slot_const && ctx->var_slot_const[root_vi]) {
        is_const = 1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_bool(ctx->out, is_const);
}

int flat_native_mem_ref_empty(FlatNativeCtx *ctx) {
    uint32_t id;

    if (!ctx->out || !ctx->io_status || !ctx->pool) {
        return -1;
    }
    if (ctx->inner && ctx->inner[0] != '\0') {
        return -1;
    }
    if (flat_pool_alloc_empty(ctx->pool, &id) != 0) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_str_ref(ctx->out, id);
}

int flat_native_mem_ref_from(FlatNativeCtx *ctx) {
    FlatValue src;
    char *bytes = NULL;
    size_t blen = 0;
    FILE *mem;
    uint32_t id;

    if (!ctx->out || !ctx->io_status || !ctx->pool || !ctx->inner) {
        return -1;
    }
    if (flat_native_eval_call(ctx, ctx->inner, &src) != 0 &&
        flat_value_parse_assign_rhs(ctx->inner, &src, ctx->pool, ctx->coll, ctx->vars,
                                    ctx->var_count, ctx->var_names) != 0) {
        return -1;
    }
    mem = flat_open_memstream(&bytes, &blen);
    if (!mem) {
        return -1;
    }
    flat_value_fprint_io(mem, &src, ctx->pool, ctx->coll);
    if (flat_close_memstream(mem) != 0) {
        return -1;
    }
    if (blen == 0) {
        if (flat_pool_alloc_empty(ctx->pool, &id) != 0) {
            free(bytes);
            return -1;
        }
    } else if (flat_pool_alloc_copy(ctx->pool, bytes, blen, &id) != 0) {
        free(bytes);
        return -1;
    }
    free(bytes);
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_str_ref(ctx->out, id);
}
