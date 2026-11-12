#include "vm.h"
#include "fgir.h"
#include "flat_func.h"
#include "flat_date.h"
#include "flat_file.h"
#include "flat_io_ctx.h"
#include "flat_native_plugin.h"
#include "flat.h"
#include "flat_mem_stats.h"
#include "flat_native.h"
#include "flat_str.h"
#include "flat_number.h"
#include "flat_value.h"
#include "flat_vcx.h"
#include "flat_slot_type.h"
#include "flat_obj_path.h"
#include "flat_schema.h"

#ifdef FLAT_WASM_BUILD
#include "flat_wasm_io.h"
#endif

#include "flat_port.h"

#include <ctype.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VM_MAX_CALL 64
#define VM_MAX_FOREACH 8

static int vm_block_index(const FlatBytecode *bc, uint32_t ip);
static int vm_ip_for_block_id(const FlatBytecode *bc, uint32_t block_id, uint32_t *out_ip);
static void vm_native_decls_at_ip(const FlatBytecode *bc, uint32_t ip,
                                    const FlatOptionalNativeDecl **decls_out, int *count_out);

typedef FlatVcxVar NamedVar;

typedef struct {
    uint32_t return_ip;
    uint32_t func_instr_ctx; /* OP_CALL_FUNCTION: instr VCX id; 0 otherwise */
} CallFrame;

typedef struct {
    uint32_t resume_ip;
    uint32_t loop_block_ip;
    int item_count;
    int index;
    char item_var[FLAT_VAR_NAME_MAX + 1];
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
    char caught_panic_reason[64];
    int unhandled_panic;
    FlatFileTable files;
    FlatIOContext *io_ctx;
    int via_jump;
    uint64_t step_count;
    uint64_t jump_count;
    const FlatVmOptions *run_opts;
    FlatVcxRuntime *vcx;
} VmState;

static VmState *g_vm_call_instr;
static uint32_t g_instr_stack[VM_MAX_CALL];
static int g_instr_stack_sp;
static int g_instr_push_call_sp[VM_MAX_CALL];
static int g_last_call_function_sp;

void flat_vm_instr_stack_push(uint32_t id) {
    if (id < 1u || g_instr_stack_sp >= VM_MAX_CALL) {
        return;
    }
    g_instr_push_call_sp[g_instr_stack_sp] =
        g_vm_call_instr ? g_vm_call_instr->call_sp : 0;
    g_instr_stack[g_instr_stack_sp++] = id;
}

int flat_vm_instr_stack_pop(uint32_t id) {
    if (g_instr_stack_sp <= 0 || g_instr_stack[g_instr_stack_sp - 1] != id) {
        return 0;
    }
    g_instr_stack_sp--;
    return 1;
}

uint32_t flat_vm_instr_stack_peek(void) {
    if (g_instr_stack_sp <= 0) {
        return 0u;
    }
    return g_instr_stack[g_instr_stack_sp - 1];
}

void flat_vm_instr_stack_clear(void) {
    g_instr_stack_sp = 0;
    g_last_call_function_sp = 0;
}

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

static void vm_note_caught_panic(VmState *st, const char *reason) {
    if (!st) {
        return;
    }
    if (!reason || reason[0] == '\0') {
        reason = "panic";
    }
    snprintf(st->caught_panic_reason, sizeof st->caught_panic_reason, "%s", reason);
}

static void vm_clear_caught_panic(VmState *st) {
    if (st) {
        st->caught_panic_reason[0] = '\0';
    }
}

static void vm_log_unhandled_panic_stderr(const char *reason) {
    char line[160];
    const char *r;

    r = reason && reason[0] != '\0' ? reason : "panic";
    snprintf(line, sizeof line, "flvm: unhandled PANIC: %s\n", r);
    vm_write_stderr(line);
}

