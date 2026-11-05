#include "vm.h"
#include "flat_date.h"
#include "flat_file.h"
#include "flat_io_ctx.h"
#include "flat_native_plugin.h"
#include "flat.h"
#include "flat_mem_stats.h"
#include "flat_native.h"
#include "flat_str.h"
#include "flat_value.h"

#ifdef FLAT_WASM_BUILD
#include "flat_wasm_io.h"
#endif

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VM_MAX_CALL 64
#define VM_MAX_FOREACH 8
#define VM_MAX_LIST 16

static int vm_block_index(const FlatBytecode *bc, uint32_t ip);
static void vm_native_decls_at_ip(const FlatBytecode *bc, uint32_t ip,
                                    const FlatOptionalNativeDecl **decls_out, int *count_out);

typedef struct {
    char name[FLAT_VAR_NAME_MAX + 1];
    FlatValue val;
} NamedVar;

typedef struct {
    uint32_t return_ip;
} CallFrame;

typedef struct {
    uint32_t resume_ip;
    uint32_t loop_block_ip;
    char items[VM_MAX_LIST][64];
    int item_count;
    int index;
    char item_var[FLAT_VAR_NAME_MAX + 1];
    int use_list;
    uint32_t list_id;
    int is_for;
    int list_retained;
} ForeachFrame;

typedef struct {
    const FlatBytecode *bc;
    FlatStringPool *pool;
    FlatCollectionPool *coll;
    NamedVar *vars;
    uint32_t var_slots_max;
    int var_count;
    FlatMemLiveStats live;
    CallFrame calls[VM_MAX_CALL];
    int call_sp;
    ForeachFrame loops[VM_MAX_FOREACH];
    int loop_sp;
    char panic_reason[64];
    int unhandled_panic;
    FlatFileTable files;
    FlatIOContext *io_ctx;
    int via_jump;
    uint64_t step_count;
    uint64_t jump_count;
    const FlatVmOptions *run_opts;
} VmState;

static void vm_write_stderr(const char *s) {
    size_t n;

    if (!s || !*s) {
        return;
    }
    n = strlen(s);
#ifdef FLAT_WASM_BUILD
    flat_wasm_io_write_stderr(s, n);
#else
    fwrite(s, 1, n, stderr);
#endif
}

static void vm_record_run_message(const FlatVmOptions *opts, const VmState *st, FlatTermination term,
                                  int exit_code) {
    if (!opts || !opts->out_panic_msg || opts->out_panic_msg_sz == 0) {
        return;
    }
    opts->out_panic_msg[0] = '\0';
    if (exit_code == 0) {
        return;
    }
    if (st && st->panic_reason[0] != '\0') {
        if (st->unhandled_panic) {
            snprintf(opts->out_panic_msg, opts->out_panic_msg_sz, "unhandled PANIC: %s",
                     st->panic_reason);
        } else {
            snprintf(opts->out_panic_msg, opts->out_panic_msg_sz, "%s", st->panic_reason);
        }
        return;
    }
    if (term == FLAT_TERM_FAIL) {
        snprintf(opts->out_panic_msg, opts->out_panic_msg_sz, "logical FAIL");
    } else if (term == FLAT_TERM_PANIC) {
        snprintf(opts->out_panic_msg, opts->out_panic_msg_sz, "unhandled PANIC");
    } else if (term == FLAT_TERM_ERROR) {
        snprintf(opts->out_panic_msg, opts->out_panic_msg_sz, "VM error");
    }
}

static void vm_teardown(VmState *st, int rc);

static int vm_finish(VmState *st, char *guard_names, FlatValue *guard_vals, int exit_code,
                     FlatTermination term, const FlatVmOptions *opts) {
    if (opts && opts->out_termination) {
        *opts->out_termination = (int)term;
    }
    vm_record_run_message(opts, st, term, exit_code);
    free(guard_names);
    free(guard_vals);
    vm_teardown(st, exit_code);
    return exit_code;
}

static FlatTermination vm_term_for_exit1(const VmState *st) {
    return st->unhandled_panic ? FLAT_TERM_PANIC : FLAT_TERM_ERROR;
}

static uint64_t vm_effective_max_jumps(const FlatVmOptions *opts) {
    if (opts && opts->no_jump_limit) {
        return UINT64_MAX;
    }
    if (opts && opts->max_jumps > 0) {
        return opts->max_jumps;
    }
    return (uint64_t)FLAT_VM_DEFAULT_MAX_JUMPS;
}

static int vm_fail_setup(VmState *st, const FlatVmOptions *opts) {
    if (opts && opts->out_termination) {
        *opts->out_termination = (int)FLAT_TERM_ERROR;
    }
    vm_teardown(st, 1);
    return 1;
}

static uint32_t flat_label_hash(const char *label) {
    uint32_t h = 5381u;
    const unsigned char *p;
    for (p = (const unsigned char *)label; *p; p++) {
        h = h * 33u + *p;
    }
    return h;
}

