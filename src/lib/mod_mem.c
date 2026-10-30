#include "flat_native_mod.h"
#include "flat_mem_stats.h"
#include "flat_value.h"

#include <stdio.h>
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
