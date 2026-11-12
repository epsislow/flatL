#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_vcx.h"
#include "flat_value.h"
#include "fb_format.h"

#define VCX_NAME_LIST_MAX 64

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int vcx_number_to_uint32_strict(double d, uint32_t *out) {
    if (!out || d != d || d < 0.0 || d > (double)UINT32_MAX) {
        return -1;
    }
    if (floor(d) != d) {
        return -1;
    }
    *out = (uint32_t)d;
    return 0;
}

static int vcx_out_num(FlatNativeCtx *ctx, double n) {
    if (!ctx || !ctx->out || !ctx->io_status) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_number(ctx->out, n);
}

static int vcx_out_bool(FlatNativeCtx *ctx, int b) {
    if (!ctx || !ctx->out || !ctx->io_status) {
        return -1;
    }
    *ctx->io_status = FLAT_IO_OK;
    return flat_value_set_bool(ctx->out, b);
}

static int vcx_fail(FlatNativeCtx *ctx, const char *reason) {
    if (ctx && ctx->io_status) {
        *ctx->io_status = FLAT_IO_ERR;
    }
    if (ctx && reason && reason != ctx->panic_detail) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", reason);
    }
    return -1;
}

static int parse_expr(FlatNativeCtx *ctx, const char *expr, FlatValue *out) {
    char buf[512];
    if (!expr || !out) {
        return -1;
    }
    snprintf(buf, sizeof buf, "%s", expr);
    return flat_value_parse_assign_rhs(flat_native_trim(buf), out, ctx->pool, ctx->coll, ctx->vars,
                                       ctx->var_count, ctx->var_names);
}

static int parse_ctx_id(FlatNativeCtx *ctx, const char *expr, uint32_t *out_id) {
    FlatValue v;
    char buf[512];

    if (!expr || !out_id) {
        return -1;
    }
    snprintf(buf, sizeof buf, "%s", expr);
    flat_native_trim(buf);
    if (flat_native_is_call_line(buf)) {
        if (flat_native_eval_call(ctx, buf, &v) != 0) {
            return -1;
        }
    } else if (parse_expr(ctx, expr, &v) != 0) {
        return -1;
    }
    if (v.tag != VAL_NUMBER) {
        return -1;
    }
    if (vcx_number_to_uint32_strict(v.u.number, out_id) != 0) {
        return -1;
    }
    if (*out_id < 1u) {
        return -1;
    }
    return 0;
}

static int parse_name_string(FlatNativeCtx *ctx, const char *expr, char *name, size_t name_sz) {
    FlatValue v;
    if (parse_expr(ctx, expr, &v) != 0) {
        return -1;
    }
    if (v.tag == VAL_STR_INLINE) {
        snprintf(name, name_sz, "%s", v.u.ins.bytes);
        return 0;
    }
    if (v.tag == VAL_STR_REF && ctx->pool) {
        return flat_value_get_cstr(&v, ctx->pool, name, name_sz);
    }
    return -1;
}

static int split_two_args(char *inner, char **left, char **right) {
    int depth = 0;
    char quote = 0;
    char *p;
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
            return 0;
        }
    }
    return -1;
}

static int split_three_args(char *inner, char **first, char **second, char **third) {
    int depth = 0;
    char quote = 0;
    char *p;
    char *c1 = NULL;
    char *c2 = NULL;
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
            if (!c1) {
                *p = '\0';
                c1 = p;
            } else if (!c2) {
                *p = '\0';
                c2 = p;
                break;
            }
        }
    }
    if (!c1 || !c2) {
        return -1;
    }
    *first = flat_native_trim(inner);
    *second = flat_native_trim(c1 + 1);
    *third = flat_native_trim(c2 + 1);
    return 0;
}

