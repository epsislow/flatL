#include "fgir.h"
#include "flat.h"
#include "flat_value.h"
#include "fb_format.h"
#include "flat_native.h"
#include "flat_native_use.h"
#include "fb_format.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef char VarName[64];

static int var_in_list(const char *name, char known[][64], int n) {
    int i;
    for (i = 0; i < n; i++) {
        if (strcmp(known[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int assign_record_lhs(const char *text, char known[][64], int *kn) {
    char buf[FGIR_MAX_LINE];
    char *eq;
    char *lhs;

    snprintf(buf, sizeof buf, "%s", text);
    eq = strchr(buf, '=');
    if (!eq || eq == buf) {
        return 0;
    }
    *eq = '\0';
    lhs = buf;
    while (*lhs && isspace((unsigned char)*lhs)) {
        lhs++;
    }
    {
        char *end = lhs + strlen(lhs);
        while (end > lhs && isspace((unsigned char)end[-1])) {
            end--;
        }
        *end = '\0';
    }
    if (lhs[0] == '\0' || strlen(lhs) > FLAT_VAR_NAME_MAX) {
        return 0;
    }
    if (*kn >= FLAT_MAX_VARS) {
        return -1;
    }
    if (!var_in_list(lhs, known, *kn)) {
        snprintf(known[*kn], 64, "%.*s", FLAT_VAR_NAME_MAX, lhs);
        (*kn)++;
    }
    return 0;
}

static int validate_free_in_program(const FgirProgram *prog, char *err, size_t err_size) {
    int bi, ii;
    VarName *known = malloc((size_t)FLAT_MAX_VARS * sizeof(VarName));

    if (!known) {
        snprintf(err, err_size, "out of memory");
        return -1;
    }
    for (bi = 0; bi < prog->block_count; bi++) {
        const FgirBlock *b = &prog->blocks[bi];
        int kn = 0;
        for (ii = 0; ii < b->instr_count; ii++) {
            const FgirInstr *ins = &b->instr[ii];
            if (ins->kind == FGIR_ASSIGN) {
                if (assign_record_lhs(ins->text, known, &kn) != 0) {
                    snprintf(err, err_size, "too many variables in block %s", b->label);
                    free(known);
                    return -1;
                }
            } else if (ins->kind == FGIR_IO && ins->call_out[0]) {
                if (!var_in_list(ins->call_out, known, kn)) {
                    if (kn >= FLAT_MAX_VARS) {
                        snprintf(err, err_size, "too many variables in block %s", b->label);
                        free(known);
                        return -1;
                    }
                    snprintf(known[kn], 64, "%s", ins->call_out);
                    kn++;
                }
            } else if (ins->kind == FGIR_FREE) {
                char var[64];
                if (flat_parse_free_varname(ins->text, var, sizeof var) != 0) {
                    snprintf(err, err_size, "invalid free: %s", ins->text);
                    free(known);
                    return -1;
                }
                if (!var_in_list(var, known, kn)) {
                    snprintf(err, err_size, "free of undeclared variable %s in %s", var, b->label);
                    free(known);
                    return -1;
                }
            }
        }
    }
    free(known);
    return 0;
}

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
    char *eq;
    char *lhs;
    char *end;

    snprintf(buf, sizeof buf, "%s", text);
    eq = strchr(buf, '=');
    if (!eq || eq == buf) {
        return 0;
    }
    *eq = '\0';
    lhs = buf;
    while (*lhs && isspace((unsigned char)*lhs)) {
        lhs++;
    }
    end = lhs + strlen(lhs);
    while (end > lhs && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    if (lhs[0] == '\0' || bind_name_forbidden(lhs)) {
        return 0;
    }
    return varset_add(vs, lhs);
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
                if (ins->call_out[0] && !bind_name_forbidden(ins->call_out)) {
                    rc = varset_add(&vs, ins->call_out);
                }
                break;
            case FGIR_FOREACH:
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

static void backtrack_label(const FgirBlock *b, int instr_index, char *out, size_t out_sz) {
    snprintf(out, out_sz, "#backtrack.%s.%d", b->label, instr_index);
}

static void panic_label(const FgirBlock *b, int instr_index, char *out, size_t out_sz) {
    snprintf(out, out_sz, "#panic.%s.%d", b->label, instr_index);
}

typedef struct {
    size_t abs_offset;
} BlockOff;

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
        case FGIR_ASSIGN: {
            uint8_t plen = (uint8_t)(strlen(ins->text) + 1);
            if (plen > 220) {
                snprintf(err, err_size, "assign too long");
                return -1;
            }
            if (emit_instr(code, OP_ASSIGN, ins->text, plen) != 0) {
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
        case FGIR_GUARD: {
            uint8_t payload[4 + FGIR_MAX_LINE];
            char bt[FGIR_MAX_LABEL];
            int bt_idx;
            uint32_t off32 = 0;
            uint8_t plen;
            backtrack_label(b, ins->index, bt, sizeof bt);
            bt_idx = find_block_idx(prog, bt);
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
        case FGIR_FOREACH: {
            uint8_t payload[4 + FGIR_MAX_LINE];
            int tgt = find_block_idx(prog, ins->foreach_block);
            uint32_t off32 = 0;
            uint8_t plen = (uint8_t)(4 + strlen(ins->text) + 1);
            if (tgt < 0) {
                snprintf(err, err_size, "unknown foreach block %s", ins->foreach_block);
                return -1;
            }
            off32 = (uint32_t)offs[tgt].abs_offset;
            memcpy(payload, &off32, 4);
            memcpy(payload + 4, ins->text, strlen(ins->text) + 1);
            if (emit_instr(code, OP_FOREACH, payload, plen) != 0) {
                return -1;
            }
            break;
        }
        case FGIR_IO: {
            uint8_t payload[2 + FGIR_MAX_LINE + 64];
            uint16_t native_id;
            char inner[FGIR_MAX_LINE];
            size_t il;
            size_t bl;
            size_t total;
            uint8_t plen;

            if (flat_native_split_call_prog(ins->text, prog, &native_id, inner, sizeof inner) != 0) {
                snprintf(err, err_size, "unknown native call: %s", ins->text);
                return -1;
            }
            il = strlen(inner) + 1;
            bl = strlen(ins->call_out) + 1;
            total = 2 + il + bl;
            if (total > sizeof payload || total > 255) {
                snprintf(err, err_size, "native payload too long");
                return -1;
            }
            payload[0] = (uint8_t)(native_id & 0xffu);
            payload[1] = (uint8_t)((native_id >> 8) & 0xffu);
            memcpy(payload + 2, inner, il);
            memcpy(payload + 2 + il, ins->call_out, bl);
            plen = (uint8_t)total;
            if (emit_instr(code, OP_NATIVE, payload, plen) != 0) {
                return -1;
            }
            break;
        }
        case FGIR_RAISE_PANIC: {
            uint8_t payload[4];
            char plab[FGIR_MAX_LABEL];
            int pidx;
            uint32_t off32 = 0;
            panic_label(b, ins->index, plab, sizeof plab);
            pidx = find_block_idx(prog, plab);
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

    if (!prog || !out_path) {
        return -1;
    }
    if (validate_free_in_program(prog, err, err_size) != 0) {
        return -1;
    }
    if (flat_native_use_validate_program(prog, err, err_size) != 0) {
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
        uint32_t native_off = 0;
        if (prog->optional_native_count > 0) {
            native_off = (uint32_t)(stream_base + code.len);
        }
        fwrite(magic, 1, 4, f);
        fwrite(&sentinel, 1, 1, f);
        fwrite(&version, 1, 1, f);
        fwrite(&count, 1, 2, f);
        fwrite(&native_off, 1, 4, f);
        fwrite(&var_slots, 1, 4, f);
        for (i = 0; i < prog->block_count; i++) {
            uint32_t id = block_ids[i];
            uint32_t off = (uint32_t)offs[i].abs_offset;
            fwrite(&id, 1, 4, f);
            fwrite(&off, 1, 4, f);
        }
        fwrite(code.buf, 1, code.len, f);
        if (native_off != 0) {
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
