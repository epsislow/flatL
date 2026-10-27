#include "vm.h"
#include "flat.h"
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
} VmState;

static int find_var(VmState *st, const char *name) {
    int i;
    for (i = 0; i < st->var_count; i++) {
        if (strcmp(st->vars[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
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

static void apply_assign(VmState *st, const char *line) {
    char buf[4096];
    char *eq;
    char *lhs;
    char *rhs;
    FlatValue v;
    char names[FLAT_MAX_VARS][64];

    snprintf(buf, sizeof buf, "%s", line);
    eq = strchr(buf, '=');
    if (!eq) {
        return;
    }
    *eq = '\0';
    lhs = trim(buf);
    rhs = trim(eq + 1);
    FlatValue vals[FLAT_MAX_VARS];
    int i;
    var_names_table(st, names);
    for (i = 0; i < st->var_count; i++) {
        vals[i] = st->vars[i].val;
    }
    if (flat_value_parse_assign_rhs(rhs, &v, st->pool, st->coll, vals, st->var_count, names) != 0) {
        return;
    }
    set_var_value(st, lhs, &v);
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

int flat_vm_run(const FlatBytecode *bc) {
    VmState st;
    uint32_t ip;
    size_t entry_index = 0;
    char guard_names[FLAT_MAX_VARS][64];

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
        case OP_ASSIGN:
            apply_assign(&st, (const char *)payload);
            ip += 2u + payload_size;
            continue;
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
