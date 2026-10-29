#include "fgir.h"
#include "flat.h"
#include "flat_native.h"
#include "flat_native_use.h"
#include "fb_format.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    size_t stream_base;
    FILE *f;
    int i;

    if (!prog || !out_path) {
        return -1;
    }
    if (flat_native_use_validate_program(prog, err, err_size) != 0) {
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

    stream_base = 4 + 2 + (size_t)prog->block_count * 8;
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
        uint16_t count = (uint16_t)prog->block_count;
        fwrite(magic, 1, 4, f);
        fwrite(&count, 1, 2, f);
        for (i = 0; i < prog->block_count; i++) {
            uint32_t id = block_ids[i];
            uint32_t off = (uint32_t)offs[i].abs_offset;
            fwrite(&id, 1, 4, f);
            fwrite(&off, 1, 4, f);
        }
        fwrite(code.buf, 1, code.len, f);
    }
    fclose(f);
    free(offs);
    free(block_ids);
    free(code.buf);
    return 0;
}