static int split_four_args(char *inner, char **a, char **b, char **c, char **d) {
    int depth = 0;
    char quote = 0;
    char *p;
    char *c1 = NULL;
    char *c2 = NULL;
    char *c3 = NULL;
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
            if (!c1) {
                *p = '\0';
                c1 = p;
            } else if (!c2) {
                *p = '\0';
                c2 = p;
            } else if (!c3) {
                *p = '\0';
                c3 = p;
                break;
            }
        }
    }
    if (!c1 || !c2 || !c3) {
        return -1;
    }
    *a = flat_native_trim(inner);
    *b = flat_native_trim(c1 + 1);
    *c = flat_native_trim(c2 + 1);
    *d = flat_native_trim(c3 + 1);
    return 0;
}

static int value_to_name_string(FlatNativeCtx *ctx, const FlatValue *v, char *name, size_t name_sz) {
    if (!v || !name) {
        return -1;
    }
    if (v->tag == VAL_STR_INLINE) {
        if (v->u.ins.len == 0) {
            return -1;
        }
        snprintf(name, name_sz, "%s", v->u.ins.bytes);
        return 0;
    }
    if (v->tag == VAL_STR_REF && ctx->pool) {
        if (flat_value_get_cstr(v, ctx->pool, name, name_sz) != 0) {
            return -1;
        }
        if (name[0] == '\0') {
            return -1;
        }
        return 0;
    }
    return -1;
}

static int parse_name_list_expr(FlatNativeCtx *ctx, const char *expr,
                                char names[VCX_NAME_LIST_MAX][FLAT_VAR_NAME_MAX + 1], int *out_n) {
    FlatValue v;
    int len;
    int i;
    FlatValue item;

    if (!out_n) {
        return -1;
    }
    *out_n = 0;
    if (parse_expr(ctx, expr, &v) != 0) {
        return -1;
    }
    if (v.tag != VAL_LIST) {
        return -1;
    }
    if (flat_list_len(ctx->coll, v.u.coll_id, &len) != 0 || len < 0) {
        return -1;
    }
    if (len > VCX_NAME_LIST_MAX) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (flat_list_get(ctx->coll, v.u.coll_id, i, &item) != 0) {
            return -1;
        }
        if (value_to_name_string(ctx, &item, names[i], sizeof names[i]) != 0) {
            return -1;
        }
    }
    *out_n = len;
    return 0;
}

static FlatVcxRuntime *rt_from(FlatNativeCtx *ctx) {
    if (ctx && ctx->vcx) {
        return ctx->vcx;
    }
    return flat_vcx_runtime_active();
}

int flat_native_vcx_call_instr(FlatNativeCtx *ctx) {
    uint32_t id = flat_vcx_call_instr_ctx();

    if (id < 1u) {
        return vcx_fail(ctx, "vcx.callInstr");
    }
    return vcx_out_num(ctx, (double)id);
}

int flat_native_vcx_current(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    if (!rt) {
        return vcx_fail(ctx, "vcx.current");
    }
    ctx->panic_detail[0] = '\0';
    return vcx_out_num(ctx, (double)flat_vcx_current_id(rt));
}