static int vm_jump_panic_any(VmState *st, const FlatBytecode *bc, uint32_t *ip, const char *reason,
                             int trace) {
    size_t i;
    uint32_t want = flat_label_hash("#panic.any");

    if (trace) {
        {
            char tbuf[128];
            snprintf(tbuf, sizeof tbuf, "[trace] panic reason=%s handler=#panic.any\n", reason);
            vm_write_stderr(tbuf);
        }
    }
    for (i = 0; i < bc->block_count; i++) {
        if (bc->blocks[i].block_id == want) {
            *ip = bc->blocks[i].offset;
            return 0;
        }
    }
    {
        char line[128];
        snprintf(line, sizeof line, "flvm: unhandled PANIC: %s\n", reason);
        vm_write_stderr(line);
    }
    if (st) {
        st->unhandled_panic = 1;
        snprintf(st->panic_reason, sizeof st->panic_reason, "%s", reason ? reason : "panic");
    }
    return 1;
}

static const char *io_status_reason(FlatIoStatus st) {
    switch (st) {
    case FLAT_IO_PANIC_EOF:
        return "io.eof";
    case FLAT_IO_PANIC_CANCEL:
        return "io.user_cancel";
    case FLAT_IO_PANIC_PARSE:
        return "io.parse";
    case FLAT_IO_PANIC_CTX:
        return "io.context";
    default:
        return "io.error";
    }
}

static int vm_handle_io_status(VmState *st, const FlatBytecode *bc, uint32_t *ip, FlatIoStatus ios,
                               int trace) {
    if (ios == FLAT_IO_OK || ios == FLAT_IO_ERR) {
        return 0;
    }
    if (ios == FLAT_IO_PANIC_CTX && st && st->io_ctx) {
        snprintf(st->panic_reason, sizeof st->panic_reason, "%s",
                 flat_io_ctx_panic_reason(st->io_ctx));
        flat_io_ctx_reset_terminals(st->io_ctx);
    } else {
        snprintf(st->panic_reason, sizeof st->panic_reason, "%s", io_status_reason(ios));
    }
    return vm_jump_panic_any(st, bc, ip, st->panic_reason, trace);
}

/* 0 = continue; 1 = unhandled panic; 2 = logical FAIL (~ without #or). */
static int vm_line_mod_outcome(VmState *st, const FlatBytecode *bc, uint32_t *ip, uint16_t native_id,
                               uint8_t mod, uint32_t or_target, int invoke_rc, FlatIoStatus ios,
                               int trace) {
    int soft_fail = (invoke_rc != 0 || ios == FLAT_IO_ERR);

    if (!soft_fail && ios == FLAT_IO_OK) {
        return 0;
    }
    if (!soft_fail && ios != FLAT_IO_OK) {
        if (mod == FLAT_LINE_MOD_Q) {
            return 0;
        }
        if (mod == FLAT_LINE_MOD_TILDE) {
            if (or_target == 0) {
                return 2;
            }
            *ip = or_target;
            return 0;
        }
        if (vm_handle_io_status(st, bc, ip, ios, trace) != 0) {
            return 1;
        }
        return 0;
    }

    if (mod == FLAT_LINE_MOD_Q) {
        return 0;
    }
    if (mod == FLAT_LINE_MOD_TILDE) {
        if (or_target == 0) {
            return 2;
        }
        *ip = or_target;
        return 0;
    }
    snprintf(st->panic_reason, sizeof st->panic_reason, "%s", flat_native_name(native_id));
    if (trace) {
        fprintf(stderr, "[trace] panic reason=%s (native fail)\n", st->panic_reason);
    }
    if (vm_jump_panic_any(st, bc, ip, st->panic_reason, trace) != 0) {
        return 1;
    }
    return 0;
}