static int vm_jump_panic_any(VmState *st, const FlatBytecode *bc, uint32_t *ip, const char *reason,
                             int trace) {
    size_t i;
    uint32_t want = flat_label_hash("#panic.any");

    vm_note_caught_panic(st, reason);

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
    vm_log_unhandled_panic_stderr(reason);
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
                               const char *panic_detail, int trace) {
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
    if (panic_detail && panic_detail[0] != '\0') {
        snprintf(st->panic_reason, sizeof st->panic_reason, "%s", panic_detail);
    } else {
        snprintf(st->panic_reason, sizeof st->panic_reason, "%s", flat_native_name(native_id));
    }
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

static uint32_t vm_number_to_u32(double d) {
    if (d != d || d < 0.0 || d > (double)UINT32_MAX || floor(d) != d) {
        return 0u;
    }
    return (uint32_t)d;
}

static uint32_t vm_resolve_call_instr_ctx(void) {
    int i;

    if (!g_vm_call_instr) {
        return 0u;
    }
    {
        uint32_t stack_id = flat_vm_instr_stack_peek();
        uint32_t frame_id = 0u;
        int push_sp = 0;

        for (i = g_vm_call_instr->call_sp - 1; i >= 0; i--) {
            if (g_vm_call_instr->calls[i].func_instr_ctx != 0u) {
                frame_id = g_vm_call_instr->calls[i].func_instr_ctx;
                break;
            }
        }
        if (g_instr_stack_sp > 0) {
            push_sp = g_instr_push_call_sp[g_instr_stack_sp - 1];
        }
        if (stack_id >= 1u && frame_id >= 1u && stack_id != frame_id) {
            if (push_sp >= g_last_call_function_sp) {
                return stack_id;
            }
            return frame_id;
        }
        if (frame_id >= 1u) {
            return frame_id;
        }
        if (stack_id >= 1u) {
            return stack_id;
        }
    }
    {
        int vi = find_var(g_vm_call_instr, FLAT_FUNC_RESERVED_INSTR_VAR);
        if (vi >= 0 && g_vm_call_instr->vars[vi].val.tag == VAL_NUMBER) {
            uint32_t id = vm_number_to_u32(flat_number_as_f64(&g_vm_call_instr->vars[vi].val));
            if (id >= 1u) {
                return id;
            }
        }
    }
    if (g_vm_call_instr->vcx) {
        FlatValue v;
        char panic[32];

        if (flat_vcx_get_var(g_vm_call_instr->vcx, 1u, FLAT_FUNC_RESERVED_INSTR_VAR, &v,
                             g_vm_call_instr->coll, g_vm_call_instr->pool, &g_vm_call_instr->files,
                             panic, sizeof panic) == 0 &&
            v.tag == VAL_NUMBER) {
            uint32_t id = vm_number_to_u32(flat_number_as_f64(&v));
            flat_value_release(g_vm_call_instr->coll, g_vm_call_instr->pool, &v);
            if (id >= 1u) {
                return id;
            }
        }
    }
    return 0u;
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

static int slot_types_compatible_for_retype(const FlatVcxVar *slot, const FlatSlotType *lhs_ty,
                                            int lhs_has_ty, int lhs_dynamic,
                                            const char *lhs_dynamic_expr) {
    if (!slot || !slot->has_slot_type || !lhs_has_ty || !lhs_ty) {
        return 1;
    }
    if (slot->slot_dynamic != (uint8_t)(lhs_dynamic ? 1 : 0)) {
        return flat_slot_type_equals(&slot->slot_type, lhs_ty);
    }
    if (slot->slot_dynamic && lhs_dynamic && lhs_dynamic_expr &&
        strcmp(slot->slot_dynamic_expr, lhs_dynamic_expr) != 0) {
        return flat_slot_type_equals(&slot->slot_type, lhs_ty);
    }
    return flat_slot_type_equals(&slot->slot_type, lhs_ty);
}

static int set_var_value(VmState *st, const char *name, const FlatValue *val,
                         const FlatSlotType *lhs_ty, int lhs_has_ty, int lhs_dynamic,
                         const char *lhs_dynamic_expr, int bind_const) {
    FlatSlotType contract;
    FlatValue staged;
    const FlatValue *use;
    int has_contract = 0;
    char chk[64];
    int i = find_var(st, name);
    if (i >= 0) {
        if (st->vars[i].slot_const) {
            snprintf(st->panic_reason, sizeof st->panic_reason, "type.const.assign");
            return -1;
        }
        if (st->vars[i].slot_frozen) {
            snprintf(st->panic_reason, sizeof st->panic_reason, "type.freeze.assign");
            return -1;
        }
        if (bind_const) {
            snprintf(st->panic_reason, sizeof st->panic_reason, "type.const.assign");
            return -1;
        }
        if (st->vars[i].has_slot_type && lhs_has_ty && lhs_ty &&
            !slot_types_compatible_for_retype(&st->vars[i], lhs_ty, lhs_has_ty, lhs_dynamic,
                                              lhs_dynamic_expr)) {
            snprintf(st->panic_reason, sizeof st->panic_reason, "type.assign.retype");
            return -1;
        }
        if (lhs_has_ty && lhs_ty) {
            contract = *lhs_ty;
            has_contract = 1;
        } else if (st->vars[i].has_slot_type) {
            contract = st->vars[i].slot_type;
            has_contract = 1;
        }
        {
            const FlatSchemaCatalog *sc = st->bc ? &st->bc->schemas : NULL;
            staged = *val;
            if (has_contract &&
                flat_slot_type_check_value(&contract, val, st->pool, st->coll, sc, &staged, chk,
                                           sizeof chk) != 0) {
                snprintf(st->panic_reason, sizeof st->panic_reason, "%s", chk);
                return -1;
            }
            use = &staged;
        }
        if (flat_value_same_storage(&st->vars[i].val, use)) {
            if (lhs_has_ty && lhs_ty && !st->vars[i].has_slot_type) {
                st->vars[i].slot_type = *lhs_ty;
                st->vars[i].has_slot_type = 1;
                st->vars[i].slot_dynamic = lhs_dynamic ? 1u : 0u;
                if (lhs_dynamic && lhs_dynamic_expr) {
                    snprintf(st->vars[i].slot_dynamic_expr, sizeof st->vars[i].slot_dynamic_expr,
                             "%s", lhs_dynamic_expr);
                }
            }
            return 0;
        }
        if (st->vars[i].val.tag == VAL_FILE) {
            flat_file_release(&st->files, st->vars[i].val.u.file_id);
        }
        flat_value_retain(st->coll, st->pool, use);
        if (use->tag == VAL_FILE) {
            flat_file_retain(&st->files, use->u.file_id);
        }
        flat_value_live_replace(&st->live, &st->vars[i].val, use);
        flat_value_release(st->coll, st->pool, &st->vars[i].val);
        st->vars[i].val = *use;
        if (lhs_has_ty && lhs_ty) {
            st->vars[i].slot_type = *lhs_ty;
            st->vars[i].has_slot_type = 1;
            st->vars[i].slot_dynamic = lhs_dynamic ? 1u : 0u;
            if (lhs_dynamic && lhs_dynamic_expr) {
                snprintf(st->vars[i].slot_dynamic_expr, sizeof st->vars[i].slot_dynamic_expr, "%s",
                         lhs_dynamic_expr);
            } else {
                st->vars[i].slot_dynamic_expr[0] = '\0';
            }
        }
        if (st->io_ctx) {
            flat_io_ctx_var_updated(st->io_ctx, i, use);
        }
        if (strcmp(name, FLAT_FUNC_RESERVED_INSTR_VAR) == 0 && use->tag == VAL_NUMBER) {
            uint32_t iid = vm_number_to_u32(flat_number_as_f64(use));
            if (iid >= 1u) {
                flat_vm_instr_stack_push(iid);
            }
        }
        return 0;
    }
    if ((uint32_t)st->var_count >= st->var_slots_max) {
        if (!st->vcx ||
            flat_vcx_prepare_active_new_bind(st->vcx, st->panic_reason, sizeof st->panic_reason) !=
                0) {
            if (st->panic_reason[0] == '\0') {
                snprintf(st->panic_reason, sizeof st->panic_reason, "vm.var.slots");
            }
            return -1;
        }
    }
    if (var_name_forbidden(name)) {
        snprintf(st->panic_reason, sizeof st->panic_reason, "vm.var.forbidden");
        return -1;
    }
    if (lhs_has_ty && lhs_ty) {
        contract = *lhs_ty;
        has_contract = 1;
    }
    {
        const FlatSchemaCatalog *sc = st->bc ? &st->bc->schemas : NULL;
        staged = *val;
        if (has_contract &&
            flat_slot_type_check_value(&contract, val, st->pool, st->coll, sc, &staged, chk,
                                       sizeof chk) != 0) {
            snprintf(st->panic_reason, sizeof st->panic_reason, "%s", chk);
            return -1;
        }
        use = &staged;
    }
    flat_value_retain(st->coll, st->pool, use);
    if (use->tag == VAL_FILE) {
        flat_file_retain(&st->files, use->u.file_id);
    }
    flat_value_live_bind(&st->live, use);
    snprintf(st->vars[st->var_count].name, sizeof st->vars[st->var_count].name, "%s", name);
    st->vars[st->var_count].val = *use;
    if (lhs_has_ty && lhs_ty) {
        st->vars[st->var_count].slot_type = *lhs_ty;
        st->vars[st->var_count].has_slot_type = 1;
        st->vars[st->var_count].slot_dynamic = lhs_dynamic ? 1u : 0u;
        if (lhs_dynamic && lhs_dynamic_expr) {
            snprintf(st->vars[st->var_count].slot_dynamic_expr,
                     sizeof st->vars[st->var_count].slot_dynamic_expr, "%s", lhs_dynamic_expr);
        } else {
            st->vars[st->var_count].slot_dynamic_expr[0] = '\0';
        }
    } else {
        flat_slot_type_clear(&st->vars[st->var_count].slot_type);
        st->vars[st->var_count].has_slot_type = 0;
        st->vars[st->var_count].slot_dynamic = 0;
        st->vars[st->var_count].slot_dynamic_expr[0] = '\0';
    }
    if (bind_const) {
        st->vars[st->var_count].slot_const = 1;
    }
    if (st->io_ctx) {
        flat_io_ctx_var_updated(st->io_ctx, st->var_count, use);
    }
    st->var_count++;
    if (strcmp(name, FLAT_FUNC_RESERVED_INSTR_VAR) == 0 && use->tag == VAL_NUMBER) {
        uint32_t iid = vm_number_to_u32(flat_number_as_f64(use));
        if (iid >= 1u) {
            flat_vm_instr_stack_push(iid);
        }
    }
    if (st->vcx) {
        flat_vcx_runtime_sync_bind_var_count(st->vcx);
    }
    return 0;
}

static int apply_panic_reason(VmState *st, const char *bind_name) {
    FlatValue v;

    if (!st || !bind_name || !bind_name[0]) {
        return -1;
    }
    memset(&v, 0, sizeof v);
    if (st->caught_panic_reason[0] == '\0') {
        if (flat_value_set_str_inline(&v, "", 0) != 0) {
            return -1;
        }
    } else {
        size_t len = strlen(st->caught_panic_reason);
        if (len > FLAT_INLINE_STR_MAX) {
            uint32_t id = 0u;
            if (flat_pool_alloc_copy(st->pool, st->caught_panic_reason, len, &id) != 0) {
                return -1;
            }
            flat_value_set_str_ref(&v, id);
        } else if (flat_value_set_str_inline(&v, st->caught_panic_reason, len) != 0) {
            return -1;
        }
    }
    return set_var_value(st, bind_name, &v, NULL, 0, 0, NULL, 0);
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
    flat_slot_type_clear(&st->vars[i].slot_type);
    st->vars[i].has_slot_type = 0;
    st->vars[i].slot_dynamic = 0;
    st->vars[i].slot_dynamic_expr[0] = '\0';
    st->vars[i].slot_frozen = 0;
    st->vars[i].slot_const = 0;
    if (st->io_ctx) {
        flat_io_ctx_var_updated(st->io_ctx, i, &nullv);
    }
    return 0;
}

static int apply_freeze(VmState *st, const char *varname) {
    int i = find_var(st, varname);
    if (i < 0) {
        return 0;
    }
    st->vars[i].slot_frozen = 1;
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
    char *lhs;
    char *rhs;
    char slot_name[FLAT_VAR_NAME_MAX + 1];
    char lhs_dyn_expr[FLAT_VAR_NAME_MAX + 1];
    FlatSlotType lhs_ty;
    int lhs_has_ty = 0;
    int lhs_dynamic = 0;
    FlatValue v;
    FlatIoStatus ios;
    FlatValue *vals = NULL;
    char (*names)[64] = NULL;
    FlatSlotType *slot_types = NULL;
    uint8_t *has_types = NULL;
    uint8_t *frozen_flags = NULL;
    uint8_t *const_flags = NULL;
    size_t scratch_n;
    int i;
    int rc = 0;
    uint8_t mod = FLAT_LINE_MOD_NONE;
    const char *line = NULL;
    uint32_t or_target = 0;
    uint16_t native_id = 0;
    int bind_const = 0;

    if (decode_assign_payload(payload, payload_size, &mod, &line, &or_target) != 0) {
        return -1;
    }

    scratch_n = st->var_slots_max > 0 ? st->var_slots_max : 1u;
    vals = calloc(scratch_n, sizeof *vals);
    names = calloc(scratch_n, 64);
    slot_types = calloc(scratch_n, sizeof *slot_types);
    has_types = calloc(scratch_n, sizeof *has_types);
    frozen_flags = calloc(scratch_n, sizeof *frozen_flags);
    const_flags = calloc(scratch_n, sizeof *const_flags);
    if (!vals || !names || !slot_types || !has_types || !frozen_flags || !const_flags) {
        free(vals);
        free(names);
        free(slot_types);
        free(has_types);
        free(frozen_flags);
        free(const_flags);
        return -1;
    }

    snprintf(buf, sizeof buf, "%s", line);
    {
        FlatAssignSplitForm form;

        bind_const = 0;
        if (flat_assign_strip_const_prefix(buf, &bind_const) != 0) {
            rc = 0;
            goto done;
        }
        if (flat_assign_split_line(buf, &lhs, &rhs, &form) != 0) {
            rc = 0;
            goto done;
        }
        (void)form;
    }
    var_names_table(st, names);
    for (i = 0; i < st->var_count; i++) {
        vals[i] = st->vars[i].val;
        slot_types[i] = st->vars[i].slot_type;
        has_types[i] = (uint8_t)st->vars[i].has_slot_type;
        frozen_flags[i] = st->vars[i].slot_frozen;
        const_flags[i] = st->vars[i].slot_const;
    }
    if (flat_assign_lhs_has_path(lhs)) {
        char path_expr[384];
        char root_bind[192];
        char root_name[FLAT_VAR_NAME_MAX + 1];
        FlatSlotType root_ty;
        int root_has_ty = 0;
        uint32_t root_obj_id = 0;
        FlatPathErr perr = FLAT_PATH_OK;
        FlatPathTypedCtx ptc;
        int root_vi;

        if (flat_assign_path_build_expr(lhs, path_expr, sizeof path_expr, root_bind,
                                        sizeof root_bind) != 0) {
            rc = 0;
            goto done;
        }
        lhs_dyn_expr[0] = '\0';
        st->panic_reason[0] = '\0';
        if (flat_bind_resolve_lhs_runtime(root_bind, root_name, sizeof root_name, &root_ty,
                                          &root_has_ty, &lhs_dynamic, vals, st->var_count, names,
                                          st->pool, st->coll, bc ? &bc->schemas : NULL, st->vcx,
                                          st->panic_reason, sizeof st->panic_reason) != 0) {
            rc = vm_jump_panic_any(st, bc, ip, st->panic_reason, trace);
            goto done;
        }
        if (lhs_dynamic) {
            FlatSlotType raw;
            int has_raw;

            if (flat_bind_parse_lhs_catalog(root_bind, root_name, sizeof root_name, &raw, &has_raw,
                                            bc ? &bc->schemas : NULL) == 0 && has_raw) {
                snprintf(lhs_dyn_expr, sizeof lhs_dyn_expr, "%s", raw.dynamic_expr);
            }
        }
        root_vi = find_var(st, root_name);
        if (root_vi < 0) {
            FlatValue seed;

            if (!root_has_ty) {
                rc = 0;
                goto done;
            }
            memset(&seed, 0, sizeof seed);
            if (flat_value_parse_assign_rhs("{}", &seed, st->pool, st->coll, vals, st->var_count,
                                            names) != 0) {
                rc = 0;
                goto done;
            }
            if (set_var_value(st, root_name, &seed, &root_ty, 1, lhs_dynamic,
                              lhs_dynamic ? lhs_dyn_expr : NULL, bind_const) != 0) {
                rc = vm_jump_panic_any(st, bc, ip, st->panic_reason, trace);
                goto done;
            }
            root_vi = find_var(st, root_name);
            vals[root_vi] = st->vars[root_vi].val;
            slot_types[root_vi] = st->vars[root_vi].slot_type;
            has_types[root_vi] = (uint8_t)st->vars[root_vi].has_slot_type;
            snprintf(names[root_vi], 64, "%s", root_name);
        }
        if (rhs_is_native_call(rhs)) {
            rc = 0;
            goto done;
        }
        if (flat_value_parse_assign_rhs(rhs, &v, st->pool, st->coll, vals, st->var_count, names) !=
            0) {
            rc = 0;
            goto done;
        }
        if (root_vi >= 0) {
            if (st->vars[root_vi].slot_const) {
                snprintf(st->panic_reason, sizeof st->panic_reason, "type.const.assign");
                rc = vm_jump_panic_any(st, bc, ip, st->panic_reason, trace);
                goto done;
            }
            if (st->vars[root_vi].slot_frozen) {
                snprintf(st->panic_reason, sizeof st->panic_reason, "type.freeze.assign");
                rc = vm_jump_panic_any(st, bc, ip, st->panic_reason, trace);
                goto done;
            }
        }
        memset(&ptc, 0, sizeof ptc);
        ptc.schemas = bc ? &bc->schemas : NULL;
        ptc.var_slot_types = slot_types;
        ptc.var_has_slot_type = has_types;
        ptc.type_panic = st->panic_reason;
        ptc.type_panic_sz = sizeof st->panic_reason;
        st->panic_reason[0] = '\0';
        if (flat_path_set2(st->coll, st->pool, vals, st->var_count, names, path_expr, &v,
                           &root_obj_id, &perr, &ptc) != 0) {
            if (st->panic_reason[0] != '\0') {
                rc = vm_jump_panic_any(st, bc, ip, st->panic_reason, trace);
            } else {
                rc = 0;
            }
            goto done;
        }
        {
            FlatValue nv;

            flat_value_set_object(&nv, root_obj_id);
            flat_value_retain(st->coll, st->pool, &nv);
            flat_value_live_replace(&st->live, &st->vars[root_vi].val, &nv);
            flat_value_release(st->coll, st->pool, &st->vars[root_vi].val);
            st->vars[root_vi].val = nv;
            if (st->io_ctx) {
                flat_io_ctx_var_updated(st->io_ctx, root_vi, &nv);
            }
        }
        rc = 0;
        goto done;
    }
    lhs_dyn_expr[0] = '\0';
    st->panic_reason[0] = '\0';
    if (flat_bind_resolve_lhs_runtime(lhs, slot_name, sizeof slot_name, &lhs_ty, &lhs_has_ty,
                                      &lhs_dynamic, vals, st->var_count, names, st->pool, st->coll,
                                      bc ? &bc->schemas : NULL, st->vcx, st->panic_reason,
                                      sizeof st->panic_reason) != 0) {
        rc = vm_jump_panic_any(st, bc, ip, st->panic_reason, trace);
        goto done;
    }
    if (lhs_dynamic) {
        FlatSlotType raw;
        int has_raw;

        if (flat_bind_parse_lhs_catalog(lhs, slot_name, sizeof slot_name, &raw, &has_raw,
                                        bc ? &bc->schemas : NULL) == 0 && has_raw) {
            snprintf(lhs_dyn_expr, sizeof lhs_dyn_expr, "%s", raw.dynamic_expr);
        }
    }
    if (var_name_forbidden(slot_name)) {
        rc = 0;
        goto done;
    }
    if (rhs_is_native_call(rhs)) {
        const FlatOptionalNativeDecl *ndecl = NULL;
        int ndecl_count = 0;
        char inner[4096];
        int invoke_rc;
        FlatNativeCtx ctx;

        memset(&ctx, 0, sizeof ctx);
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
            ctx.vcx = st->vcx;
            ctx.var_slot_types = slot_types;
            ctx.var_has_slot_type = has_types;
            ctx.var_slot_frozen = frozen_flags;
            ctx.var_slot_const = const_flags;
            vm_native_decls_at_ip(bc, *ip, &ctx.doc_decls, &ctx.doc_decl_count);
            invoke_rc = flat_native_invoke(native_id, &ctx);
        }
        rc = vm_line_mod_outcome(st, bc, ip, native_id, mod, or_target, invoke_rc, ios,
                                 ctx.panic_detail, trace);
        if (rc != 0) {
            goto done;
        }
        if (invoke_rc == 0 && ios == FLAT_IO_OK) {
            if (set_var_value(st, slot_name, &v, lhs_has_ty ? &lhs_ty : NULL, lhs_has_ty,
                              lhs_dynamic, lhs_dynamic ? lhs_dyn_expr : NULL, bind_const) != 0) {
                rc = vm_jump_panic_any(st, bc, ip, st->panic_reason, trace);
            } else {
                rc = 0;
            }
        } else {
            rc = 0;
        }
        goto done;
    }
    if (flat_value_parse_assign_rhs(rhs, &v, st->pool, st->coll, vals, st->var_count, names) != 0) {
        rc = 0;
        goto done;
    }
    if (set_var_value(st, slot_name, &v, lhs_has_ty ? &lhs_ty : NULL, lhs_has_ty, lhs_dynamic,
                      lhs_dynamic ? lhs_dyn_expr : NULL, bind_const) != 0) {
        rc = vm_jump_panic_any(st, bc, ip, st->panic_reason, trace);
    } else {
        rc = 0;
    }
done:
    free(vals);
    free(names);
    free(slot_types);
    free(has_types);
    free(frozen_flags);
    free(const_flags);
    return rc;
}

static void set_var_from_number(VmState *st, const char *name, double n) {
    FlatValue v;
    if (flat_value_set_number(&v, n) == 0) {
        set_var_value(st, name, &v, NULL, 0, 0, NULL, 0);
    }
}

static int for_limit_from_double(double d, int *out_iters) {
    if (isnan(d) || isinf(d)) {
        return -2;
    }
    if (d < 0.0) {
        return -3;
    }
    if (d == 0.0) {
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
        int fl = for_limit_from_double(d, out_iters);
        if (fl == -3) {
            return -3;
        }
        if (fl != 0) {
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
    {
        int fl = for_limit_from_double(flat_number_as_f64(&st->vars[vi].val), out_iters);
        if (fl == -3) {
            return -3;
        }
        if (fl != 0) {
            return -2;
        }
    }
    return 0;
}

static int vm_resolve_loop_block_ip(VmState *st, const FlatBytecode *bc, const char *third_tok,
                                    uint32_t *out_ip);
static int vm_loop_line_third_block(const char *line, char *out_tok, size_t out_sz);

/* Returns: 1 = loop pushed; 2 = routed to #panic.any (ip updated); 0 = unhandled panic. */
static int setup_for(VmState *st, const FlatBytecode *bc, uint32_t *ip, const char *line,
                     uint32_t resume_ip, uint32_t block_off, int trace) {
    char limit_tok[64] = "";
    char item[64] = "";
    char block_tok[FGIR_MAX_LABEL] = "";
    const char *lp = strchr(line, '(');
    const char *rp = strrchr(line, ')');
    ForeachFrame *fr;
    int iters = 0;
    int ev;
    uint32_t loop_ip = block_off;
    int br;

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
    if (vm_loop_line_third_block(line, block_tok, sizeof block_tok) == 0) {
        br = vm_resolve_loop_block_ip(st, bc, block_tok, &loop_ip);
        if (br != 0) {
            if (vm_jump_panic_any(st, bc, ip, "for.loop.block", trace) == 0) {
                return 2;
            }
            return 0;
        }
    }
    ev = eval_for_ntimes(st, limit_tok, &iters);
    if (ev != 0) {
        const char *preason = (ev == -3) ? "for.ntimes.negative" : "for.ntimes_type";
        if (vm_jump_panic_any(st, bc, ip, preason, trace) == 0) {
            return 2;
        }
        return 0;
    }
    if (st->loop_sp >= VM_MAX_FOREACH) {
        return 0;
    }
    fr = &st->loops[st->loop_sp++];
    fr->resume_ip = resume_ip;
    fr->loop_block_ip = loop_ip;
    snprintf(fr->item_var, sizeof fr->item_var, "%s", item);
    fr->is_for = 1;
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

/* Returns: 1 = frame pushed (maybe 0 items); 2 = routed to #panic.any; 0 = unhandled panic. */
static int setup_foreach(VmState *st, const FlatBytecode *bc, uint32_t *ip, const char *line,
                          uint32_t resume_ip, uint32_t block_off, int trace) {
    char coll_expr[512] = "";
    char item[64] = "";
    char block_tok[FGIR_MAX_LABEL] = "";
    const char *lp = strchr(line, '(');
    const char *rp = strrchr(line, ')');
    ForeachFrame *fr;
    FlatValue cv;
    FlatValue *vals = NULL;
    char (*names)[64] = NULL;
    int i;
    uint32_t loop_ip = block_off;
    int br;

    if (lp && rp && rp > lp) {
        char inner[768];
        char *coll_t;
        char *item_t;
        char *block_t;
        snprintf(inner, sizeof inner, "%.*s", (int)(rp - lp - 1), lp + 1);
        if (flat_str_split_three(inner, &coll_t, &item_t, &block_t) == 0) {
            snprintf(coll_expr, sizeof coll_expr, "%s", trim(coll_t));
            snprintf(item, sizeof item, "%s", trim(item_t));
        }
    }
    if (vm_loop_line_third_block(line, block_tok, sizeof block_tok) == 0) {
        br = vm_resolve_loop_block_ip(st, bc, block_tok, &loop_ip);
        if (br != 0) {
            if (vm_jump_panic_any(st, bc, ip, "foreach.loop.block", trace) == 0) {
                return 2;
            }
            return 0;
        }
    }
    if (st->loop_sp >= VM_MAX_FOREACH) {
        return 0;
    }
    vals = calloc((size_t)st->var_count, sizeof(FlatValue));
    names = calloc((size_t)st->var_count, 64);
    if (!vals || !names) {
        free(vals);
        free(names);
        return 0;
    }
    var_names_table(st, names);
    for (i = 0; i < st->var_count; i++) {
        vals[i] = st->vars[i].val;
    }
    if (flat_value_parse_assign_rhs(coll_expr, &cv, st->pool, st->coll, vals, st->var_count,
                                    names) != 0 ||
        cv.tag != VAL_LIST) {
        free(vals);
        free(names);
        if (vm_jump_panic_any(st, bc, ip, "foreach.collection", trace) == 0) {
            return 2;
        }
        return 0;
    }
    free(vals);
    free(names);

    fr = &st->loops[st->loop_sp++];
    fr->resume_ip = resume_ip;
    fr->loop_block_ip = loop_ip;
    snprintf(fr->item_var, sizeof fr->item_var, "%s", item);
    fr->is_for = 0;
    fr->list_id = cv.u.coll_id;
    fr->list_retained = 0;
    fr->item_count = 0;
    if (flat_list_len(st->coll, fr->list_id, &fr->item_count) != 0) {
        fr->item_count = 0;
    }
    if (fr->item_count > 0) {
        FlatValue lv;
        flat_value_set_list(&lv, fr->list_id);
        flat_value_retain(st->coll, st->pool, &lv);
        fr->list_retained = 1;
    }
    fr->index = 0;
    if (fr->item_count > 0) {
        FlatValue el;
        if (flat_list_get(st->coll, fr->list_id, 0, &el) == 0) {
            set_var_value(st, fr->item_var, &el, NULL, 0, 0, NULL, 0);
        }
    } else {
        foreach_frame_end(st, fr);
        st->loop_sp--;
    }
    return 1;
}

static void vm_teardown(VmState *st, int rc) {
    (void)rc;
    if (g_vm_call_instr == st) {
        g_vm_call_instr = NULL;
        flat_vcx_set_call_instr_resolver(NULL);
        flat_vm_instr_stack_clear();
    }
    if (st->vcx) {
        flat_vcx_runtime_destroy(st->vcx, st->coll, st->pool, &st->files);
        st->vcx = NULL;
        st->vars = NULL;
        st->var_count = 0;
    }
    flat_vcx_runtime_set_active(NULL);
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
    case OP_CALL_BLOCK:
        return "CALL_BLOCK";
    case OP_RETURN:
        return "RETURN";
    case OP_NEXT:
        return "NEXT";
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
    case OP_PANIC_RESET:
        return "PANIC_RESET";
    case OP_PANIC_REASON:
        return "PANIC_REASON";
    case OP_JUMP:
        return "JUMP";
    case OP_CALL_FUNCTION:
        return "CALL_FUNCTION";
    default:
        return "?";
    }
}

static int vm_get_var_copy(VmState *st, const char *name, FlatValue *out) {
    int i = find_var(st, name);
    if (i < 0) {
        return -1;
    }
    *out = st->vars[i].val;
    flat_value_retain(st->coll, st->pool, out);
    return 0;
}

static int vm_apply_call_function(VmState *st, const FlatBytecode *bc, const uint8_t *payload,
                                    uint8_t psz, uint32_t *ip, int trace) {
    uint16_t fi;
    uint8_t argc;
    uint8_t is_global;
    const char *callee_ret;
    const char *formal;
    size_t pos = 0;
    const FlatFbFunctionEntry *fn;
    uint32_t calle_id;
    uint32_t instr_id;
    uint32_t exec_ip;
    char panic[64];
    FlatValue nv;
    int i;

    (void)trace;
    if (!st->vcx || !bc->functions || bc->function_count == 0) {
        return -1;
    }
    if (psz < 4) {
        return -1;
    }
    fi = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
    argc = payload[2];
    is_global = payload[3];
    pos = 4;
    if (fi >= bc->function_count) {
        return -1;
    }
    fn = &bc->functions[fi];
    if (argc != fn->param_count) {
        return -1;
    }
    for (i = 0; i < (int)argc; i++) {
        if (fn->param_has_type[i] && fn->param_type_spec[i][0] != '\0') {
            char tmp[24];
            FlatValue argv;
            FlatSlotType contract;
            FlatValue checked;
            char chk[64];

            snprintf(tmp, sizeof tmp, "__fl_tmp_%d", i);
            if (vm_get_var_copy(st, tmp, &argv) != 0) {
                return -1;
            }
            flat_slot_type_clear(&contract);
            if (flat_slot_type_parse_spec(fn->param_type_spec[i], &contract, &bc->schemas, panic,
                                          sizeof panic) != 0) {
                flat_value_release(st->coll, st->pool, &argv);
                return -1;
            }
            checked = argv;
            if (flat_slot_type_check_value(&contract, &argv, st->pool, st->coll, &bc->schemas,
                                           &checked, chk, sizeof chk) != 0) {
                flat_value_release(st->coll, st->pool, &argv);
                snprintf(st->panic_reason, sizeof st->panic_reason, "%s",
                         chk[0] ? chk : "type.call.arg");
                return vm_jump_panic_any(st, bc, ip, st->panic_reason, trace);
            }
            flat_value_release(st->coll, st->pool, &argv);
        }
    }
    callee_ret = (const char *)(payload + pos);
    pos += strlen(callee_ret) + 1;
    if (pos >= psz) {
        return -1;
    }
    (void)is_global;

    calle_id = flat_vcx_current_id(st->vcx);
    if (flat_vcx_create(st->vcx, FLAT_FUNC_INSTR_VCX_FIXED + (uint32_t)argc, &instr_id, panic,
                        sizeof panic) != 0) {
        return -1;
    }
    flat_value_set_number(&nv, (double)fn->impl_block_id);
    if (flat_vcx_set_named_var(st->vcx, instr_id, "targetBlock", &nv, st->coll, st->pool,
                                  &st->files, &st->live, panic, sizeof panic) != 0) {
        flat_value_release(st->coll, st->pool, &nv);
        return -1;
    }
    flat_value_release(st->coll, st->pool, &nv);
    flat_value_set_number(&nv, (double)calle_id);
    if (flat_vcx_set_named_var(st->vcx, instr_id, "calleVcx", &nv, st->coll, st->pool, &st->files,
                                  &st->live, panic, sizeof panic) != 0) {
        flat_value_release(st->coll, st->pool, &nv);
        return -1;
    }
    flat_value_release(st->coll, st->pool, &nv);
    flat_value_set_str_inline(&nv, fn->return_name, strlen(fn->return_name));
    if (flat_vcx_set_named_var(st->vcx, instr_id, "returnVar", &nv, st->coll, st->pool, &st->files,
                                  &st->live, panic, sizeof panic) != 0) {
        return -1;
    }
    flat_value_set_str_inline(&nv, callee_ret, strlen(callee_ret));
    if (flat_vcx_set_named_var(st->vcx, instr_id, "calleReturnVar", &nv, st->coll, st->pool,
                                  &st->files, &st->live, panic, sizeof panic) != 0) {
        return -1;
    }
    flat_value_set_number(&nv, (double)fn->func_vcx_slots);
    if (flat_vcx_set_named_var(st->vcx, instr_id, "funcVcxSlots", &nv, st->coll, st->pool,
                                  &st->files, &st->live, panic, sizeof panic) != 0) {
        flat_value_release(st->coll, st->pool, &nv);
        return -1;
    }
    flat_value_release(st->coll, st->pool, &nv);
    flat_value_set_number(&nv, (double)fi);
    if (flat_vcx_set_named_var(st->vcx, instr_id, "__fl_funcIndex", &nv, st->coll, st->pool,
                                  &st->files, &st->live, panic, sizeof panic) != 0) {
        flat_value_release(st->coll, st->pool, &nv);
        return -1;
    }
    flat_value_release(st->coll, st->pool, &nv);

    {
        FlatValue items[FGIR_MAX_FUNC_PARAMS * 2];
        FlatValue listv;
        uint32_t list_id;
        int nformal = fn->param_count;
        formal = (const char *)(payload + pos);
        for (i = 0; i < nformal; i++) {
            flat_value_set_str_inline(&items[i], formal, strlen(formal));
            formal += strlen(formal) + 1;
        }
        if (flat_list_build(st->coll, st->pool, items, nformal, &list_id) != 0) {
            return -1;
        }
        flat_value_set_list(&listv, list_id);
        if (flat_vcx_set_named_var(st->vcx, instr_id, "funcParamNames", &listv, st->coll, st->pool,
                                      &st->files, &st->live, panic, sizeof panic) != 0) {
            return -1;
        }
        for (i = 0; i < nformal; i++) {
            char pin[24];
            snprintf(pin, sizeof pin, "param_in_%d", i);
            flat_value_set_str_inline(&items[i], pin, strlen(pin));
        }
        if (flat_list_build(st->coll, st->pool, items, nformal, &list_id) != 0) {
            return -1;
        }
        flat_value_set_list(&listv, list_id);
        if (flat_vcx_set_named_var(st->vcx, instr_id, "paramSrcNames", &listv, st->coll, st->pool,
                                      &st->files, &st->live, panic, sizeof panic) != 0) {
            return -1;
        }
    }

    for (i = 0; i < (int)argc; i++) {
        char tmp[24];
        char pin[24];
        FlatValue argv;
        snprintf(tmp, sizeof tmp, "__fl_tmp_%d", i);
        snprintf(pin, sizeof pin, "param_in_%d", i);
        if (vm_get_var_copy(st, tmp, &argv) != 0) {
            return -1;
        }
        if (flat_vcx_set_named_var(st->vcx, instr_id, pin, &argv, st->coll, st->pool, &st->files,
                                      &st->live, panic, sizeof panic) != 0) {
            flat_value_release(st->coll, st->pool, &argv);
            return -1;
        }
        flat_value_release(st->coll, st->pool, &argv);
    }

    if (vm_ip_for_block_id(bc, fn->executor_block_id, &exec_ip) != 0) {
        return -1;
    }
    if (st->call_sp >= VM_MAX_CALL) {
        return -1;
    }
    st->calls[st->call_sp].return_ip = *ip + 2u + psz;
    st->calls[st->call_sp].func_instr_ctx = instr_id;
    st->call_sp++;
    g_last_call_function_sp = st->call_sp;
    *ip = exec_ip;
    return 0;
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

static int vm_number_to_u32_block_id(double d, uint32_t *out) {
    if (!out || d != d || d < 0.0 || d > (double)UINT32_MAX) {
        return -1;
    }
    if (floor(d) != d) {
        return -1;
    }
    *out = (uint32_t)d;
    return 0;
}

static int vm_ip_for_block_id(const FlatBytecode *bc, uint32_t block_id, uint32_t *out_ip) {
    size_t i;

    if (!bc || !bc->blocks || !out_ip) {
        return -1;
    }
    for (i = 0; i < bc->block_count; i++) {
        if (bc->blocks[i].block_id == block_id) {
            *out_ip = bc->blocks[i].offset;
            return 0;
        }
    }
    return -1;
}

static void vm_strip_block_quotes(char *t) {
    size_t n;

    if (!t || !t[0]) {
        return;
    }
    if (t[0] == '\'' || t[0] == '"') {
        n = strlen(t);
        if (n >= 2 && t[n - 1] == t[0]) {
            t[n - 1] = '\0';
            memmove(t, t + 1, n - 1);
        }
    }
}

static int vm_ip_for_block_label(const FlatBytecode *bc, const char *label, uint32_t *out_ip) {
    size_t i;
    char norm[FGIR_MAX_LABEL];

    if (!bc || !label || !out_ip) {
        return -1;
    }
    snprintf(norm, sizeof norm, "%s", label);
    while (norm[0] == ' ' || norm[0] == '\t') {
        memmove(norm, norm + 1, strlen(norm));
    }
    if (norm[0] != '#') {
        size_t n = strlen(norm);
        if (n + 1 >= sizeof norm) {
            return -1;
        }
        memmove(norm + 1, norm, n + 1);
        norm[0] = '#';
    }
    if (bc->block_labels) {
        for (i = 0; i < bc->block_count; i++) {
            if (strcmp(bc->block_labels[i], norm) == 0) {
                *out_ip = bc->blocks[i].offset;
                return 0;
            }
        }
    }
    {
        uint32_t want = flat_label_hash(norm);
        for (i = 0; i < bc->block_count; i++) {
            if (bc->blocks[i].block_id == want) {
                *out_ip = bc->blocks[i].offset;
                return 0;
            }
        }
    }
    return -1;
}

/* -2 = bad arg; -3 = bad block id number; 0 = ok */
static int vm_resolve_loop_block_ip(VmState *st, const FlatBytecode *bc, const char *third_tok,
                                    uint32_t *out_ip) {
    char tokbuf[FGIR_MAX_LABEL];
    char label[FGIR_MAX_LABEL];
    FlatValue v;
    char (*names)[64] = NULL;
    FlatValue *vals = NULL;
    int i;
    int rc = -1;

    if (!st || !bc || !third_tok || !out_ip) {
        return -1;
    }
    snprintf(tokbuf, sizeof tokbuf, "%s", third_tok);
    vm_strip_block_quotes(tokbuf);
    if (tokbuf[0] == '#') {
        if (vm_ip_for_block_label(bc, tokbuf, out_ip) == 0) {
            return 0;
        }
        return -1;
    }
    names = calloc(st->var_slots_max > 0 ? st->var_slots_max : 1u, 64);
    vals = calloc(st->var_slots_max > 0 ? st->var_slots_max : 1u, sizeof *vals);
    if (!names || !vals) {
        goto done;
    }
    var_names_table(st, names);
    for (i = 0; i < st->var_count; i++) {
        vals[i] = st->vars[i].val;
    }
    if (flat_value_parse_assign_rhs(tokbuf, &v, st->pool, st->coll, vals, st->var_count, names) !=
        0) {
        rc = -2;
        goto done;
    }
    if (v.tag == VAL_NUMBER) {
        uint32_t bid = 0;
        if (vm_number_to_u32_block_id(flat_number_as_f64(&v), &bid) != 0) {
            rc = -3;
            goto done;
        }
        if (vm_ip_for_block_id(bc, bid, out_ip) == 0) {
            rc = 0;
        }
        goto done;
    }
    if (flat_value_get_cstr(&v, st->pool, label, sizeof label) != 0) {
        rc = -2;
        goto done;
    }
    vm_strip_block_quotes(label);
    if (vm_ip_for_block_label(bc, label, out_ip) == 0) {
        rc = 0;
    }
done:
    free(vals);
    free(names);
    return rc;
}

static int vm_loop_line_third_block(const char *line, char *out_tok, size_t out_sz) {
    const char *lp;
    const char *rp;
    char inner[768];
    char *a;
    char *b;
    char *c;

    if (!line || !out_tok || out_sz == 0) {
        return -1;
    }
    out_tok[0] = '\0';
    lp = strchr(line, '(');
    rp = strrchr(line, ')');
    if (!lp || !rp || rp <= lp) {
        return -1;
    }
    snprintf(inner, sizeof inner, "%.*s", (int)(rp - lp - 1), lp + 1);
    if (flat_str_split_three(inner, &a, &b, &c) != 0) {
        return -1;
    }
    (void)a;
    (void)b;
    snprintf(out_tok, out_sz, "%s", trim(c));
    return 0;
}

static int vm_eval_block_id_expr(VmState *st, const char *expr, uint32_t *out_id) {
    FlatValue v;
    char (*names)[64] = NULL;
    FlatValue *vals = NULL;
    int i;
    int rc = -1;

    if (!st || !expr || !out_id) {
        return -1;
    }
    names = calloc(st->var_slots_max > 0 ? st->var_slots_max : 1u, 64);
    vals = calloc(st->var_slots_max > 0 ? st->var_slots_max : 1u, sizeof *vals);
    if (!names || !vals) {
        goto done;
    }
    var_names_table(st, names);
    for (i = 0; i < st->var_count; i++) {
        vals[i] = st->vars[i].val;
    }
    if (flat_value_parse_assign_rhs(expr, &v, st->pool, st->coll, vals, st->var_count, names) != 0) {
        goto done;
    }
    if (v.tag != VAL_NUMBER) {
        rc = -2;
        goto done;
    }
    if (vm_number_to_u32_block_id(flat_number_as_f64(&v), out_id) != 0) {
        rc = -3;
        goto done;
    }
    rc = 0;
done:
    free(vals);
    free(names);
    return rc;
}

static int run_native_call(VmState *st, const FlatBytecode *bc, uint32_t *ip, uint16_t native_id,
                           const char *inner, const char *bind, uint8_t mod, uint32_t or_target,
                           int trace) {
    FlatValue result;
    FlatIoStatus ios;
    FlatNativeCtx ctx;

    memset(&result, 0, sizeof result);
    FlatValue *vals = NULL;
    char (*names)[64] = NULL;
    FlatSlotType *slot_types = NULL;
    uint8_t *has_types = NULL;
    uint8_t *frozen_flags = NULL;
    uint8_t *const_flags = NULL;
    size_t scratch_n;
    int i;
    int rc = 0;

    scratch_n = st->var_slots_max > 0 ? st->var_slots_max : 1u;
    vals = calloc(scratch_n, sizeof *vals);
    names = calloc(scratch_n, 64);
    slot_types = calloc(scratch_n, sizeof *slot_types);
    has_types = calloc(scratch_n, sizeof *has_types);
    frozen_flags = calloc(scratch_n, sizeof *frozen_flags);
    const_flags = calloc(scratch_n, sizeof *const_flags);
    if (!vals || !names || !slot_types || !has_types || !frozen_flags || !const_flags) {
        free(vals);
        free(names);
        free(slot_types);
        free(has_types);
        free(frozen_flags);
        free(const_flags);
        return 0;
    }

    memset(&ctx, 0, sizeof ctx);
    var_names_table(st, names);
    for (i = 0; i < st->var_count; i++) {
        vals[i] = st->vars[i].val;
        slot_types[i] = st->vars[i].slot_type;
        has_types[i] = (uint8_t)st->vars[i].has_slot_type;
        frozen_flags[i] = st->vars[i].slot_frozen;
        const_flags[i] = st->vars[i].slot_const;
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
    ctx.vcx = st->vcx;
    ctx.var_slot_types = slot_types;
    ctx.var_has_slot_type = has_types;
    ctx.var_slot_frozen = frozen_flags;
    ctx.var_slot_const = const_flags;
    ctx.panic_detail[0] = '\0';
    vm_native_decls_at_ip(bc, *ip, &ctx.doc_decls, &ctx.doc_decl_count);
    ios = FLAT_IO_OK;
    {
        int invoke_rc = flat_native_invoke(native_id, &ctx);
        rc = vm_line_mod_outcome(st, bc, ip, native_id, mod, or_target, invoke_rc, ios,
                                 ctx.panic_detail, trace);
        if (rc != 0) {
            goto done;
        }
        if (invoke_rc == 0 && ios == FLAT_IO_OK && bind && bind[0] != '\0') {
            set_var_value(st, bind, &result, NULL, 0, 0, NULL, 0);
        }
        rc = 0;
    }
done:
    free(vals);
    free(names);
    free(slot_types);
    free(has_types);
    free(frozen_flags);
    free(const_flags);
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
    /*
     * Host locale can use ',' as decimal separator; strtod then rejects '.' from
     * piped test input → io.parse / json parse failures on Windows.
     */
    (void)setlocale(LC_ALL, "C");
#ifndef FLAT_WASM_BUILD
    flat_port_stdio_setup();
#endif
    flat_native_runtime_reset();
    flat_date_run_begin(flat_date_global_state());
    flat_native_runtime_set_manifest(bc->optional_natives, bc->optional_native_count);
    memset(&st, 0, sizeof st);
    st.run_opts = opts;
    st.bc = bc;
    st.var_slots_max = bc->var_slots;
    scratch_n = st.var_slots_max > 0 ? st.var_slots_max : 1u;
    st.pool = flat_pool_create();
    st.coll = flat_coll_create();
    flat_mem_stats_init(&st.live);
    st.vcx = flat_vcx_runtime_create(scratch_n);
    if (!st.vcx || !st.pool || !st.coll) {
        return vm_fail_setup(&st, opts);
    }
    flat_vcx_runtime_attach_vm(st.vcx,
                               (FlatVcxVmBind){.vars = (FlatVcxVar **)&st.vars,
                                               .var_count = &st.var_count,
                                               .var_slots_max = &st.var_slots_max});
    flat_vcx_runtime_set_active(st.vcx);
    g_vm_call_instr = &st;
    flat_vcx_set_call_instr_resolver(vm_resolve_call_instr_ctx);
    if (!st.vars) {
        return vm_fail_setup(&st, opts);
    }
    flat_coll_set_live_stats(st.coll, &st.live);
    flat_file_table_init(&st.files);
    st.io_ctx = flat_io_ctx_create(&st.files);
    if (!st.io_ctx) {
        return vm_fail_setup(&st, opts);
    }
    flat_io_ctx_set_vcx(st.io_ctx, st.vcx);
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
        case OP_NEXT:
            /* Loop continuation wins over call return: a for/foreach body may live
             * inside an OP_CALL block (call #worker with for in the body). */
            if (st.loop_sp > 0) {
                ForeachFrame *fr = &st.loops[st.loop_sp - 1];
                fr->index++;
                if (fr->index < fr->item_count) {
                    if (fr->is_for) {
                        set_var_from_number(&st, fr->item_var, (double)fr->index);
                    } else {
                        FlatValue el;
                        if (flat_list_get(st.coll, fr->list_id, fr->index, &el) == 0) {
                            set_var_value(&st, fr->item_var, &el, NULL, 0, 0, NULL, 0);
                        }
                    }
                    ip = fr->loop_block_ip;
                    continue;
                }
                foreach_frame_end(&st, fr);
                st.loop_sp--;
                ip = fr->resume_ip;
                continue;
            }
            if (st.call_sp > 0) {
                ip = st.calls[--st.call_sp].return_ip;
                continue;
            }
            return vm_finish(&st, (char *)guard_names, guard_vals, 0, FLAT_TERM_COMPLETE, opts);
        case OP_RETURN:
            if (st.call_sp > 0) {
                ip = st.calls[--st.call_sp].return_ip;
                continue;
            }
            if (st.loop_sp > 0) {
                ForeachFrame *fr = &st.loops[st.loop_sp - 1];
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
        case OP_FREEZE: {
            apply_freeze(&st, (const char *)payload);
            ip += 2u + payload_size;
            continue;
        }
        case OP_PANIC_RESET: {
            vm_clear_caught_panic(&st);
            ip += 2u + payload_size;
            continue;
        }
        case OP_PANIC_REASON: {
            if (apply_panic_reason(&st, (const char *)payload) != 0) {
                if (vm_jump_panic_any(&st, bc, &ip, st.panic_reason, trace) != 0) {
                    return vm_finish(&st, (char *)guard_names, guard_vals, 1, FLAT_TERM_PANIC, opts);
                }
                continue;
            }
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
            {
                char guard_panic[64];
                int grc;
                uint8_t guard_has_ty[FLAT_MAX_VARS];
                FlatSlotType guard_slot_ty[FLAT_MAX_VARS];
                int gvi;

                for (gvi = 0; gvi < st.var_count; gvi++) {
                    guard_has_ty[gvi] = st.vars[gvi].has_slot_type;
                    if (st.vars[gvi].has_slot_type) {
                        guard_slot_ty[gvi] = st.vars[gvi].slot_type;
                    } else {
                        flat_slot_type_clear(&guard_slot_ty[gvi]);
                    }
                }
                grc = flat_value_eval_guard(expr, guard_vals, st.var_count, guard_names, st.pool,
                                           st.coll, guard_has_ty, guard_slot_ty, guard_panic,
                                           sizeof guard_panic);
                if (grc < 0) {
                    return vm_jump_panic_any(&st, bc, &ip,
                                           guard_panic[0] ? guard_panic : "type.compare.incompatible",
                                           trace);
                }
                if (!grc) {
                    if (bt == 0) {
                        return vm_finish(&st, (char *)guard_names, guard_vals, 2, FLAT_TERM_FAIL,
                                         opts);
                    }
                    ip = bt;
                    continue;
                }
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
            st.calls[st.call_sp].return_ip = ip + 2u + payload_size;
            st.calls[st.call_sp].func_instr_ctx = 0u;
            st.call_sp++;
            ip = target;
            continue;
        }
        case OP_CALL_BLOCK: {
            const char *expr;
            uint32_t block_id = 0;
            uint32_t target = 0;
            int er;

            if (payload_size < 2 || payload[0] == '\0') {
                if (vm_jump_panic_any(&st, bc, &ip, "call.block.arg", trace) != 0) {
                    return vm_finish(&st, (char *)guard_names, guard_vals, 1, FLAT_TERM_PANIC, opts);
                }
                continue;
            }
            expr = (const char *)payload;
            er = vm_eval_block_id_expr(&st, expr, &block_id);
            if (er == -2) {
                if (vm_jump_panic_any(&st, bc, &ip, "call.block.arg", trace) != 0) {
                    return vm_finish(&st, (char *)guard_names, guard_vals, 1, FLAT_TERM_PANIC, opts);
                }
                continue;
            }
            if (er == -3) {
                if (vm_jump_panic_any(&st, bc, &ip, "call.block.id", trace) != 0) {
                    return vm_finish(&st, (char *)guard_names, guard_vals, 1, FLAT_TERM_PANIC, opts);
                }
                continue;
            }
            if (er != 0) {
                if (vm_jump_panic_any(&st, bc, &ip, "call.block.arg", trace) != 0) {
                    return vm_finish(&st, (char *)guard_names, guard_vals, 1, FLAT_TERM_PANIC, opts);
                }
                continue;
            }
            if (vm_ip_for_block_id(bc, block_id, &target) != 0) {
                if (vm_jump_panic_any(&st, bc, &ip, "call.block.notfound", trace) != 0) {
                    return vm_finish(&st, (char *)guard_names, guard_vals, 1, FLAT_TERM_PANIC, opts);
                }
                continue;
            }
            if (st.call_sp >= VM_MAX_CALL) {
                if (vm_jump_panic_any(&st, bc, &ip, "call.block.limit", trace) != 0) {
                    return vm_finish(&st, (char *)guard_names, guard_vals, 1, FLAT_TERM_PANIC, opts);
                }
                continue;
            }
            st.via_jump = 0;
            st.calls[st.call_sp].return_ip = ip + 2u + payload_size;
            st.calls[st.call_sp].func_instr_ctx = 0u;
            st.call_sp++;
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
            int sr;
            memcpy(&target, payload, 4);
            sr = setup_foreach(&st, bc, &ip, (const char *)(payload + 4), ip + 2u + payload_size,
                               target, trace);
            if (sr == 0) {
                return vm_finish(&st, (char *)guard_names, guard_vals, 1, FLAT_TERM_PANIC, opts);
            }
            if (sr == 2) {
                continue;
            }
            if (st.loop_sp > 0 && st.loops[st.loop_sp - 1].item_count > 0) {
                ip = st.loops[st.loop_sp - 1].loop_block_ip;
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
                ip = st.loops[st.loop_sp - 1].loop_block_ip;
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
        case OP_CALL_FUNCTION: {
            if (vm_apply_call_function(&st, bc, payload, payload_size, &ip, trace) != 0) {
                if (vm_jump_panic_any(&st, bc, &ip, "call.function.vm", trace) != 0) {
                    return vm_finish(&st, (char *)guard_names, guard_vals, 1, FLAT_TERM_PANIC, opts);
                }
                continue;
            }
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
