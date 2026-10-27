#include "vm.h"
#include "flat.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VM_MAX_VARS 32
#define VM_MAX_CALL 64
#define VM_MAX_FOREACH 8
#define VM_MAX_LIST 16

typedef struct {
    char name[64];
    char value[128];
} VmVar;

typedef struct {
    uint32_t return_ip;
    int has_frame;
} CallFrame;

typedef struct {
    uint32_t resume_ip;
    uint32_t loop_block_ip;
    char items[VM_MAX_LIST][64];
    int item_count;
    int index;
    char item_var[64];
} ForeachFrame;

typedef struct {
    VmVar vars[VM_MAX_VARS];
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

static void set_var(VmState *st, const char *name, const char *val) {
    int i = find_var(st, name);
    if (i >= 0) {
        snprintf(st->vars[i].value, sizeof st->vars[i].value, "%s", val);
        return;
    }
    if (st->var_count >= VM_MAX_VARS) {
        return;
    }
    snprintf(st->vars[st->var_count].name, sizeof st->vars[st->var_count].name, "%s", name);
    snprintf(st->vars[st->var_count].value, sizeof st->vars[st->var_count].value, "%s", val);
    st->var_count++;
}

static const char *get_var(VmState *st, const char *name) {
    int i = find_var(st, name);
    return i >= 0 ? st->vars[i].value : "";
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
    char buf[256];
    char *eq;
    char *lhs;
    char *rhs;
    snprintf(buf, sizeof buf, "%s", line);
    eq = strstr(buf, "=");
    if (!eq) {
        return;
    }
    *eq = '\0';
    lhs = trim(buf);
    rhs = trim(eq + 1);
    if (rhs[0] == '\'' || rhs[0] == '"') {
        size_t n = strlen(rhs);
        if (n >= 2) {
            rhs[n - 1] = '\0';
            rhs++;
        }
    }
    set_var(st, lhs, rhs);
}

static int eval_guard(VmState *st, const char *expr) {
    char buf[256];
    char *eq;
    char *lhs;
    char *rhs;
    snprintf(buf, sizeof buf, "%s", expr);
    eq = strstr(buf, "==");
    if (!eq) {
        return 0;
    }
    *eq = '\0';
    lhs = trim(buf);
    rhs = trim(eq + 2);
    if (strcmp(lhs, "false") == 0) {
        return strcmp(rhs, "true") == 0 ? 0 : 1;
    }
    if (strcmp(lhs, "true") == 0) {
        return strcmp(rhs, "true") == 0 ? 1 : 0;
    }
    {
        const char *v = get_var(st, lhs);
        if (rhs[0] == '\'' || rhs[0] == '"') {
            char r[128];
            snprintf(r, sizeof r, "%s", rhs + 1);
            if (r[strlen(r) - 1] == '\'' || r[strlen(r) - 1] == '"') {
                r[strlen(r) - 1] = '\0';
            }
            return strcmp(v, r) == 0;
        }
        if (strcmp(rhs, "true") == 0) {
            return strcmp(v, "true") == 0 || strcmp(v, "1") == 0;
        }
        if (strcmp(rhs, "false") == 0) {
            return strcmp(v, "false") == 0;
        }
        return strcmp(v, rhs) == 0;
    }
}

static int split_csv(const char *s, char items[][64], int max) {
    char buf[256];
    char *tok;
    int n = 0;
    snprintf(buf, sizeof buf, "%s", s);
    for (tok = strtok(buf, ","); tok && n < max; tok = strtok(NULL, ",")) {
        snprintf(items[n], 64, "%s", trim(tok));
        n++;
    }
    return n;
}

static void setup_foreach(VmState *st, const char *line, uint32_t resume_ip, uint32_t block_off) {
    char coll[64] = "";
    char item[64] = "";
    const char *lp = strchr(line, '(');
    const char *rp = strrchr(line, ')');
    ForeachFrame *fr;
    const char *src;

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
    src = get_var(st, coll);
    fr->item_count = split_csv(src, fr->items, VM_MAX_LIST);
    fr->index = 0;
    if (fr->item_count > 0) {
        set_var(st, fr->item_var, fr->items[0]);
    }
}

static int block_end_return(VmState *st, uint32_t *ip) {
    if (st->loop_sp > 0) {
        ForeachFrame *fr = &st->loops[st->loop_sp - 1];
        fr->index++;
        if (fr->index < fr->item_count) {
            set_var(st, fr->item_var, fr->items[fr->index]);
            return 2; /* re-enter loop block — caller sets ip from loop frame */
        }
        st->loop_sp--;
        *ip = fr->resume_ip;
        return 1;
    }
    if (st->call_sp > 0) {
        CallFrame *cf = &st->calls[--st->call_sp];
        *ip = cf->return_ip;
        return 1;
    }
    return 0;
}

int flat_vm_run(const FlatBytecode *bc) {
    VmState st;
    uint32_t ip;
    size_t entry_index = 0;
    if (!bc || !bc->data || bc->block_count == 0) {
        return 1;
    }
    memset(&st, 0, sizeof st);

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
            if (block_end_return(&st, &ip)) {
                continue;
            }
            return 1;
        }
        opcode = bc->data[ip];
        payload_size = bc->data[ip + 1];
        if ((size_t)ip + 2 + payload_size > bc->size) {
            return 1;
        }
        payload = &bc->data[ip + 2];

        switch (opcode) {
        case OP_OK:
            return 0;
        case OP_FAIL:
            return 2;
        case OP_RETURN:
            if (st.loop_sp > 0) {
                ForeachFrame *fr = &st.loops[st.loop_sp - 1];
                fr->index++;
                if (fr->index < fr->item_count) {
                    set_var(&st, fr->item_var, fr->items[fr->index]);
                    ip = fr->loop_block_ip;
                    continue;
                }
                st.loop_sp--;
                ip = fr->resume_ip;
                continue;
            }
            if (st.call_sp == 0) {
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
            memcpy(&bt, payload, 4);
            expr = (const char *)(payload + 4);
            if (!eval_guard(&st, expr)) {
                if (bt == 0) {
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
            const char *out_name;
            memcpy(&target, payload, 4);
            out_name = (const char *)(payload + 4);
            if (st.call_sp >= VM_MAX_CALL) {
                return 1;
            }
            st.calls[st.call_sp].return_ip = ip + 2u + payload_size;
            st.calls[st.call_sp].has_frame = 1;
            st.call_sp++;
            (void)out_name;
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
                return 1;
            }
            ip = target;
            continue;
        }
        default:
            fprintf(stderr, "flvm: unsupported opcode 0x%02x at %u\n", opcode, ip);
            return 1;
        }
    }
}