static int find_var(VmState *st, const char *name) {
    int i;
    for (i = 0; i < st->var_count; i++) {
        if (strcmp(st->vars[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int var_name_forbidden(const char *name) {
    return strcmp(name, "null") == 0 || strcmp(name, "true") == 0 || strcmp(name, "false") == 0;
}

static void var_names_table(VmState *st, char (*names)[64]) {
    int i;
    for (i = 0; i < st->var_count; i++) {
        snprintf(names[i], 64, "%s", st->vars[i].name);
    }
}

static int flat_value_same_storage(const FlatValue *a, const FlatValue *b) {
    if (!a || !b || a->tag != b->tag) {
        return 0;
    }
    switch (a->tag) {
    case VAL_STR_REF:
        return a->u.ref_id == b->u.ref_id;
    case VAL_LIST:
        return a->u.coll_id == b->u.coll_id;
    case VAL_OBJECT:
        return a->u.coll_id == b->u.coll_id;
    case VAL_FILE:
        return a->u.file_id == b->u.file_id;
    default:
        return 0;
    }
}

static int set_var_value(VmState *st, const char *name, const FlatValue *val) {
    int i = find_var(st, name);
    if (i >= 0) {
        if (flat_value_same_storage(&st->vars[i].val, val)) {
            return 0;
        }
        if (st->vars[i].val.tag == VAL_FILE) {
            flat_file_release(&st->files, st->vars[i].val.u.file_id);
        }
        flat_value_retain(st->coll, st->pool, val);
        if (val->tag == VAL_FILE) {
            flat_file_retain(&st->files, val->u.file_id);
        }
        flat_value_live_replace(&st->live, &st->vars[i].val, val);
        flat_value_release(st->coll, st->pool, &st->vars[i].val);
        st->vars[i].val = *val;
        if (st->io_ctx) {
            flat_io_ctx_var_updated(st->io_ctx, i, val);
        }
        return 0;
    }
    if ((uint32_t)st->var_count >= st->var_slots_max) {
        return -1;
    }
    if (var_name_forbidden(name)) {
        return -1;
    }
    flat_value_retain(st->coll, st->pool, val);
    if (val->tag == VAL_FILE) {
        flat_file_retain(&st->files, val->u.file_id);
    }
    flat_value_live_bind(&st->live, val);
    snprintf(st->vars[st->var_count].name, sizeof st->vars[st->var_count].name, "%s", name);
    st->vars[st->var_count].val = *val;
    if (st->io_ctx) {
        flat_io_ctx_var_updated(st->io_ctx, st->var_count, val);
    }
    st->var_count++;
    return 0;
}

static int apply_free(VmState *st, const char *varname) {
    FlatValue nullv;
    int i = find_var(st, varname);
    if (i < 0) {
        return 0;
    }
    if (st->vars[i].val.tag == VAL_FILE) {
        flat_file_release(&st->files, st->vars[i].val.u.file_id);
    }
    flat_value_release(st->coll, st->pool, &st->vars[i].val);
    flat_value_set_null(&nullv);
    flat_value_live_replace(&st->live, &st->vars[i].val, &nullv);
    flat_value_set_null(&st->vars[i].val);
    if (st->io_ctx) {
        flat_io_ctx_var_updated(st->io_ctx, i, &nullv);
    }
    return 0;
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) {
        *e-- = '\0';
    }
    return s;
}

static int rhs_is_native_call(const char *rhs) {
    return flat_native_is_call_line(rhs);
}

static int decode_assign_payload(const uint8_t *payload, uint8_t payload_size, uint8_t *mod_out,
                                 const char **line_out, uint32_t *or_out) {
    const char *line;
    size_t line_len;

    if (!payload || payload_size < 2 || !mod_out || !line_out || !or_out) {
        return -1;
    }
    *mod_out = payload[0];
    line = (const char *)(payload + 1);
    line_len = strlen(line) + 1;
    *line_out = line;
    *or_out = 0;
    if (*mod_out == FLAT_LINE_MOD_TILDE && payload_size >= 1 + line_len + 4) {
        memcpy(or_out, payload + 1 + line_len, 4);
    }
    return 0;
}

static int apply_assign(VmState *st, const FlatBytecode *bc, uint32_t *ip,
                        const uint8_t *payload, uint8_t payload_size, int trace) {
    char buf[4096];
    char *eq;
    char *lhs;
    char *rhs;
    FlatValue v;
    FlatIoStatus ios;
    FlatValue *vals = NULL;
    char (*names)[64] = NULL;
    size_t scratch_n;
    int i;
    int rc = 0;
    uint8_t mod = FLAT_LINE_MOD_NONE;
    const char *line = NULL;
    uint32_t or_target = 0;
    uint16_t native_id = 0;

    if (decode_assign_payload(payload, payload_size, &mod, &line, &or_target) != 0) {
        return -1;
    }

    scratch_n = st->var_slots_max > 0 ? st->var_slots_max : 1u;
    vals = calloc(scratch_n, sizeof *vals);
    names = calloc(scratch_n, 64);
    if (!vals || !names) {
        free(vals);
        free(names);
        return -1;
    }

    snprintf(buf, sizeof buf, "%s", line);
    eq = strchr(buf, '=');
    if (!eq) {
        return 0;
    }
    *eq = '\0';
    lhs = trim(buf);
    rhs = trim(eq + 1);
    if (var_name_forbidden(lhs)) {
        rc = 0;
        goto done;
    }
    var_names_table(st, names);
    for (i = 0; i < st->var_count; i++) {
        vals[i] = st->vars[i].val;
    }
    if (rhs_is_native_call(rhs)) {
        const FlatOptionalNativeDecl *ndecl = NULL;
        int ndecl_count = 0;
        char inner[4096];
        int invoke_rc;

        vm_native_decls_at_ip(bc, *ip, &ndecl, &ndecl_count);
        ios = FLAT_IO_ERR;
        if (ndecl && ndecl_count > 0) {
            invoke_rc =
                flat_native_split_call_manifest(rhs, ndecl, ndecl_count, &native_id, inner,
                                                sizeof inner) != 0
                    ? -1
                    : 0;
        } else {
            invoke_rc =
                flat_native_split_call(rhs, &native_id, inner, sizeof inner) != 0 ? -1 : 0;
        }
        if (invoke_rc == 0) {
            FlatNativeCtx ctx;
            ctx.pool = st->pool;
            ctx.coll = st->coll;
            ctx.vars = vals;
            ctx.var_count = st->var_count;
            ctx.var_names = names;
            ctx.inner = inner;
            ctx.out = &v;
            ctx.io_status = &ios;
            ctx.mem_live = &st->live;
            ctx.vars_max = st->var_slots_max;
            ctx.bc = st->bc;
            ctx.io_ctx = st->io_ctx;
            ctx.file_tbl = &st->files;
            vm_native_decls_at_ip(bc, *ip, &ctx.doc_decls, &ctx.doc_decl_count);
            invoke_rc = flat_native_invoke(native_id, &ctx);
        }
        rc = vm_line_mod_outcome(st, bc, ip, native_id, mod, or_target, invoke_rc, ios, trace);
        if (rc != 0) {
            goto done;
        }
        if (invoke_rc == 0 && ios == FLAT_IO_OK) {
            set_var_value(st, lhs, &v);
        }
        rc = 0;
        goto done;
    }
    if (flat_value_parse_assign_rhs(rhs, &v, st->pool, st->coll, vals, st->var_count, names) != 0) {
        rc = 0;
        goto done;
    }
    set_var_value(st, lhs, &v);
    rc = 0;
done:
    free(vals);
    free(names);
    return rc;
}

static int split_csv_from_var(VmState *st, const char *coll, char items[][64], int max) {
    char buf[4096];
    int vi = find_var(st, coll);
    char *bp;
    char *tok;
    int n = 0;

    if (vi < 0) {
        return 0;
    }
    if (flat_value_get_cstr(&st->vars[vi].val, st->pool, buf, sizeof buf) != 0) {
        return 0;
    }
    bp = buf;
    while (n < max && (tok = flat_str_next(&bp, ',')) != NULL) {
        snprintf(items[n], 64, "%s", trim(tok));
        n++;
    }
    return n;
}

static void set_var_from_cstr(VmState *st, const char *name, const char *val) {
    FlatValue v;
    if (flat_value_set_str_inline(&v, val, strlen(val)) == 0) {
        set_var_value(st, name, &v);
    }
}

static void set_var_from_number(VmState *st, const char *name, double n) {
    FlatValue v;
    if (flat_value_set_number(&v, n) == 0) {
        set_var_value(st, name, &v);
    }
}

static int for_limit_from_double(double d, int *out_iters) {
    if (isnan(d) || isinf(d) || d <= 0.0) {
        *out_iters = 0;
        return 0;
    }
    if (d >= (double)INT_MAX) {
        return -1;
    }
    *out_iters = (int)floor(d);
    return 0;
}

static int eval_for_ntimes(VmState *st, const char *token, int *out_iters) {
    char buf[64];
    char *end = NULL;
    double d;
    int vi;

    snprintf(buf, sizeof buf, "%s", token);
    trim(buf);
    d = strtod(buf, &end);
    if (end != buf && *end == '\0') {
        if (for_limit_from_double(d, out_iters) != 0) {
            return -2;
        }
        return 0;
    }
    vi = find_var(st, buf);
    if (vi < 0) {
        return -2;
    }
    if (st->vars[vi].val.tag != VAL_NUMBER) {
        return -2;
    }
    if (for_limit_from_double(st->vars[vi].val.u.number, out_iters) != 0) {
        return -2;
    }
    return 0;
}

/* Returns: 1 = loop pushed; 2 = routed to #panic.any (ip updated); 0 = unhandled panic. */
static int setup_for(VmState *st, const FlatBytecode *bc, uint32_t *ip, const char *line,
                     uint32_t resume_ip, uint32_t block_off, int trace) {
    char limit_tok[64] = "";
    char item[64] = "";
    const char *lp = strchr(line, '(');
    const char *rp = strrchr(line, ')');
    ForeachFrame *fr;
    int iters = 0;
    int ev;

    if (lp && rp && rp > lp) {
        char inner[256];
        char *ipp;
        char *tok;
        int part = 0;
        snprintf(inner, sizeof inner, "%.*s", (int)(rp - lp - 1), lp + 1);
        ipp = inner;
        while ((tok = flat_str_next(&ipp, ',')) != NULL) {
            if (part == 0) {
                snprintf(limit_tok, sizeof limit_tok, "%s", trim(tok));
            } else if (part == 1) {
                snprintf(item, sizeof item, "%s", trim(tok));
            }
            part++;
        }
    }
    ev = eval_for_ntimes(st, limit_tok, &iters);
    if (ev != 0) {
        if (vm_jump_panic_any(st, bc, ip, "for.ntimes_type", trace) == 0) {
            return 2;
        }
        return 0;
    }
    if (st->loop_sp >= VM_MAX_FOREACH) {
        return 0;
    }
    fr = &st->loops[st->loop_sp++];
    fr->resume_ip = resume_ip;
    fr->loop_block_ip = block_off;
    snprintf(fr->item_var, sizeof fr->item_var, "%s", item);
    fr->is_for = 1;
    fr->use_list = 0;
    fr->list_id = 0;
    fr->item_count = iters;
    fr->index = 0;
    fr->list_retained = 0;
    if (fr->item_count > 0) {
        set_var_from_number(st, fr->item_var, 0.0);
    } else {
        st->loop_sp--;
    }
    return 1;
}

static void foreach_frame_end(VmState *st, ForeachFrame *fr) {
    if (fr->list_retained) {
        FlatValue lv;
        flat_value_set_list(&lv, fr->list_id);
        flat_value_release(st->coll, st->pool, &lv);
        fr->list_retained = 0;
    }
}

static void setup_foreach(VmState *st, const char *line, uint32_t resume_ip, uint32_t block_off) {
    char coll[64] = "";
    char item[64] = "";
    const char *lp = strchr(line, '(');
    const char *rp = strrchr(line, ')');
    ForeachFrame *fr;

    if (lp && rp && rp > lp) {
        char inner[256];
        char *ip;
        char *tok;
        int part = 0;
        snprintf(inner, sizeof inner, "%.*s", (int)(rp - lp - 1), lp + 1);
        ip = inner;
        while ((tok = flat_str_next(&ip, ',')) != NULL) {
            if (part == 0) {
                snprintf(coll, sizeof coll, "%s", trim(tok));
            } else if (part == 1) {
                snprintf(item, sizeof item, "%s", trim(tok));
            }
            part++;
        }
    }
    if (st->loop_sp >= VM_MAX_FOREACH) {
        return;
    }
    fr = &st->loops[st->loop_sp++];
    fr->resume_ip = resume_ip;
    fr->loop_block_ip = block_off;
    snprintf(fr->item_var, sizeof fr->item_var, "%s", item);
    fr->is_for = 0;
    fr->use_list = 0;
    fr->list_id = 0;
    fr->list_retained = 0;
    {
        int vi = find_var(st, coll);
        if (vi >= 0 && st->vars[vi].val.tag == VAL_LIST) {
            int len = 0;
            fr->use_list = 1;
            fr->list_id = st->vars[vi].val.u.coll_id;
            if (flat_list_len(st->coll, fr->list_id, &len) == 0) {
                fr->item_count = len;
            } else {
                fr->item_count = 0;
            }
        } else {
            fr->item_count = split_csv_from_var(st, coll, fr->items, VM_MAX_LIST);
        }
    }
    if (fr->use_list && fr->item_count > 0) {
        FlatValue lv;
        flat_value_set_list(&lv, fr->list_id);
        flat_value_retain(st->coll, st->pool, &lv);
        fr->list_retained = 1;
    }
    fr->index = 0;
    if (fr->item_count > 0) {
        if (fr->use_list) {
            FlatValue el;
            if (flat_list_get(st->coll, fr->list_id, 0, &el) == 0) {
                set_var_value(st, fr->item_var, &el);
            }
        } else {
            set_var_from_cstr(st, fr->item_var, fr->items[0]);
        }
    } else {
        foreach_frame_end(st, fr);
        st->loop_sp--;
    }
}

static void vm_teardown(VmState *st, int rc) {
    int i;
    (void)rc;
    if (st->vars) {
        for (i = 0; i < st->var_count; i++) {
            flat_value_release(st->coll, st->pool, &st->vars[i].val);
        }
        free(st->vars);
        st->vars = NULL;
    }
    flat_io_ctx_destroy(st->io_ctx);
    st->io_ctx = NULL;
    flat_file_table_close_all(&st->files);
    flat_coll_destroy(st->coll);
    flat_pool_destroy(st->pool);
}

static const char *opcode_name(uint8_t opcode) {
    switch ((OpCode)opcode) {
    case OP_ASSIGN:
        return "ASSIGN";
    case OP_GUARD:
        return "GUARD";
    case OP_CALL:
        return "CALL";
    case OP_RETURN:
        return "RETURN";
    case OP_FOREACH:
        return "FOREACH";
    case OP_FOR:
        return "FOR";
    case OP_OK:
        return "OK";
    case OP_FAIL:
        return "FAIL";
    case OP_PANIC:
        return "PANIC";
    case OP_IO:
        return "IO";
    case OP_NATIVE:
        return "NATIVE";
    case OP_FREE:
        return "FREE";
    case OP_JUMP:
        return "JUMP";
    default:
        return "?";
    }
}

static void trace_vars_dump(const VmState *st) {
    int i;
    fprintf(stderr, "[trace-vars] %d var(s):\n", st->var_count);
    for (i = 0; i < st->var_count; i++) {
        fprintf(stderr, "  %s = ", st->vars[i].name);
        flat_value_fprint(stderr, &st->vars[i].val, st->pool, st->coll);
        fputc('\n', stderr);
    }
}

static void vm_native_decls_at_ip(const FlatBytecode *bc, uint32_t ip,
                                  const FlatOptionalNativeDecl **decls_out, int *count_out) {
    int bi;

    *decls_out = bc->optional_natives;
    *count_out = bc->optional_native_count;
    bi = vm_block_index(bc, ip);
    if (bi >= 0 && bc->native_units && bc->block_native_unit &&
        (size_t)bi < bc->block_count) {
        int ui = (int)bc->block_native_unit[bi];
        if (ui >= 0 && ui < bc->native_unit_count) {
            *decls_out = bc->native_units[ui].decls;
            *count_out = bc->native_units[ui].decl_count;
        }
    }
}

static int vm_block_index(const FlatBytecode *bc, uint32_t ip) {
    size_t i;

    if (!bc || !bc->blocks) {
        return -1;
    }
    for (i = 0; i < bc->block_count; i++) {
        uint32_t start = bc->blocks[i].offset;
        uint32_t end = (i + 1 < bc->block_count) ? bc->blocks[i + 1].offset : (uint32_t)bc->size;
        if (ip >= start && ip < end) {
            return (int)i;
        }
    }
    return -1;
}

static int run_native_call(VmState *st, const FlatBytecode *bc, uint32_t *ip, uint16_t native_id,
                           const char *inner, const char *bind, uint8_t mod, uint32_t or_target,
                           int trace) {
    FlatValue result;
    FlatIoStatus ios;
    FlatNativeCtx ctx;
    FlatValue *vals = NULL;
    char (*names)[64] = NULL;
    size_t scratch_n;
    int i;
    int rc = 0;

    scratch_n = st->var_slots_max > 0 ? st->var_slots_max : 1u;
    vals = calloc(scratch_n, sizeof *vals);
    names = calloc(scratch_n, 64);
    if (!vals || !names) {
        free(vals);
        free(names);
        return 0;
    }

    var_names_table(st, names);
    for (i = 0; i < st->var_count; i++) {
        vals[i] = st->vars[i].val;
    }
    ctx.pool = st->pool;
    ctx.coll = st->coll;
    ctx.vars = vals;
    ctx.var_count = st->var_count;
    ctx.var_names = names;
    ctx.inner = inner;
    ctx.out = &result;
    ctx.io_status = &ios;
    ctx.mem_live = &st->live;
    ctx.vars_max = st->var_slots_max;
    ctx.bc = st->bc;
    ctx.io_ctx = st->io_ctx;
    ctx.file_tbl = &st->files;
    vm_native_decls_at_ip(bc, *ip, &ctx.doc_decls, &ctx.doc_decl_count);
    ios = FLAT_IO_ERR;
    {
        int invoke_rc = flat_native_invoke(native_id, &ctx);
        rc = vm_line_mod_outcome(st, bc, ip, native_id, mod, or_target, invoke_rc, ios, trace);
        if (rc != 0) {
            goto done;
        }
        if (invoke_rc == 0 && ios == FLAT_IO_OK && bind && bind[0] != '\0') {
            set_var_value(st, bind, &result);
        }
        rc = 0;
    }
done:
    free(vals);
    free(names);
    return rc;
}

static int apply_io_legacy(VmState *st, const FlatBytecode *bc, uint32_t *ip, const char *payload,
                           int trace) {
    const char *line = payload;
    const char *bind = line + strlen(line) + 1;
    uint16_t id;
    char inner[4096];

    if (flat_native_split_call(line, &id, inner, sizeof inner) != 0) {
        return 0;
    }
    return run_native_call(st, bc, ip, id, inner, bind, FLAT_LINE_MOD_NONE, 0, trace);
}

static int apply_native(VmState *st, const FlatBytecode *bc, uint32_t *ip, const uint8_t *payload,
                        uint8_t payload_size, int trace) {
    uint16_t id;
    const char *inner;
    const char *bind;
    size_t base_end;
    uint8_t mod = FLAT_LINE_MOD_NONE;
    uint32_t or_target = 0;

    if (payload_size < 3) {
        return 0;
    }
    id = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
    inner = (const char *)(payload + 2);
    bind = inner + strlen(inner) + 1;
    if ((size_t)(bind - (const char *)payload) + strlen(bind) + 1 > payload_size) {
        return 0;
    }
    base_end = (size_t)(bind - (const char *)payload) + strlen(bind) + 1;
    if (payload_size >= base_end + 1) {
        mod = payload[base_end];
        if (payload_size >= base_end + 5) {
            memcpy(&or_target, payload + base_end + 1, 4);
        }
    }
    return run_native_call(st, bc, ip, id, inner, bind, mod, or_target, trace);
}

int flat_vm_run(const FlatBytecode *bc) {
    return flat_vm_run_opts(bc, NULL);
}

int flat_vm_run_opts(const FlatBytecode *bc, const FlatVmOptions *opts) {
    VmState st;
    uint32_t ip;
    size_t entry_index = 0;
    char (*guard_names)[64] = NULL;
    FlatValue *guard_vals = NULL;
    size_t scratch_n;
    int trace = opts && opts->trace;
    int trace_vars = opts && opts->trace_vars;

    if (!bc || !bc->data || bc->block_count == 0) {
        if (opts && opts->out_termination) {
            *opts->out_termination = (int)FLAT_TERM_ERROR;
        }
        return 1;
    }
    flat_native_runtime_reset();
    flat_date_run_begin(flat_date_global_state());
    flat_native_runtime_set_manifest(bc->optional_natives, bc->optional_native_count);
    memset(&st, 0, sizeof st);
    st.run_opts = opts;
    st.bc = bc;
    st.var_slots_max = bc->var_slots;
    scratch_n = st.var_slots_max > 0 ? st.var_slots_max : 1u;
    st.vars = calloc(scratch_n, sizeof *st.vars);
    st.pool = flat_pool_create();
    st.coll = flat_coll_create();
    flat_mem_stats_init(&st.live);
    if (!st.vars || !st.pool || !st.coll) {
        return vm_fail_setup(&st, opts);
    }
    flat_coll_set_live_stats(st.coll, &st.live);
    flat_file_table_init(&st.files);
    st.io_ctx = flat_io_ctx_create(&st.files);
    if (!st.io_ctx) {
        return vm_fail_setup(&st, opts);
    }
    guard_names = calloc(scratch_n, 64);
    guard_vals = calloc(scratch_n, sizeof *guard_vals);
    if (!guard_names || !guard_vals) {
        free(guard_names);
        free(guard_vals);
        return vm_fail_setup(&st, opts);
    }
    if (opts && opts->out_termination) {
        *opts->out_termination = (int)FLAT_TERM_ERROR;
    }

    for (entry_index = 0; entry_index < bc->block_count; entry_index++) {
        if (bc->blocks[entry_index].block_id == FLAT_FB_BLOCK_ID_MAIN) {
            break;
        }
    }
    if (entry_index >= bc->block_count) {
        entry_index = 0;
    }
    ip = bc->blocks[entry_index].offset;

    for (;;) {
        uint8_t opcode;
        uint8_t payload_size;
        const uint8_t *payload;

        if (opts && opts->max_steps > 0) {
            st.step_count++;
            if (st.step_count > opts->max_steps) {
                if (vm_jump_panic_any(&st, bc, &ip, "vm.step_limit", trace) != 0) {
                    return vm_finish(&st, (char *)guard_names, guard_vals, 1, FLAT_TERM_PANIC,
                                     opts);
                }
                continue;
            }
        }

        if ((size_t)ip + 2 > bc->code_size) {
            return vm_finish(&st, (char *)guard_names, guard_vals, 0, FLAT_TERM_COMPLETE, opts);
        }
        opcode = bc->data[ip];
        payload_size = bc->data[ip + 1];
        if ((size_t)ip + 2 + payload_size > bc->code_size) {
            return vm_finish(&st, (char *)guard_names, guard_vals, 1, vm_term_for_exit1(&st),
                             opts);
        }
        payload = &bc->data[ip + 2];
        flat_pool_reap_idle(st.pool);
        flat_coll_reap_idle(st.coll, st.pool);

        if (trace || trace_vars) {
            fprintf(stderr, "[trace] ip=%u op=%s(0x%02x) len=%u", ip, opcode_name(opcode), opcode,
                    (unsigned)payload_size);
            if (opcode == OP_ASSIGN) {
                const char *pt = (const char *)payload;
                if (payload_size > 1) {
                    pt++;
                }
                fprintf(stderr, " %s", pt);
            } else if (opcode == OP_IO) {
                fprintf(stderr, " %s", (const char *)payload);
            } else if (opcode == OP_NATIVE && payload_size >= 2) {
                uint16_t nid = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
                fprintf(stderr, " %s", flat_native_name(nid));
            } else if (opcode == OP_GUARD && payload_size > 4) {
                fprintf(stderr, " %s", (const char *)(payload + 4));
            }
            fputc('\n', stderr);
        }
        if (trace_vars) {
            trace_vars_dump(&st);
        }

        switch (opcode) {
        case OP_OK:
            return vm_finish(&st, (char *)guard_names, guard_vals, 0, FLAT_TERM_OK, opts);
        case OP_FAIL:
            return vm_finish(&st, (char *)guard_names, guard_vals, 2, FLAT_TERM_FAIL, opts);
        case OP_RETURN:
            if (st.call_sp > 0) {
                ip = st.calls[--st.call_sp].return_ip;
                continue;
            }
            if (st.loop_sp > 0) {
                ForeachFrame *fr = &st.loops[st.loop_sp - 1];
                fr->index++;
                if (fr->index < fr->item_count) {
                    if (fr->is_for) {
                        set_var_from_number(&st, fr->item_var, (double)fr->index);
                    } else if (fr->use_list) {
                        FlatValue el;
                        if (flat_list_get(st.coll, fr->list_id, fr->index, &el) == 0) {
                            set_var_value(&st, fr->item_var, &el);
                        }
                    } else {
                        set_var_from_cstr(&st, fr->item_var, fr->items[fr->index]);
                    }
                    ip = fr->loop_block_ip;
                    continue;
                }
                foreach_frame_end(&st, fr);
                st.loop_sp--;
                ip = fr->resume_ip;
                continue;
            }
            return vm_finish(&st, (char *)guard_names, guard_vals, 0, FLAT_TERM_COMPLETE, opts);
        case OP_ASSIGN: {
            uint32_t ip_before = ip;
            int ar = apply_assign(&st, bc, &ip, payload, payload_size, trace);
            if (ar == 2) {
                return vm_finish(&st, (char *)guard_names, guard_vals, 2, FLAT_TERM_FAIL, opts);
            }
            if (ar != 0) {
                return vm_finish(&st, (char *)guard_names, guard_vals, 1, vm_term_for_exit1(&st),
                                 opts);
            }
            if (ip == ip_before) {
                ip += 2u + payload_size;
            }
            continue;
        }
        case OP_FREE: {
            apply_free(&st, (const char *)payload);
            ip += 2u + payload_size;
            continue;
        }
        case OP_IO: {
            uint32_t ip_before = ip;
            if (apply_io_legacy(&st, bc, &ip, (const char *)payload, trace) != 0) {
                return vm_finish(&st, (char *)guard_names, guard_vals, 1, vm_term_for_exit1(&st),
                                 opts);
            }
            if (ip == ip_before) {
                ip += 2u + payload_size;
            }
            continue;
        }
        case OP_NATIVE: {
            uint32_t ip_before = ip;
            int nr = apply_native(&st, bc, &ip, payload, payload_size, trace);
            if (nr == 2) {
                return vm_finish(&st, (char *)guard_names, guard_vals, 2, FLAT_TERM_FAIL, opts);
            }
            if (nr != 0) {
                return vm_finish(&st, (char *)guard_names, guard_vals, 1, vm_term_for_exit1(&st),
                                 opts);
            }
            if (ip == ip_before) {
                ip += 2u + payload_size;
            }
            continue;
        }
        case OP_GUARD: {
            uint32_t bt = 0;
            const char *expr;
            int gi;
            var_names_table(&st, guard_names);
            memcpy(&bt, payload, 4);
            expr = (const char *)(payload + 4);
            for (gi = 0; gi < st.var_count; gi++) {
                guard_vals[gi] = st.vars[gi].val;
            }
            if (!flat_value_eval_guard(expr, guard_vals, st.var_count, guard_names, st.pool)) {
                if (bt == 0) {
                    return vm_finish(&st, (char *)guard_names, guard_vals, 2, FLAT_TERM_FAIL,
                                     opts);
                }
                ip = bt;
                continue;
            }
            ip += 2u + payload_size;
            continue;
        }
        case OP_CALL: {
            uint32_t target = 0;
            memcpy(&target, payload, 4);
            if (st.call_sp >= VM_MAX_CALL) {
                return vm_finish(&st, (char *)guard_names, guard_vals, 1, FLAT_TERM_ERROR, opts);
            }
            st.via_jump = 0;
            st.calls[st.call_sp++].return_ip = ip + 2u + payload_size;
            ip = target;
            continue;
        }
        case OP_JUMP: {
            uint32_t target = 0;
            uint64_t jump_max = vm_effective_max_jumps(opts);
            if (payload_size < 4) {
                return vm_finish(&st, (char *)guard_names, guard_vals, 1, FLAT_TERM_ERROR, opts);
            }
            st.jump_count++;
            if (st.jump_count > jump_max) {
                if (vm_jump_panic_any(&st, bc, &ip, "vm.jump_limit", trace) != 0) {
                    return vm_finish(&st, (char *)guard_names, guard_vals, 1, FLAT_TERM_PANIC,
                                     opts);
                }
                continue;
            }
            memcpy(&target, payload, 4);
            st.loop_sp = 0;
            st.via_jump = 1;
            ip = target;
            continue;
        }
        case OP_FOREACH: {
            uint32_t target = 0;
            memcpy(&target, payload, 4);
            setup_foreach(&st, (const char *)(payload + 4), ip + 2u + payload_size, target);
            if (st.loop_sp > 0 && st.loops[st.loop_sp - 1].item_count > 0) {
                ip = target;
            } else {
                ip += 2u + payload_size;
            }
            continue;
        }
        case OP_FOR: {
            uint32_t target = 0;
            int sr;
            memcpy(&target, payload, 4);
            sr = setup_for(&st, bc, &ip, (const char *)(payload + 4), ip + 2u + payload_size,
                           target, trace);
            if (sr == 0) {
                return vm_finish(&st, (char *)guard_names, guard_vals, 1, FLAT_TERM_PANIC, opts);
            }
            if (sr == 2) {
                continue;
            }
            if (st.loop_sp > 0 && st.loops[st.loop_sp - 1].item_count > 0) {
                ip = target;
            } else {
                ip += 2u + payload_size;
            }
            continue;
        }
        case OP_PANIC: {
            uint32_t target = 0;
            if (payload_size >= 4) {
                memcpy(&target, payload, 4);
            }
            if (target == 0) {
                return vm_finish(&st, (char *)guard_names, guard_vals, 1, FLAT_TERM_ERROR, opts);
            }
            ip = target;
            continue;
        }
        default:
            snprintf(st.panic_reason, sizeof st.panic_reason, "unsupported opcode 0x%02x", opcode);
            {
                char line[96];
                snprintf(line, sizeof line, "flvm: %s at ip %u\n", st.panic_reason, ip);
                vm_write_stderr(line);
            }
            return vm_finish(&st, (char *)guard_names, guard_vals, 1, FLAT_TERM_ERROR, opts);
        }
    }
}
