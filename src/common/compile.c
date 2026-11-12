#include "fgir.h"
#include "flat_func.h"
#include "flat.h"
#include "flat_value.h"
#include "fb_format.h"
#include "flat_guard.h"
#include "flat_line_label.h"
#include "flat_native.h"
#include "flat_native_use.h"
#include "flat_number.h"
#include "flat_slot_type.h"
#include "flat_schema.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef char VarName[64];

static const FlatSchemaCatalog *g_compile_schemas;

/* --- var_slots: distinct bind target names across the whole linked program --- */

typedef struct {
    char (*names)[FLAT_VAR_NAME_MAX + 1];
    int *table; /* open addressing; value = index into names or -1 */
    size_t table_size;
    int count;
} VarSet;

static uint32_t varset_hash(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

/* returns 0 ok (added or present), 1 if cap exceeded, -1 on OOM */
static int varset_add(VarSet *vs, const char *raw) {
    char name[FLAT_VAR_NAME_MAX + 1];
    size_t pos;

    snprintf(name, sizeof name, "%s", raw);
    if (name[0] == '\0') {
        return 0;
    }
    pos = varset_hash(name) & (vs->table_size - 1);
    while (vs->table[pos] >= 0) {
        if (strcmp(vs->names[vs->table[pos]], name) == 0) {
            return 0;
        }
        pos = (pos + 1) & (vs->table_size - 1);
    }
    if (vs->count >= FLAT_MAX_VARS) {
        return 1;
    }
    memcpy(vs->names[vs->count], name, sizeof name);
    vs->table[pos] = vs->count++;
    return 0;
}

static int bind_name_forbidden(const char *n) {
    return strcmp(n, "null") == 0 || strcmp(n, "true") == 0 || strcmp(n, "false") == 0;
}

static int varset_add_assign_lhs(VarSet *vs, const char *text) {
    char buf[FGIR_MAX_LINE];
    char *bind;
    char *value;
    FlatAssignSplitForm form;

    (void)value;
    snprintf(buf, sizeof buf, "%s", text);
    (void)flat_assign_strip_const_prefix(buf, NULL);
    if (flat_assign_split_line(buf, &bind, &value, &form) != 0) {
        return 0;
    }
    if (bind[0] == '\0' || bind_name_forbidden(bind)) {
        return 0;
    }
    {
        char name[FLAT_VAR_NAME_MAX + 1];
        FlatSlotType ty;
        int has;

        if (g_compile_schemas) {
            if (flat_bind_parse_lhs_catalog(bind, name, sizeof name, &ty, &has,
                                            g_compile_schemas) != 0 ||
                name[0] == '\0') {
                return 0;
            }
        } else if (flat_bind_parse_lhs(bind, name, sizeof name, &ty, &has) != 0 ||
                   name[0] == '\0') {
            return 0;
        }
        return varset_add(vs, name);
    }
}

static int varset_has_name(const VarSet *vs, const char *name) {
    size_t pos;
    char key[FLAT_VAR_NAME_MAX + 1];

    if (!vs || !name || !name[0]) {
        return 0;
    }
    snprintf(key, sizeof key, "%s", name);
    pos = varset_hash(key) & (vs->table_size - 1);
    while (vs->table[pos] >= 0) {
        if (strcmp(vs->names[vs->table[pos]], key) == 0) {
            return 1;
        }
        pos = (pos + 1) & (vs->table_size - 1);
    }
    return 0;
}

static int validate_free_in_program(const FgirProgram *prog, char *err, size_t err_size) {
    VarSet vs;
    int bi, ii;
    int rc = 0;

    memset(&vs, 0, sizeof vs);
    vs.table_size = 65536;
    vs.names = malloc((size_t)FLAT_MAX_VARS * sizeof *vs.names);
    vs.table = malloc(vs.table_size * sizeof *vs.table);
    if (!vs.names || !vs.table) {
        free(vs.names);
        free(vs.table);
        snprintf(err, err_size, "out of memory");
        return -1;
    }
    memset(vs.table, 0xff, vs.table_size * sizeof *vs.table);

    for (bi = 0; bi < prog->block_count && rc == 0; bi++) {
        const FgirBlock *b = &prog->blocks[bi];
        for (ii = 0; ii < b->instr_count && rc == 0; ii++) {
            const FgirInstr *ins = &b->instr[ii];
            switch (ins->kind) {
            case FGIR_ASSIGN:
                rc = varset_add_assign_lhs(&vs, ins->text);
                break;
            case FGIR_IO:
            case FGIR_CALL:
            case FGIR_CALL_BLOCK:
                if (ins->call_out[0] && !bind_name_forbidden(ins->call_out)) {
                    rc = varset_add(&vs, ins->call_out);
                }
                break;
            case FGIR_FOREACH:
            case FGIR_FOR:
                if (ins->foreach_item[0] && !bind_name_forbidden(ins->foreach_item)) {
                    rc = varset_add(&vs, ins->foreach_item);
                }
                break;
            default:
                break;
            }
        }
    }
    if (rc == 1) {
        snprintf(err, err_size, "too many distinct variables for free validation");
        rc = -1;
    }

    for (bi = 0; bi < prog->block_count && rc == 0; bi++) {
        const FgirBlock *b = &prog->blocks[bi];
        for (ii = 0; ii < b->instr_count && rc == 0; ii++) {
            const FgirInstr *ins = &b->instr[ii];
            if (ins->kind == FGIR_FREE) {
                char var[64];
                if (flat_parse_free_varname(ins->text, var, sizeof var) != 0) {
                    snprintf(err, err_size, "invalid free: %s", ins->text);
                    rc = -1;
                    break;
                }
                if (!varset_has_name(&vs, var)) {
                    snprintf(err, err_size, "free of undeclared variable %s in %s", var,
                             b->label);
                    rc = -1;
                }
            }
            if (ins->kind == FGIR_FREEZE) {
                char var[64];
                if (flat_parse_freeze_varname(ins->text, var, sizeof var) != 0) {
                    snprintf(err, err_size, "invalid freeze: %s", ins->text);
                    rc = -1;
                    break;
                }
                if (!varset_has_name(&vs, var)) {
                    snprintf(err, err_size, "freeze of undeclared variable %s in %s", var,
                             b->label);
                    rc = -1;
                }
            }
        }
    }

    free(vs.names);
    free(vs.table);
    return rc;
}

/* Scans every instruction that can create a variable at runtime:
 *   FGIR_ASSIGN lhs, FGIR_IO bind (-> var), FGIR_CALL out, FGIR_FOREACH item. */
static int count_var_slots(const FgirProgram *prog, uint32_t *out_slots, char *err,
                           size_t err_size) {
    VarSet vs;
    int bi, ii;
    int rc = 0;

    memset(&vs, 0, sizeof vs);
    vs.table_size = 65536; /* power of two, > 2 * FLAT_MAX_VARS */
    vs.names = malloc((size_t)FLAT_MAX_VARS * sizeof *vs.names);
    vs.table = malloc(vs.table_size * sizeof *vs.table);
    if (!vs.names || !vs.table) {
        free(vs.names);
        free(vs.table);
        snprintf(err, err_size, "out of memory");
        return -1;
    }
    memset(vs.table, 0xff, vs.table_size * sizeof *vs.table);

    for (bi = 0; bi < prog->block_count && rc == 0; bi++) {
        const FgirBlock *b = &prog->blocks[bi];
        for (ii = 0; ii < b->instr_count && rc == 0; ii++) {
            const FgirInstr *ins = &b->instr[ii];
            switch (ins->kind) {
            case FGIR_ASSIGN:
                rc = varset_add_assign_lhs(&vs, ins->text);
                break;
            case FGIR_IO:
            case FGIR_CALL:
            case FGIR_CALL_BLOCK:
                if (ins->call_out[0] && !bind_name_forbidden(ins->call_out)) {
                    rc = varset_add(&vs, ins->call_out);
                }
                break;
            case FGIR_FOREACH:
            case FGIR_FOR:
                if (ins->foreach_item[0] && !bind_name_forbidden(ins->foreach_item)) {
                    rc = varset_add(&vs, ins->foreach_item);
                }
                break;
            default:
                break;
            }
        }
    }
    if (rc == 1) {
        snprintf(err, err_size,
                 "too many distinct variables: program binds more than %d names (FLAT_MAX_VARS)",
                 FLAT_MAX_VARS);
        rc = -1;
    } else if (rc == 0) {
        *out_slots = (uint32_t)vs.count;
    }
    free(vs.names);
    free(vs.table);
    return rc == 0 ? 0 : -1;
}

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
} EmitBuf;

