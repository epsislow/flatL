#include "flat_csv_runtime.h"
#include "flat_file.h"
#include "flat_number.h"
#include "flat_memstream.h"
#include "flat_native_mod.h"
#include "flat_mem_stats.h"
#include "flat_native.h"
#include "flat_native_util.h"
#include "flat_obj_path.h"
#include "flat_slot_type.h"
#include "flat_value.h"
#include "flat_vcx.h"
#include "flat_clock_runtime.h"
#include "flat_native_mods.h"
#if FLAT_WITH_HTTP
#include "flat_http_runtime.h"
#endif

#include <math.h>
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

static int info_split_two_args(char *inner, char **left, char **right) {
    int depth = 0;
    char quote = 0;
    char *p;

    if (!inner || !left || !right) {
        return -1;
    }
    for (p = inner; *p; p++) {
        if (!quote && (*p == '\'' || *p == '"')) {
            quote = *p;
        } else if (quote && *p == quote) {
            quote = 0;
        } else if (!quote && (*p == '[' || *p == '(')) {
            depth++;
        } else if (!quote && (*p == ']' || *p == ')')) {
            if (depth > 0) {
                depth--;
            }
        } else if (!quote && depth == 0 && *p == ',') {
            *p = '\0';
            *left = flat_native_trim(inner);
            *right = flat_native_trim(p + 1);
            return (*left[0] != '\0' && *right[0] != '\0') ? 0 : -1;
        }
    }
    return -1;
}

static int info_parse_rhs(FlatNativeCtx *ctx, const char *expr, FlatValue *out) {
    char buf[512];

    if (!ctx || !expr || !out) {
        return -1;
    }
    snprintf(buf, sizeof buf, "%s", expr);
    flat_native_trim(buf);
    if (flat_native_eval_call(ctx, buf, out) == 0) {
        return 0;
    }
    return flat_value_parse_assign_rhs(buf, out, ctx->pool, ctx->coll, ctx->vars, ctx->var_count,
                                       ctx->var_names);
}

static int info_parse_hint(FlatNativeCtx *ctx, const char *expr, char *hint, size_t hint_sz) {
    FlatValue v;

    if (info_parse_rhs(ctx, expr, &v) != 0) {
        return -1;
    }
    if (v.tag == VAL_STR_INLINE) {
        snprintf(hint, hint_sz, "%s", v.u.ins.bytes);
        return 0;
    }
    if (v.tag == VAL_STR_REF && ctx->pool) {
        return flat_value_get_cstr(&v, ctx->pool, hint, hint_sz);
    }
    return -1;
}

enum {
    INFO_HINT_CSV = 1,
    INFO_HINT_FILE,
    INFO_HINT_VCX,
    INFO_HINT_CLOCK,
    INFO_HINT_HTTP
};

static int info_parse_hint_kind(const char *hint) {
    if (!hint || hint[0] == '\0') {
        return 0;
    }
    if (strcmp(hint, "csv") == 0) {
        return INFO_HINT_CSV;
    }
    if (strcmp(hint, "file") == 0) {
        return INFO_HINT_FILE;
    }
    if (strcmp(hint, "vcx") == 0) {
        return INFO_HINT_VCX;
    }
    if (strcmp(hint, "clock") == 0) {
        return INFO_HINT_CLOCK;
    }
    if (strcmp(hint, "http") == 0) {
        return INFO_HINT_HTTP;
    }
    return 0;
}

