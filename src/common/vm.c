#include "vm.h"
#include "flat.h"
#include "flat_native.h"
#include "flat_value.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VM_MAX_CALL 64
#define VM_MAX_FOREACH 8
#define VM_MAX_LIST 16

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
} ForeachFrame;

typedef struct {
    FlatStringPool *pool;
    FlatCollectionPool *coll;
    NamedVar vars[FLAT_MAX_VARS];
    int var_count;
    CallFrame calls[VM_MAX_CALL];
    int call_sp;
    ForeachFrame loops[VM_MAX_FOREACH];
    int loop_sp;
    char panic_reason[64];
} VmState;

static uint32_t flat_label_hash(const char *label) {
    uint32_t h = 5381u;
    const unsigned char *p;
    for (p = (const unsigned char *)label; *p; p++) {
        h = h * 33u + *p;
    }
    return h;
}

static int vm_jump_panic_any(const FlatBytecode *bc, uint32_t *ip, const char *reason, int trace) {
    size_t i;
    uint32_t want = flat_label_hash("#panic.any");

    if (trace) {
        fprintf(stderr, "[trace] panic reason=%s handler=#panic.any\n", reason);
    }
    for (i = 0; i < bc->block_count; i++) {
        if (bc->blocks[i].block_id == want) {
            *ip = bc->blocks[i].offset;
            return 0;
        }
    }
    fprintf(stderr, "flvm: unhandled panic: %s\n", reason);
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
    default:
        return "io.error";
    }
}