static int emit_u8(EmitBuf *e, uint8_t v) {
    if (e->len + 1 > e->cap) {
        size_t n = e->cap ? e->cap * 2 : 256;
        uint8_t *p = realloc(e->buf, n);
        if (!p) {
            return -1;
        }
        e->buf = p;
        e->cap = n;
    }
    e->buf[e->len++] = v;
    return 0;
}

static int emit_bytes(EmitBuf *e, const void *p, size_t n) {
    const uint8_t *b = p;
    size_t i;
    for (i = 0; i < n; i++) {
        if (emit_u8(e, b[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int emit_instr(EmitBuf *e, uint8_t op, const void *payload, uint8_t plen) {
    if (emit_u8(e, op) != 0 || emit_u8(e, plen) != 0) {
        return -1;
    }
    if (plen > 0 && payload && emit_bytes(e, payload, plen) != 0) {
        return -1;
    }
    return 0;
}

static uint32_t hash_label_u32(const char *label) {
    uint32_t h = 5381u;
    const unsigned char *p;
    for (p = (const unsigned char *)label; *p; p++) {
        h = h * 33u + *p;
    }
    return h;
}

static int block_id_for(const FgirBlock *b) {
    if (strcmp(b->label, "#main") == 0) {
        return FLAT_FB_BLOCK_ID_MAIN;
    }
    return (int)hash_label_u32(b->label);
}

static int find_block_idx(const FgirProgram *p, const char *label) {
    int i;
    for (i = 0; i < p->block_count; i++) {
        if (strcmp(p->blocks[i].label, label) == 0) {
            return i;
        }
    }
    return -1;
}

/* Room for "#or." / "#panic." + block label + suffix (GCC -Wformat-truncation). */
enum { FLAT_HANDLER_LABEL_BUF = 12 + FGIR_MAX_LABEL + 64 };

static int find_guard_handler(const FgirProgram *prog, const FgirBlock *b, const FgirInstr *ins) {
    char lab[FLAT_HANDLER_LABEL_BUF];
    int idx;

    snprintf(lab, sizeof lab, FLAT_OR_HANDLER_PREFIX "%s.%d", b->label, ins->index);
    idx = find_block_idx(prog, lab);
    if (idx >= 0) {
        return idx;
    }
    if (ins->line_label[0]) {
        snprintf(lab, sizeof lab, FLAT_OR_HANDLER_PREFIX "%s.%s", b->label, ins->line_label);
        idx = find_block_idx(prog, lab);
        if (idx >= 0) {
            return idx;
        }
        if (flat_label_is_or_handler(b->label)) {
            snprintf(lab, sizeof lab, FLAT_OR_HANDLER_PREFIX "%s", ins->line_label);
            return find_block_idx(prog, lab);
        }
    }
    return -1;
}

static int find_panic_handler(const FgirProgram *prog, const FgirBlock *b, const FgirInstr *ins) {
    char lab[FLAT_HANDLER_LABEL_BUF];
    int idx;

    snprintf(lab, sizeof lab, "#panic.%s.%d", b->label, ins->index);
    idx = find_block_idx(prog, lab);
    if (idx >= 0) {
        return idx;
    }
    if (ins->line_label[0]) {
        snprintf(lab, sizeof lab, "#panic.%s.%s", b->label, ins->line_label);
        return find_block_idx(prog, lab);
    }
    return -1;
}

static int validate_line_label_handlers(const FgirProgram *prog, char *err, size_t err_size) {
    int i;

    for (i = 0; i < prog->block_count; i++) {
        const char *lab = prog->blocks[i].label;
        char canonical[FGIR_MAX_LABEL];

        if (!flat_label_is_or_handler(lab) && strncmp(lab, "#panic.", 7) != 0) {
            continue;
        }
        if (flat_line_label_resolve_handler(prog, lab, canonical, sizeof canonical, err,
                                            err_size) < 0) {
            return -1;
        }
    }
    return 0;
}

typedef struct {
    size_t abs_offset;
} BlockOff;

static int loop_block_compile_idx(const FgirProgram *prog, const char *tok) {
    char label[FGIR_MAX_LABEL];

    if (!tok || !tok[0]) {
        return -1;
    }
    if (tok[0] == '#') {
        return find_block_idx(prog, tok);
    }
    snprintf(label, sizeof label, "#%s", tok);
    return find_block_idx(prog, label);
}

static int emit_block_code(EmitBuf *code, const FgirProgram *prog, int bi,
                           const BlockOff *offs, size_t stream_base, char *err, size_t err_size) {
    const FgirBlock *b = &prog->blocks[bi];
    int j;

    for (j = 0; j < b->instr_count; j++) {
        const FgirInstr *ins = &b->instr[j];
        switch (ins->kind) {
        case FGIR_OK:
            if (emit_instr(code, OP_OK, NULL, 0) != 0) {
                return -1;
            }
            break;
        case FGIR_FAIL:
            if (emit_instr(code, OP_FAIL, NULL, 0) != 0) {
                return -1;
            }
            break;
        case FGIR_RETURN:
            if (emit_instr(code, OP_RETURN, NULL, 0) != 0) {
                return -1;
            }
            break;
        case FGIR_NEXT:
            if (emit_instr(code, OP_NEXT, NULL, 0) != 0) {
                return -1;
            }
            break;
        case FGIR_ASSIGN: {
            uint8_t payload[1 + FGIR_MAX_LINE + 4];
            uint32_t or_off = 0;
            size_t tlen = strlen(ins->text) + 1;
            uint8_t plen;
            char assign_chk[FGIR_MAX_LINE];
            char *bind;
            char *value;
            FlatAssignSplitForm form;

            snprintf(assign_chk, sizeof assign_chk, "%s", ins->text);
            {
                int is_const = 0;

                if (flat_assign_strip_const_prefix(assign_chk, &is_const) != 0) {
                    snprintf(err, err_size, "invalid assignment: %s", ins->text);
                    return -1;
                }
                if (is_const && !flat_assign_lhs_has_type_in_bind(assign_chk)) {
                    snprintf(err, err_size, "const bind requires :type: %s", ins->text);
                    return -1;
                }
            }
            if (flat_assign_split_line(assign_chk, &bind, &value, &form) != 0) {
                snprintf(err, err_size, "invalid assignment: %s", ins->text);
                return -1;
            }
            if (flat_bind_validate_lhs(bind, g_compile_schemas, err, err_size) != 0) {
                return -1;
            }
            if (tlen + 1 > sizeof payload) {
                snprintf(err, err_size, "assign too long");
                return -1;
            }
            if (ins->line_mod == FGIR_LINE_MOD_TILDE) {
                int bt_idx = find_guard_handler(prog, b, ins);
                if (bt_idx >= 0) {
                    or_off = (uint32_t)offs[bt_idx].abs_offset;
                }
            }
            payload[0] = (uint8_t)ins->line_mod;
            memcpy(payload + 1, ins->text, tlen);
            plen = (uint8_t)(1 + tlen);
            if (ins->line_mod == FGIR_LINE_MOD_TILDE) {
                memcpy(payload + plen, &or_off, 4);
                plen = (uint8_t)(plen + 4);
            }
            if (plen > 225) {
                snprintf(err, err_size, "assign too long");
                return -1;
            }
            if (emit_instr(code, OP_ASSIGN, payload, plen) != 0) {
                return -1;
            }
            break;
        }
        case FGIR_FREE: {
            char var[64];
            uint8_t plen;
            if (flat_parse_free_varname(ins->text, var, sizeof var) != 0) {
                snprintf(err, err_size, "invalid free: %s", ins->text);
                return -1;
            }
            plen = (uint8_t)(strlen(var) + 1);
            if (emit_instr(code, OP_FREE, var, plen) != 0) {
                return -1;
            }
            break;
        }
        case FGIR_FREEZE: {
            char var[64];
            uint8_t plen;
            if (flat_parse_freeze_varname(ins->text, var, sizeof var) != 0) {
                snprintf(err, err_size, "invalid freeze: %s", ins->text);
                return -1;
            }
            plen = (uint8_t)(strlen(var) + 1);
            if (emit_instr(code, OP_FREEZE, var, plen) != 0) {
                return -1;
            }
            break;
        }
        case FGIR_PANIC_RESET:
            if (emit_instr(code, OP_PANIC_RESET, NULL, 0) != 0) {
                return -1;
            }
            break;
        case FGIR_PANIC_REASON: {
            char var[64];
            uint8_t plen;
            if (flat_parse_panic_reason_bind(ins->text, var, sizeof var) != 0) {
                snprintf(err, err_size, "invalid panicReason bind: %s", ins->text);
                return -1;
            }
            plen = (uint8_t)(strlen(var) + 1);
            if (emit_instr(code, OP_PANIC_REASON, var, plen) != 0) {
                return -1;
            }
            break;
        }
        case FGIR_GUARD: {
            uint8_t payload[4 + FGIR_MAX_LINE];
            int bt_idx;
            uint32_t off32 = 0;
            uint8_t plen;
            bt_idx = find_guard_handler(prog, b, ins);
            if (bt_idx >= 0) {
                off32 = (uint32_t)offs[bt_idx].abs_offset;
            }
            memcpy(payload, &off32, 4);
            plen = (uint8_t)(4 + strlen(ins->text) + 1);
            memcpy(payload + 4, ins->text, strlen(ins->text) + 1);
            if (emit_instr(code, OP_GUARD, payload, plen) != 0) {
                return -1;
            }
            break;
        }
        case FGIR_CALL: {
            uint8_t payload[4 + FGIR_MAX_LABEL];
            int tgt = find_block_idx(prog, ins->call_target);
            uint32_t off32 = 0;
            uint8_t plen;
            if (tgt < 0) {
                snprintf(err, err_size, "unknown call target %s", ins->call_target);
                return -1;
            }
            off32 = (uint32_t)offs[tgt].abs_offset;
            memcpy(payload, &off32, 4);
            memcpy(payload + 4, ins->call_out, strlen(ins->call_out) + 1);
            plen = (uint8_t)(4 + strlen(ins->call_out) + 1);
            if (emit_instr(code, OP_CALL, payload, plen) != 0) {
                return -1;
            }
            break;
        }
        case FGIR_CALL_BLOCK: {
            uint8_t payload[FGIR_MAX_LABEL + 64];
            size_t elen = strlen(ins->call_target) + 1;
            size_t blen = strlen(ins->call_out) + 1;
            uint8_t plen;
            if (elen + blen > sizeof payload) {
                snprintf(err, err_size, "call block expression too long");
                return -1;
            }
            memcpy(payload, ins->call_target, elen);
            memcpy(payload + elen, ins->call_out, blen);
            plen = (uint8_t)(elen + blen);
            if (emit_instr(code, OP_CALL_BLOCK, payload, plen) != 0) {
                return -1;
            }
            break;
        }
        case FGIR_JUMP: {
            uint8_t payload[4];
            int tgt = find_block_idx(prog, ins->call_target);
            uint32_t off32 = 0;
            if (tgt < 0) {
                snprintf(err, err_size, "unknown jump target %s", ins->call_target);
                return -1;
            }
            if (ins->call_target[0] == '\0') {
                snprintf(err, err_size, "jump requires #block target");
                return -1;
            }
            off32 = (uint32_t)offs[tgt].abs_offset;
            memcpy(payload, &off32, 4);
            if (emit_instr(code, OP_JUMP, payload, 4) != 0) {
                return -1;
            }
            break;
        }
        case FGIR_FOREACH: {
            uint8_t payload[4 + FGIR_MAX_LINE];
            int tgt = loop_block_compile_idx(prog, ins->foreach_block);
            uint32_t off32 = 0;
            uint8_t plen = (uint8_t)(4 + strlen(ins->text) + 1);
            if (ins->foreach_item[0] == '\0' || ins->foreach_block[0] == '\0') {
                snprintf(err, err_size, "invalid foreach: %s", ins->text);
                return -1;
            }
            if (tgt < 0) {
                off32 = 0;
            } else {
                off32 = (uint32_t)offs[tgt].abs_offset;
            }
            memcpy(payload, &off32, 4);
            memcpy(payload + 4, ins->text, strlen(ins->text) + 1);
            if (emit_instr(code, OP_FOREACH, payload, plen) != 0) {
                return -1;
            }
            break;
        }
        case FGIR_FOR: {
            uint8_t payload[4 + FGIR_MAX_LINE];
            int tgt = loop_block_compile_idx(prog, ins->foreach_block);
            uint32_t off32 = 0;
            uint8_t plen = (uint8_t)(4 + strlen(ins->text) + 1);
            if (ins->for_limit[0] == '\0' || ins->foreach_item[0] == '\0' ||
                ins->foreach_block[0] == '\0') {
                snprintf(err, err_size, "invalid for: %s", ins->text);
                return -1;
            }
            if (flat_number_is_negative_literal_token(ins->for_limit)) {
                snprintf(err, err_size, "for: negative ntimes literal: %s", ins->text);
                return -1;
            }
            if (tgt < 0) {
                off32 = 0;
            } else {
                off32 = (uint32_t)offs[tgt].abs_offset;
            }
            memcpy(payload, &off32, 4);
            memcpy(payload + 4, ins->text, strlen(ins->text) + 1);
            if (emit_instr(code, OP_FOR, payload, plen) != 0) {
                return -1;
            }
            break;
        }
        case FGIR_IO: {
            uint8_t payload[2 + FGIR_MAX_LINE + 64 + 5];
            uint16_t native_id;
            char inner[FGIR_MAX_LINE];
            size_t il;
            size_t bl;
            size_t total;
            uint8_t plen;
            uint32_t or_off = 0;

            {
                char nerr[256];
                nerr[0] = '\0';
                if (flat_native_split_call_prog(ins->text, prog, prog->blocks[bi].native_unit_index,
                                                &native_id, inner, sizeof inner, nerr,
                                                sizeof nerr) != 0) {
                    if (nerr[0]) {
                        snprintf(err, err_size, "%s", nerr);
                    } else {
                        snprintf(err, err_size, "unknown native call: %s", ins->text);
                    }
                    return -1;
                }
            }
            if (ins->line_mod == FGIR_LINE_MOD_TILDE) {
                int bt_idx = find_guard_handler(prog, b, ins);
                if (bt_idx >= 0) {
                    or_off = (uint32_t)offs[bt_idx].abs_offset;
                }
            }
            il = strlen(inner) + 1;
            bl = strlen(ins->call_out) + 1;
            total = 2 + il + bl + 1 + 4;
            if (total > sizeof payload || total > 255) {
                snprintf(err, err_size, "native payload too long");
                return -1;
            }
            payload[0] = (uint8_t)(native_id & 0xffu);
            payload[1] = (uint8_t)((native_id >> 8) & 0xffu);
            memcpy(payload + 2, inner, il);
            memcpy(payload + 2 + il, ins->call_out, bl);
            payload[2 + il + bl] = (uint8_t)ins->line_mod;
            memcpy(payload + 2 + il + bl + 1, &or_off, 4);
            plen = (uint8_t)total;
            if (emit_instr(code, OP_NATIVE, payload, plen) != 0) {
                return -1;
            }
            break;
        }
        case FGIR_RAISE_PANIC: {
            uint8_t payload[4];
            int pidx;
            uint32_t off32 = 0;
            pidx = find_panic_handler(prog, b, ins);
            if (pidx < 0) {
                pidx = find_block_idx(prog, "#panic.any");
            }
            if (pidx >= 0) {
                off32 = (uint32_t)offs[pidx].abs_offset;
            }
            memcpy(payload, &off32, 4);
            if (emit_instr(code, OP_PANIC, payload, 4) != 0) {
                return -1;
            }
            break;
        }
        case FGIR_FUNC_CALL_VM: {
            const FgirFunction *fn = flat_func_find(prog, ins->func_name);
            uint8_t payload[255];
            size_t pos = 0;
            uint16_t fi;
            int pi;
            uint8_t plen;

            if (!fn) {
                snprintf(err, err_size, "call vm: unknown function %s", ins->func_name);
                return -1;
            }
            fi = (uint16_t)flat_func_index(prog, ins->func_name);
            if (fi >= (uint16_t)prog->function_count) {
                snprintf(err, err_size, "call vm: bad function index");
                return -1;
            }
            if (pos + 4 >= sizeof payload) {
                return -1;
            }
            memcpy(payload + pos, &fi, 2);
            pos += 2;
            payload[pos++] = (uint8_t)fn->param_count;
            payload[pos++] = (uint8_t)(fn->is_global_executor ? 1 : 0);
            {
                size_t rl = strlen(ins->func_ret) + 1;
                if (pos + rl > sizeof payload) {
                    snprintf(err, err_size, "call vm payload too long");
                    return -1;
                }
                memcpy(payload + pos, ins->func_ret, rl);
                pos += rl;
            }
            for (pi = 0; pi < fn->param_count; pi++) {
                size_t nl = strlen(fn->params[pi]) + 1;
                if (pos + nl > sizeof payload) {
                    snprintf(err, err_size, "call vm payload too long");
                    return -1;
                }
                memcpy(payload + pos, fn->params[pi], nl);
                pos += nl;
            }
            if (pos > 255) {
                snprintf(err, err_size, "call vm payload too long");
                return -1;
            }
            plen = (uint8_t)pos;
            if (emit_instr(code, OP_CALL_FUNCTION, payload, plen) != 0) {
                return -1;
            }
            break;
        }
        default:
            snprintf(err, err_size, "unsupported instruction");
            return -1;
        }
    }
    (void)stream_base;
    return 0;
}

static size_t measure_block(const FgirProgram *prog, int bi) {
    EmitBuf tmp = {0};
    BlockOff *dummy = NULL;
    char err[64];
    size_t n;

    dummy = calloc((size_t)prog->block_count, sizeof(BlockOff));
    if (!dummy) {
        return 0;
    }
    emit_block_code(&tmp, prog, bi, dummy, 0, err, sizeof err);
    n = tmp.len;
    free(dummy);
    free(tmp.buf);
    return n;
}

int flat_compile_fgir(const FgirProgram *prog, const char *out_path, char *err, size_t err_size) {
    BlockOff *offs = NULL;
    EmitBuf code = {0};
    uint32_t *block_ids = NULL;
    uint32_t var_slots = 0;
    size_t stream_base;
    FILE *f;
    int i;
    FlatSchemaCatalog schema_catalog;

    if (!prog || !out_path) {
        return -1;
    }
    memset(&schema_catalog, 0, sizeof schema_catalog);
    if (flat_schema_build_catalog(prog->schemas, prog->schema_count, &schema_catalog, err,
                                  err_size) != 0) {
        return -1;
    }
    g_compile_schemas = &schema_catalog;
    if (validate_free_in_program(prog, err, err_size) != 0) {
        return -1;
    }
    if (validate_line_label_handlers(prog, err, err_size) != 0) {
        return -1;
    }
    if (flat_native_use_validate_program(prog, err, err_size) != 0) {
        return -1;
    }
    if (flat_func_validate_program(prog, err, err_size) != 0) {
        return -1;
    }
    if (count_var_slots(prog, &var_slots, err, err_size) != 0) {
        return -1;
    }

    offs = calloc((size_t)prog->block_count, sizeof(BlockOff));
    block_ids = calloc((size_t)prog->block_count, sizeof(uint32_t));
    if (!offs || !block_ids) {
        free(offs);
        free(block_ids);
        snprintf(err, err_size, "out of memory");
        return -1;
    }

    stream_base = FLAT_FB_HEADER_SIZE + (size_t)prog->block_count * 8;
    {
        size_t cursor = stream_base;
        for (i = 0; i < prog->block_count; i++) {
            offs[i].abs_offset = cursor;
            block_ids[i] = (uint32_t)block_id_for(&prog->blocks[i]);
            cursor += measure_block(prog, i);
        }
    }

    for (i = 0; i < prog->block_count; i++) {
        if (emit_block_code(&code, prog, i, offs, stream_base, err, err_size) != 0) {
            free(offs);
            free(block_ids);
            free(code.buf);
            return -1;
        }
    }

    f = fopen(out_path, "wb");
    if (!f) {
        snprintf(err, err_size, "cannot write %s", out_path);
        free(offs);
        free(block_ids);
        free(code.buf);
        return -1;
    }
    {
        const uint8_t magic[4] = { 'F', 'L', 'A', 'T' };
        const uint8_t sentinel = FLAT_FB_SENTINEL;
        const uint8_t version = FLAT_FB_FORMAT_VERSION;
        uint16_t count = (uint16_t)prog->block_count;
        uint32_t labels_off = (uint32_t)(stream_base + code.len);
        uint32_t fnun_off = 0;
        uint32_t ffun_off = 0;
        uint32_t native_off = 0;
        uint32_t schm_off = 0;
        size_t label_sec = 6;
        size_t fnun_sz = 0;
        size_t ffun_sz = 0;
        size_t schm_sz = 0;
        int li;

        for (li = 0; li < prog->block_count; li++) {
            label_sec += 2 + strlen(prog->blocks[li].label);
        }
        if (prog->native_unit_count > 0) {
            fnun_off = (uint32_t)(labels_off + label_sec);
        }
        if (fnun_off != 0) {
            FlatFbNativeUnit *units_tmp = NULL;

            units_tmp = calloc((size_t)prog->native_unit_count, sizeof(FlatFbNativeUnit));
            if (units_tmp) {
                int ui;
                for (ui = 0; ui < prog->native_unit_count; ui++) {
                    units_tmp[ui].decl_count = prog->native_units[ui].decl_count;
                    memcpy(units_tmp[ui].decls, prog->native_units[ui].decls,
                           sizeof(units_tmp[ui].decls));
                }
                fnun_sz = flat_fb_fnun_section_size(prog->native_unit_count, units_tmp, count);
                free(units_tmp);
            }
        }
        ffun_sz = flat_fb_ffun_section_size((uint16_t)prog->function_count, NULL);
        if (prog->function_count > 0) {
            FlatFbFunctionEntry *ftab = NULL;
            int fi;

            ftab = calloc((size_t)prog->function_count, sizeof(FlatFbFunctionEntry));
            if (ftab) {
                for (fi = 0; fi < prog->function_count; fi++) {
                    const FgirFunction *fn = &prog->functions[fi];
                    int bi;

                    int pj;
                    snprintf(ftab[fi].name, sizeof ftab[fi].name, "%s", fn->name);
                    snprintf(ftab[fi].return_name, sizeof ftab[fi].return_name, "%s",
                             fn->return_name);
                    ftab[fi].param_count = (uint8_t)fn->param_count;
                    ftab[fi].func_vcx_slots = fn->func_vcx_slots;
                    ftab[fi].return_has_type = fn->return_has_type;
                    snprintf(ftab[fi].return_type_spec, sizeof ftab[fi].return_type_spec, "%s",
                             fn->return_type_spec);
                    for (pj = 0; pj < fn->param_count; pj++) {
                        ftab[fi].param_has_type[pj] = fn->param_has_type[pj];
                        snprintf(ftab[fi].param_type_spec[pj], sizeof ftab[fi].param_type_spec[pj],
                                 "%s", fn->param_type_spec[pj]);
                    }
                    for (bi = 0; bi < prog->block_count; bi++) {
                        if (strcmp(prog->blocks[bi].label, fn->impl_label) == 0) {
                            ftab[fi].impl_block_id = block_ids[bi];
                            break;
                        }
                    }
                    for (bi = 0; bi < prog->block_count; bi++) {
                        if (strcmp(prog->blocks[bi].label, fn->executor_label) == 0) {
                            ftab[fi].executor_block_id = block_ids[bi];
                            break;
                        }
                    }
                }
                ffun_sz = flat_fb_ffun_section_size((uint16_t)prog->function_count, ftab);
                free(ftab);
            }
        }
        if (fnun_off != 0) {
            ffun_off = (uint32_t)(fnun_off + fnun_sz);
        } else {
            ffun_off = (uint32_t)(labels_off + label_sec);
        }
        schm_sz = flat_fb_schm_section_size(&schema_catalog);
        schm_off = 0;
        if (prog->optional_native_count > 0) {
            native_off = (uint32_t)(ffun_off + ffun_sz + schm_sz);
        }
        fwrite(magic, 1, 4, f);
        fwrite(&sentinel, 1, 1, f);
        fwrite(&version, 1, 1, f);
        fwrite(&count, 1, 2, f);
        fwrite(&native_off, 1, 4, f);
        fwrite(&var_slots, 1, 4, f);
        fwrite(&labels_off, 1, 4, f);
        fwrite(&fnun_off, 1, 4, f);
        fwrite(&ffun_off, 1, 4, f);
        fwrite(&schm_off, 1, 4, f);
        for (i = 0; i < prog->block_count; i++) {
            uint32_t id = block_ids[i];
            uint32_t off = (uint32_t)offs[i].abs_offset;
            fwrite(&id, 1, 4, f);
            fwrite(&off, 1, 4, f);
        }
        fwrite(code.buf, 1, code.len, f);
        {
            char (*labels)[FGIR_MAX_LABEL] = NULL;
            labels = calloc((size_t)prog->block_count, FGIR_MAX_LABEL);
            if (!labels) {
                fclose(f);
                snprintf(err, err_size, "out of memory");
                free(offs);
                free(block_ids);
                free(code.buf);
                return -1;
            }
            for (li = 0; li < prog->block_count; li++) {
                snprintf(labels[li], FGIR_MAX_LABEL, "%s", prog->blocks[li].label);
            }
            if (flat_fb_write_label_section(f, count, labels) != 0) {
                free(labels);
                fclose(f);
                snprintf(err, err_size, "failed to write block labels");
                free(offs);
                free(block_ids);
                free(code.buf);
                return -1;
            }
            free(labels);
        }
        if (fnun_off != 0) {
            FlatFbNativeUnit *units = NULL;
            uint8_t *block_unit = NULL;
            int ui;

            units = calloc((size_t)prog->native_unit_count, sizeof(FlatFbNativeUnit));
            block_unit = malloc((size_t)prog->block_count);
            if (!units || !block_unit) {
                free(units);
                free(block_unit);
                fclose(f);
                snprintf(err, err_size, "out of memory");
                free(offs);
                free(block_ids);
                free(code.buf);
                return -1;
            }
            for (ui = 0; ui < prog->native_unit_count; ui++) {
                units[ui].decl_count = prog->native_units[ui].decl_count;
                memcpy(units[ui].decls, prog->native_units[ui].decls,
                       sizeof(units[ui].decls));
            }
            for (li = 0; li < prog->block_count; li++) {
                int ui2 = prog->blocks[li].native_unit_index;
                if (ui2 < 0 || ui2 >= prog->native_unit_count) {
                    ui2 = 0;
                }
                block_unit[li] = (uint8_t)ui2;
            }
            if (flat_fb_write_fnun_section(f, prog->native_unit_count, units, count,
                                           block_unit) != 0) {
                free(units);
                free(block_unit);
                fclose(f);
                snprintf(err, err_size, "failed to write native unit section");
                free(offs);
                free(block_ids);
                free(code.buf);
                return -1;
            }
            free(units);
            free(block_unit);
        }
        {
            FlatFbFunctionEntry *ftab = NULL;
            int fi;

            ftab = calloc((size_t)prog->function_count, sizeof(FlatFbFunctionEntry));
            if (!ftab && prog->function_count > 0) {
                fclose(f);
                snprintf(err, err_size, "out of memory");
                free(offs);
                free(block_ids);
                free(code.buf);
                return -1;
            }
            for (fi = 0; fi < prog->function_count; fi++) {
                const FgirFunction *fn = &prog->functions[fi];
                int bi;

                int pj;
                snprintf(ftab[fi].name, sizeof ftab[fi].name, "%s", fn->name);
                snprintf(ftab[fi].return_name, sizeof ftab[fi].return_name, "%s", fn->return_name);
                ftab[fi].param_count = (uint8_t)fn->param_count;
                ftab[fi].func_vcx_slots = fn->func_vcx_slots;
                ftab[fi].return_has_type = fn->return_has_type;
                snprintf(ftab[fi].return_type_spec, sizeof ftab[fi].return_type_spec, "%s",
                         fn->return_type_spec);
                for (pj = 0; pj < fn->param_count; pj++) {
                    ftab[fi].param_has_type[pj] = fn->param_has_type[pj];
                    snprintf(ftab[fi].param_type_spec[pj], sizeof ftab[fi].param_type_spec[pj],
                             "%s", fn->param_type_spec[pj]);
                }
                for (bi = 0; bi < prog->block_count; bi++) {
                    if (strcmp(prog->blocks[bi].label, fn->impl_label) == 0) {
                        ftab[fi].impl_block_id = block_ids[bi];
                    }
                    if (strcmp(prog->blocks[bi].label, fn->executor_label) == 0) {
                        ftab[fi].executor_block_id = block_ids[bi];
                    }
                }
            }
            if (flat_fb_write_ffun_section(f, (uint16_t)prog->function_count, ftab) != 0) {
                free(ftab);
                fclose(f);
                snprintf(err, err_size, "failed to write FFUN");
                free(offs);
                free(block_ids);
                free(code.buf);
                return -1;
            }
            free(ftab);
        }
        schm_off = (uint32_t)ftell(f);
        if (flat_fb_write_schm_section(f, &schema_catalog) != 0) {
            fclose(f);
            snprintf(err, err_size, "failed to write SCHM");
            free(offs);
            free(block_ids);
            free(code.buf);
            return -1;
        }
        if (fseek(f, 28, SEEK_SET) != 0 ||
            fwrite(&schm_off, 1, 4, f) != 4) {
            fclose(f);
            snprintf(err, err_size, "failed to patch SCHM offset");
            free(offs);
            free(block_ids);
            free(code.buf);
            return -1;
        }
        if (prog->optional_native_count > 0) {
            if (fseek(f, 0, SEEK_END) != 0) {
                fclose(f);
                snprintf(err, err_size, "failed to seek before FNAT");
                free(offs);
                free(block_ids);
                free(code.buf);
                return -1;
            }
            native_off = (uint32_t)ftell(f);
            if (fseek(f, 8, SEEK_SET) != 0 || fwrite(&native_off, 1, 4, f) != 4) {
                fclose(f);
                snprintf(err, err_size, "failed to patch native offset");
                free(offs);
                free(block_ids);
                free(code.buf);
                return -1;
            }
            fseek(f, 0, SEEK_END);
        }
        if (native_off != 0 && prog->optional_native_count > 0) {
            if (flat_fb_write_native_section(f, prog->optional_native_count,
                                             prog->optional_natives) != 0) {
                fclose(f);
                snprintf(err, err_size, "failed to write native manifest");
                free(offs);
                free(block_ids);
                free(code.buf);
                return -1;
            }
        }
    }
    fclose(f);
    free(offs);
    free(block_ids);
    free(code.buf);
    return 0;
}