static int info_res_kind(FlatNativeCtx *ctx, int hint_kind, uint32_t id, const char **out_kind) {
    int csv_kind;

    if (!out_kind) {
        return -1;
    }
    *out_kind = "unknown";
    if (hint_kind == INFO_HINT_CSV) {
        if (id >= 1u && ctx->csv && flat_csv_session_live(ctx->csv, id, &csv_kind)) {
            *out_kind =
                (csv_kind == (int)FLAT_CSV_SESS_WRITER) ? "csv.writer" : "csv.reader";
        }
        return 0;
    }
    if (hint_kind == INFO_HINT_FILE) {
        if (ctx->file_tbl && id < FLAT_FILE_MAX_HANDLES &&
            flat_file_is_live(ctx->file_tbl, id)) {
            *out_kind = "file";
        }
        return 0;
    }
    if (hint_kind == INFO_HINT_VCX) {
        if (id >= 1u && ctx->vcx && flat_vcx_context_live(ctx->vcx, id)) {
            *out_kind = "vcx";
        }
        return 0;
    }
    if (hint_kind == INFO_HINT_CLOCK) {
        if (flat_clock_stopwatch_live(ctx->clock, id)) *out_kind = "sw";
        return 0;
    }
    if (hint_kind == INFO_HINT_HTTP) {
#if FLAT_WITH_HTTP
        if (flat_http_request_live(ctx->http, id)) *out_kind = "request";
#endif
        return 0;
    }
    return 0;
}

static int info_parse_id_for_hint(int hint_kind, const FlatValue *v, uint32_t *out_id) {
    double d;

    if (!v || !out_id || v->tag != VAL_NUMBER) {
        return -1;
    }
    d = flat_number_as_f64(v);
    if (d != d || floor(d) != d) {
        return -1;
    }
    if (hint_kind == INFO_HINT_FILE) {
        if (d < 0.0 || d >= (double)FLAT_FILE_MAX_HANDLES) {
            return -1;
        }
        *out_id = (uint32_t)d;
        return 0;
    }
    if (hint_kind == INFO_HINT_CLOCK && (d < 1.0 || d > (double)FLAT_CLOCK_MAX_STOPWATCHES)) return -1;
#if FLAT_WITH_HTTP
    if (hint_kind == INFO_HINT_HTTP && (d < 1.0 || d > (double)FLAT_HTTP_MAX_REQUESTS)) return -1;
#else
    if (hint_kind == INFO_HINT_HTTP) return -1;
#endif
    if (d < 1.0) {
        return -1;
    }
    *out_id = (uint32_t)d;
    return 0;
}

static int mem_info_res_fail(FlatNativeCtx *ctx, const char *code) {
    return mem_slot_arg_err(ctx, code);
}

int flat_native_mem_info_res_id(FlatNativeCtx *ctx) {
    FlatValue v;
    char inner_buf[512];
    char hint_buf[32];
    char *left = NULL;
    char *right = NULL;
    uint32_t id;
    int hint_kind;
    const char *kind = "unknown";

    if (!ctx || !ctx->out || !ctx->io_status) {
        return -1;
    }
    ctx->panic_detail[0] = '\0';
    if (!ctx->inner || ctx->inner[0] == '\0') {
        return mem_info_res_fail(ctx, "mem.infoResId.kind");
    }
    snprintf(inner_buf, sizeof inner_buf, "%s", ctx->inner);
    if (info_split_two_args(inner_buf, &left, &right) != 0) {
        return mem_info_res_fail(ctx, "mem.infoResId.kind");
    }
    if (info_parse_hint(ctx, right, hint_buf, sizeof hint_buf) != 0 ||
        (hint_kind = info_parse_hint_kind(hint_buf)) == 0) {
        return mem_info_res_fail(ctx, "mem.infoResId.kind");
    }
    if (info_parse_rhs(ctx, left, &v) != 0 ||
        info_parse_id_for_hint(hint_kind, &v, &id) != 0) {
        return mem_info_res_fail(ctx, "mem.infoResId.id");
    }
    info_res_kind(ctx, hint_kind, id, &kind);
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_str_inline(ctx->out, kind, strlen(kind));
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
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "pool.full");
        *ctx->io_status = FLAT_IO_ERR;
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
            snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "pool.full");
            *ctx->io_status = FLAT_IO_ERR;
            return -1;
        }
    } else if (flat_pool_alloc_copy(ctx->pool, bytes, blen, &id) != 0) {
        free(bytes);
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "pool.full");
        *ctx->io_status = FLAT_IO_ERR;
        return -1;
    }
    free(bytes);
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_str_ref(ctx->out, id);
}
