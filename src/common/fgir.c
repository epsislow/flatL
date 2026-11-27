#include "fgir.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void flat_fgir_free(FgirProgram *prog) {
    int i;
    if (!prog) return;
    for (i = 0; i < prog->block_count; i++) {
        free(prog->blocks[i].instr);
        prog->blocks[i].instr = NULL;
        prog->blocks[i].instr_count = 0;
        prog->blocks[i].instr_capacity = 0;
    }
    free(prog->blocks);
    free(prog->schemas);
    memset(prog, 0, sizeof(*prog));
    prog->main_index = -1;
    prog->func_var_slots_manual = -1;
}

static int grow_capacity(size_t current, size_t needed, size_t maximum, size_t *out) {
    size_t next = current ? current : 8u;
    if (!out || needed > maximum) return -1;
    while (next < needed) {
        if (next > maximum / 2u) {
            next = maximum;
            break;
        }
        next *= 2u;
    }
    if (next < needed || next > SIZE_MAX / sizeof(void *)) return -1;
    *out = next;
    return 0;
}

int flat_fgir_reserve_blocks(FgirProgram *prog, size_t capacity) {
    FgirBlock *next;
    size_t grown;
    if (!prog || capacity > FGIR_MAX_BLOCKS) return -1;
    if (capacity <= (size_t)prog->block_capacity) return 0;
    if (grow_capacity((size_t)prog->block_capacity, capacity, FGIR_MAX_BLOCKS, &grown) != 0 ||
        grown > SIZE_MAX / sizeof(*next)) return -1;
    next = (FgirBlock *)realloc(prog->blocks, grown * sizeof(*next));
    if (!next) return -1;
    memset(next + prog->block_capacity, 0,
           (grown - (size_t)prog->block_capacity) * sizeof(*next));
    prog->blocks = next;
    prog->block_capacity = (int)grown;
    return 0;
}

int flat_fgir_reserve_instr(FgirBlock *block, size_t capacity) {
    FgirInstr *next;
    size_t grown;
    if (!block || capacity > FGIR_MAX_INSTR) return -1;
    if (capacity <= (size_t)block->instr_capacity) return 0;
    if (grow_capacity((size_t)block->instr_capacity, capacity, FGIR_MAX_INSTR, &grown) != 0 ||
        grown > SIZE_MAX / sizeof(*next)) return -1;
    next = (FgirInstr *)realloc(block->instr, grown * sizeof(*next));
    if (!next) return -1;
    memset(next + block->instr_capacity, 0,
           (grown - (size_t)block->instr_capacity) * sizeof(*next));
    block->instr = next;
    block->instr_capacity = (int)grown;
    return 0;
}


int flat_fgir_reserve_schemas(FgirProgram *prog, size_t capacity) {
    FgirSchemaDecl *next;
    size_t grown;
    if (!prog || capacity > FGIR_SCHEMA_MAX || prog->schema_capacity < 0) return -1;
    if (capacity <= (size_t)prog->schema_capacity) return 0;
    if (grow_capacity((size_t)prog->schema_capacity, capacity, FGIR_SCHEMA_MAX, &grown) != 0 ||
        grown > SIZE_MAX / sizeof(*next)) return -1;
    next = (FgirSchemaDecl *)realloc(prog->schemas, grown * sizeof(*next));
    if (!next) return -1;
    memset(next + prog->schema_capacity, 0,
           (grown - (size_t)prog->schema_capacity) * sizeof(*next));
    prog->schemas = next;
    prog->schema_capacity = (int)grown;
    return 0;
}

int flat_fgir_block_clone(FgirBlock *dst, const FgirBlock *src) {
    if (!dst || !src || src->instr_count < 0 || src->instr_count > FGIR_MAX_INSTR) return -1;
    memset(dst, 0, sizeof(*dst));
    memcpy(dst->label, src->label, sizeof(dst->label));
    memcpy(dst->params, src->params, sizeof(dst->params));
    dst->param_count = src->param_count;
    dst->native_unit_index = src->native_unit_index;
    dst->jump_block_max_override = src->jump_block_max_override;
    if (src->instr_count > 0) {
        if (flat_fgir_reserve_instr(dst, (size_t)src->instr_count) != 0) return -1;
        memcpy(dst->instr, src->instr, (size_t)src->instr_count * sizeof(*dst->instr));
        dst->instr_count = src->instr_count;
    }
    return 0;
}