int flat_native_vcx_isactive(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    uint32_t id;
    int active = 0;
    char inner[512];

    if (!rt) {
        return vcx_fail(ctx, "vcx.isactive");
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (inner[0] == '\0' || parse_ctx_id(ctx, inner, &id) != 0) {
        return vcx_fail(ctx, "vcx.isactive.arg");
    }
    if (flat_vcx_is_active(rt, id, &active, ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.isactive.ctx");
    }
    return vcx_out_bool(ctx, active);
}

int flat_native_vcx_set(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    uint32_t id;
    char inner[512];

    if (!rt || !ctx) {
        return vcx_fail(ctx, "vcx.set");
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (inner[0] == '\0' || parse_ctx_id(ctx, inner, &id) != 0) {
        return vcx_fail(ctx, "vcx.set.arg");
    }
    if (flat_vcx_set(rt, id, ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.set.invalid");
    }
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return 0;
}

int flat_native_vcx_create(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    FlatValue capv;
    uint32_t cap;
    uint32_t new_id;
    char inner[512];

    if (!rt) {
        return vcx_fail(ctx, "vcx.create");
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (inner[0] == '\0' || parse_expr(ctx, inner, &capv) != 0 || capv.tag != VAL_NUMBER) {
        return vcx_fail(ctx, "vcx.create.arg");
    }
    if (vcx_number_to_uint32_strict(capv.u.number, &cap) != 0 || cap < 1u) {
        return vcx_fail(ctx, "vcx.create.arg");
    }
    if (flat_vcx_create(rt, cap, &new_id, ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.create");
    }
    return vcx_out_num(ctx, (double)new_id);
}

int flat_native_vcx_create_auto(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    uint32_t seed = FLAT_VCX_AUTO_DEFAULT_SEED;
    uint32_t new_id;
    char inner[512];

    if (!rt) {
        return vcx_fail(ctx, "vcx.createAuto");
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    flat_native_trim(inner);
    if (inner[0] != '\0') {
        FlatValue capv;

        if (parse_expr(ctx, inner, &capv) != 0 || capv.tag != VAL_NUMBER) {
            return vcx_fail(ctx, "vcx.createAuto.arg");
        }
        if (vcx_number_to_uint32_strict(capv.u.number, &seed) != 0) {
            return vcx_fail(ctx, "vcx.createAuto.arg");
        }
        if (seed < 1u) {
            seed = FLAT_VCX_AUTO_DEFAULT_SEED;
        }
    }
    if (flat_vcx_create_auto(rt, seed, &new_id, ctx->panic_detail, sizeof ctx->panic_detail) !=
        0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.createAuto");
    }
    return vcx_out_num(ctx, (double)new_id);
}

int flat_native_vcx_var(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    char inner[512];
    char *a;
    char *b;
    uint32_t src_id;
    char name[FLAT_VAR_NAME_MAX + 1];
    FlatValue v;

    FlatValue sink;
    FlatValue *out;

    if (!rt || !ctx) {
        return vcx_fail(ctx, "vcx.var");
    }
    out = ctx->out ? ctx->out : &sink;
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (split_two_args(inner, &a, &b) != 0) {
        return vcx_fail(ctx, "vcx.var.arg");
    }
    if (parse_ctx_id(ctx, a, &src_id) != 0 || parse_name_string(ctx, b, name, sizeof name) != 0) {
        return vcx_fail(ctx, "vcx.var.arg");
    }
    if (flat_vcx_get_var(rt, src_id, name, &v, ctx->coll, ctx->pool, ctx->file_tbl,
                         ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.var.missing");
    }
    *ctx->io_status = FLAT_IO_OK;
    *out = v;
    return 0;
}

int flat_native_vcx_has_var(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    char inner[512];
    char *a;
    char *b;
    uint32_t cid;
    char name[FLAT_VAR_NAME_MAX + 1];
    int ok = 0;

    if (!rt) {
        return vcx_fail(ctx, "vcx.hasVar");
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (split_two_args(inner, &a, &b) != 0) {
        return vcx_fail(ctx, "vcx.hasVar.arg");
    }
    if (parse_ctx_id(ctx, a, &cid) != 0 || parse_name_string(ctx, b, name, sizeof name) != 0) {
        return vcx_fail(ctx, "vcx.hasVar.arg");
    }
    if (flat_vcx_has_var(rt, cid, name, &ok, ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.hasVar.ctx");
    }
    return vcx_out_bool(ctx, ok);
}

int flat_native_vcx_vars_copy(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    char inner[512];
    char *a;
    char *b;
    uint32_t src;
    uint32_t dst;

    if (!rt) {
        return vcx_fail(ctx, "vcx.varsCopy");
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (split_two_args(inner, &a, &b) != 0) {
        return vcx_fail(ctx, "vcx.varsCopy.arg");
    }
    if (parse_ctx_id(ctx, a, &src) != 0 || parse_ctx_id(ctx, b, &dst) != 0) {
        return vcx_fail(ctx, "vcx.varsCopy.arg");
    }
    if (flat_vcx_vars_copy(rt, src, dst, ctx->coll, ctx->pool, ctx->file_tbl, ctx->mem_live,
                           ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.varsCopy");
    }
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return 0;
}

int flat_native_vcx_vars_copy_like(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    char inner[512];
    char *a;
    char *b;
    char *c;
    uint32_t src;
    uint32_t dst;
    char pattern[256];

    if (!rt) {
        return vcx_fail(ctx, "vcx.varsCopyLike");
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (split_three_args(inner, &a, &b, &c) != 0) {
        return vcx_fail(ctx, "vcx.varsCopyLike.arg");
    }
    if (parse_ctx_id(ctx, a, &src) != 0 || parse_ctx_id(ctx, b, &dst) != 0) {
        return vcx_fail(ctx, "vcx.varsCopyLike.arg");
    }
    if (parse_name_string(ctx, c, pattern, sizeof pattern) != 0) {
        return vcx_fail(ctx, "vcx.varsCopyLike.pattern");
    }
    if (flat_vcx_vars_copy_like(rt, src, dst, pattern, ctx->coll, ctx->pool, ctx->file_tbl,
                                ctx->mem_live, ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.varsCopyLike");
    }
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return 0;
}

static int vcx_ctx_stat(FlatNativeCtx *ctx, const char *fn,
                        int (*fn_stat)(const FlatVcxRuntime *, uint32_t, int *, char *, size_t)) {
    FlatVcxRuntime *rt = rt_from(ctx);
    uint32_t id;
    int n;
    char inner[512];

    if (!rt) {
        return vcx_fail(ctx, fn);
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (inner[0] == '\0' || parse_ctx_id(ctx, inner, &id) != 0) {
        snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s.arg", fn);
        return vcx_fail(ctx, ctx->panic_detail);
    }
    if (fn_stat(rt, id, &n, ctx->panic_detail, sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : fn);
    }
    return vcx_out_num(ctx, (double)n);
}

int flat_native_vcx_vars_used(FlatNativeCtx *ctx) {
    return vcx_ctx_stat(ctx, "vcx.varsUsed", flat_vcx_vars_used);
}

int flat_native_vcx_vars_avail(FlatNativeCtx *ctx) {
    return vcx_ctx_stat(ctx, "vcx.varsAvail", flat_vcx_vars_avail);
}

int flat_native_vcx_vars_max(FlatNativeCtx *ctx) {
    return vcx_ctx_stat(ctx, "vcx.varsMax", flat_vcx_vars_max);
}

int flat_native_vcx_destroy(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    uint32_t id;
    char inner[512];

    if (!rt) {
        return vcx_fail(ctx, "vcx.destroy");
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (inner[0] == '\0' || parse_ctx_id(ctx, inner, &id) != 0) {
        return vcx_fail(ctx, "vcx.destroy.arg");
    }
    if (flat_vcx_destroy_ctx(rt, id, ctx->coll, ctx->pool, ctx->file_tbl, ctx->panic_detail,
                             sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.destroy");
    }
    (void)flat_vm_instr_stack_pop(id);
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return 0;
}

int flat_native_vcx_vars_copy_by_names(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    char inner[2048];
    char *a;
    char *b;
    char *c;
    char *d;
    uint32_t src;
    uint32_t dst;
    char src_names[VCX_NAME_LIST_MAX][FLAT_VAR_NAME_MAX + 1];
    char dst_names[VCX_NAME_LIST_MAX][FLAT_VAR_NAME_MAX + 1];
    int src_n = 0;
    int dst_n = 0;
    const char *src_ptrs[VCX_NAME_LIST_MAX];
    const char *dst_ptrs[VCX_NAME_LIST_MAX];
    int i;

    if (!rt) {
        return vcx_fail(ctx, "vcx.varsCopyByNames");
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (split_four_args(inner, &a, &b, &c, &d) != 0) {
        return vcx_fail(ctx, "vcx.varsCopyByNames.arg");
    }
    if (parse_ctx_id(ctx, a, &src) != 0 || parse_ctx_id(ctx, b, &dst) != 0) {
        return vcx_fail(ctx, "vcx.varsCopyByNames.arg");
    }
    if (parse_name_list_expr(ctx, c, src_names, &src_n) != 0 ||
        parse_name_list_expr(ctx, d, dst_names, &dst_n) != 0) {
        return vcx_fail(ctx, "vcx.varsCopyByNames.list");
    }
    if (src_n != dst_n) {
        return vcx_fail(ctx, "vcx.varsCopyByNames.length");
    }
    for (i = 0; i < src_n; i++) {
        src_ptrs[i] = src_names[i];
        dst_ptrs[i] = dst_names[i];
    }
    if (flat_vcx_vars_copy_by_names(rt, src, dst, src_ptrs, dst_ptrs, src_n, ctx->coll, ctx->pool,
                                    ctx->file_tbl, ctx->mem_live, ctx->panic_detail,
                                    sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.varsCopyByNames");
    }
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return 0;
}

int flat_native_vcx_bind_call_params(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    char inner[256];
    char *instr_e;
    char *dst_e;
    uint32_t instr_id;
    uint32_t dst_id;

    if (!rt || !ctx->bc) {
        return vcx_fail(ctx, "vcx.bindCallParams");
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (split_two_args(inner, &instr_e, &dst_e) != 0) {
        return vcx_fail(ctx, "vcx.bindCallParams.arg");
    }
    if (parse_ctx_id(ctx, instr_e, &instr_id) != 0 || parse_ctx_id(ctx, dst_e, &dst_id) != 0) {
        return vcx_fail(ctx, "vcx.bindCallParams.arg");
    }
    if (flat_vcx_bind_call_params(rt, instr_id, dst_id, ctx->bc, ctx->coll, ctx->pool,
                                  ctx->file_tbl, ctx->mem_live, ctx->panic_detail,
                                  sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.bindCallParams");
    }
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return 0;
}

int flat_native_vcx_copy_call_return(FlatNativeCtx *ctx) {
    FlatVcxRuntime *rt = rt_from(ctx);
    char inner[256];
    char *src_e;
    char *dst_e;
    char *instr_e;
    uint32_t src_id;
    uint32_t dst_id;
    uint32_t instr_id;

    if (!rt || !ctx->bc) {
        return vcx_fail(ctx, "vcx.copyCallReturn");
    }
    ctx->panic_detail[0] = '\0';
    snprintf(inner, sizeof inner, "%s", ctx->inner ? ctx->inner : "");
    if (split_three_args(inner, &src_e, &dst_e, &instr_e) != 0) {
        return vcx_fail(ctx, "vcx.copyCallReturn.arg");
    }
    if (parse_ctx_id(ctx, src_e, &src_id) != 0 || parse_ctx_id(ctx, dst_e, &dst_id) != 0 ||
        parse_ctx_id(ctx, instr_e, &instr_id) != 0) {
        return vcx_fail(ctx, "vcx.copyCallReturn.arg");
    }
    if (flat_vcx_copy_call_return(rt, src_id, dst_id, instr_id, ctx->bc, ctx->coll, ctx->pool,
                                  ctx->file_tbl, ctx->mem_live, ctx->panic_detail,
                                  sizeof ctx->panic_detail) != 0) {
        return vcx_fail(ctx, ctx->panic_detail[0] ? ctx->panic_detail : "vcx.copyCallReturn");
    }
    if (ctx->io_status) {
        *ctx->io_status = FLAT_IO_OK;
    }
    return 0;
}