static int vm_handle_io_status(VmState *st, const FlatBytecode *bc, uint32_t *ip, FlatIoStatus ios,
                               int trace) {
    if (ios == FLAT_IO_OK) {
        return 0;
    }
    if (ios == FLAT_IO_ERR) {
        return 0;
    }
    snprintf(st->panic_reason, sizeof st->panic_reason, "%s", io_status_reason(ios));
    return vm_jump_panic_any(bc, ip, st->panic_reason, trace);
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

static int set_var_value(VmState *st, const char *name, const FlatValue *val) {
    int i = find_var(st, name);
    if (i >= 0) {
        st->vars[i].val = *val;
        return 0;
    }
    if (st->var_count >= FLAT_MAX_VARS) {
        return -1;
    }
    if (var_name_forbidden(name)) {
        return -1;
    }
    snprintf(st->vars[st->var_count].name, sizeof st->vars[st->var_count].name, "%s", name);
    st->vars[st->var_count].val = *val;
    st->var_count++;
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

static int apply_assign(VmState *st, const FlatBytecode *bc, uint32_t *ip, const char *line,
                        int trace) {
    char buf[4096];
    char *eq;
    char *lhs;
    char *rhs;
    FlatValue v;
    char names[FLAT_MAX_VARS][64];
    FlatIoStatus ios;
    FlatValue vals[FLAT_MAX_VARS];
    int i;

    snprintf(buf, sizeof buf, "%s", line);
    eq = strchr(buf, '=');
    if (!eq) {
        return 0;
    }
    *eq = '\0';
    lhs = trim(buf);
    rhs = trim(eq + 1);
    if (var_name_forbidden(lhs)) {
        return 0;
    }
    var_names_table(st, names);
    for (i = 0; i < st->var_count; i++) {
        vals[i] = st->vars[i].val;
    }
    if (rhs_is_native_call(rhs)) {
        if (flat_io_exec_line(rhs, st->pool, st->coll, vals, st->var_count, names, &v, &ios) != 0) {
            return 0;
        }
        if (vm_handle_io_status(st, bc, ip, ios, trace) != 0) {
            return 1;
        }
        if (ios == FLAT_IO_OK) {
            set_var_value(st, lhs, &v);
        }
        return 0;
    }
    if (flat_value_parse_assign_rhs(rhs, &v, st->pool, st->coll, vals, st->var_count, names) != 0) {
        return 0;
    }
    set_var_value(st, lhs, &v);
    return 0;
}

static int split_csv_from_var(VmState *st, const char *coll, char items[][64], int max) {
    char buf[4096];
    int vi = find_var(st, coll);
    char *tok;
    int n = 0;

    if (vi < 0) {
        return 0;
    }
    if (flat_value_get_cstr(&st->vars[vi].val, st->pool, buf, sizeof buf) != 0) {
        return 0;
    }
    for (tok = strtok(buf, ","); tok && n < max; tok = strtok(NULL, ",")) {
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

static void setup_foreach(VmState *st, const char *line, uint32_t resume_ip, uint32_t block_off) {
    char coll[64] = "";
    char item[64] = "";
    const char *lp = strchr(line, '(');
    const char *rp = strrchr(line, ')');
    ForeachFrame *fr;

    if (lp && rp && rp > lp) {
        char inner[256];
        char *tok;
        int part = 0;
        snprintf(inner, sizeof inner, "%.*s", (int)(rp - lp - 1), lp + 1);
        for (tok = strtok(inner, ","); tok; tok = strtok(NULL, ","), part++) {
            if (part == 0) {
                snprintf(coll, sizeof coll, "%s", trim(tok));
            } else if (part == 1) {
                snprintf(item, sizeof item, "%s", trim(tok));
            }
        }
    }
    if (st->loop_sp >= VM_MAX_FOREACH) {
        return;
    }
    fr = &st->loops[st->loop_sp++];
    fr->resume_ip = resume_ip;
    fr->loop_block_ip = block_off;
    snprintf(fr->item_var, sizeof fr->item_var, "%s", item);
    fr->use_list = 0;
    fr->list_id = 0;
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
    }
}

static void vm_teardown(VmState *st, int rc) {
    (void)rc;
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

static int run_native_call(VmState *st, const FlatBytecode *bc, uint32_t *ip, uint16_t native_id,
                           const char *inner, const char *bind, int trace) {
    char names[FLAT_MAX_VARS][64];
    FlatValue vals[FLAT_MAX_VARS];
    FlatValue result;
    FlatIoStatus ios;
    FlatNativeCtx ctx;
    int i;

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
    ios = FLAT_IO_ERR;
    if (flat_native_invoke(native_id, &ctx) != 0) {
        return 0;
    }
    if (vm_handle_io_status(st, bc, ip, ios, trace) != 0) {
        return 1;
    }
    if (ios == FLAT_IO_OK && bind && bind[0] != '\0') {
        set_var_value(st, bind, &result);
    }
    return 0;
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
    return run_native_call(st, bc, ip, id, inner, bind, trace);
}

static int apply_native(VmState *st, const FlatBytecode *bc, uint32_t *ip, const uint8_t *payload,
                        uint8_t payload_size, int trace) {
    uint16_t id;
    const char *inner;
    const char *bind;

    if (payload_size < 3) {
        return 0;
    }
    id = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
    inner = (const char *)(payload + 2);
    bind = inner + strlen(inner) + 1;
    if ((size_t)(bind - (const char *)payload) + strlen(bind) + 1 > payload_size) {
        return 0;
    }
    return run_native_call(st, bc, ip, id, inner, bind, trace);
}

int flat_vm_run(const FlatBytecode *bc) {
    return flat_vm_run_opts(bc, NULL);
}

int flat_vm_run_opts(const FlatBytecode *bc, const FlatVmOptions *opts) {
    VmState st;
    uint32_t ip;
    size_t entry_index = 0;
    char guard_names[FLAT_MAX_VARS][64];
    int trace = opts && opts->trace;
    int trace_vars = opts && opts->trace_vars;

    if (!bc || !bc->data || bc->block_count == 0) {
        return 1;
    }
    memset(&st, 0, sizeof st);
    st.pool = flat_pool_create();
    st.coll = flat_coll_create();
    if (!st.pool || !st.coll) {
        flat_coll_destroy(st.coll);
        flat_pool_destroy(st.pool);
        return 1;
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

        if ((size_t)ip + 2 > bc->size) {
            return 1;
        }
        opcode = bc->data[ip];
        payload_size = bc->data[ip + 1];
        if ((size_t)ip + 2 + payload_size > bc->size) {
            vm_teardown(&st, 1);
            return 1;
        }
        payload = &bc->data[ip + 2];

        if (trace || trace_vars) {
            fprintf(stderr, "[trace] ip=%u op=%s(0x%02x) len=%u", ip, opcode_name(opcode), opcode,
                    (unsigned)payload_size);
            if (opcode == OP_ASSIGN) {
                size_t sl = payload_size;
                const char *pt = (const char *)payload;
                while (sl > 0 && pt[sl - 1] != '\0') {
                    sl--;
                }
                if (sl > 0) {
                    sl--;
                }
                fprintf(stderr, " %.*s", (int)sl, pt);
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
            vm_teardown(&st, 0);
            return 0;
        case OP_FAIL:
            vm_teardown(&st, 2);
            return 2;
        case OP_RETURN:
            if (st.loop_sp > 0) {
                ForeachFrame *fr = &st.loops[st.loop_sp - 1];
                fr->index++;
                if (fr->index < fr->item_count) {
                    if (fr->use_list) {
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
                st.loop_sp--;
                ip = fr->resume_ip;
                continue;
            }
            if (st.call_sp == 0) {
                vm_teardown(&st, 0);
                return 0;
            }
            ip = st.calls[--st.call_sp].return_ip;
            continue;
        case OP_ASSIGN: {
            uint32_t ip_before = ip;
            if (apply_assign(&st, bc, &ip, (const char *)payload, trace) != 0) {
                vm_teardown(&st, 1);
                return 1;
            }
            if (ip == ip_before) {
                ip += 2u + payload_size;
            }
            continue;
        }
        case OP_IO: {
            uint32_t ip_before = ip;
            if (apply_io_legacy(&st, bc, &ip, (const char *)payload, trace) != 0) {
                vm_teardown(&st, 1);
                return 1;
            }
            if (ip == ip_before) {
                ip += 2u + payload_size;
            }
            continue;
        }
        case OP_NATIVE: {
            uint32_t ip_before = ip;
            if (apply_native(&st, bc, &ip, payload, payload_size, trace) != 0) {
                vm_teardown(&st, 1);
                return 1;
            }
            if (ip == ip_before) {
                ip += 2u + payload_size;
            }
            continue;
        }
        case OP_GUARD: {
            uint32_t bt = 0;
            const char *expr;
            var_names_table(&st, guard_names);
            memcpy(&bt, payload, 4);
            expr = (const char *)(payload + 4);
            FlatValue gvals[FLAT_MAX_VARS];
            int gi;
            for (gi = 0; gi < st.var_count; gi++) {
                gvals[gi] = st.vars[gi].val;
            }
            if (!flat_value_eval_guard(expr, gvals, st.var_count, guard_names, st.pool)) {
                if (bt == 0) {
                    vm_teardown(&st, 2);
                    return 2;
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
                vm_teardown(&st, 1);
                return 1;
            }
            st.calls[st.call_sp++].return_ip = ip + 2u + payload_size;
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
        case OP_PANIC: {
            uint32_t target = 0;
            if (payload_size >= 4) {
                memcpy(&target, payload, 4);
            }
            if (target == 0) {
                vm_teardown(&st, 1);
                return 1;
            }
            ip = target;
            continue;
        }
        default:
            fprintf(stderr, "flvm: unsupported opcode 0x%02x at %u\n", opcode, ip);
            vm_teardown(&st, 1);
            return 1;
        }
    }
}
