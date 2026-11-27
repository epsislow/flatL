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
    free(prog->functions);
    free(prog->schemas);
    free(prog->native_units);
    free(prog->exports);
    free(prog->optional_natives);
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


int flat_fgir_reserve_functions(FgirProgram *prog, size_t capacity) {
    FgirFunction *next;
    size_t grown;
    if (!prog || capacity > FGIR_MAX_FUNCTIONS || prog->function_capacity < 0) return -1;
    if (capacity <= (size_t)prog->function_capacity) return 0;
    if (grow_capacity((size_t)prog->function_capacity, capacity, FGIR_MAX_FUNCTIONS, &grown) != 0 ||
        grown > SIZE_MAX / sizeof(*next)) return -1;
    next = (FgirFunction *)realloc(prog->functions, grown * sizeof(*next));
    if (!next) return -1;
    memset(next + prog->function_capacity, 0,
           (grown - (size_t)prog->function_capacity) * sizeof(*next));
    prog->functions = next;
    prog->function_capacity = (int)grown;
    return 0;
}

int flat_fgir_reserve_native_units(FgirProgram *prog, size_t capacity) {
    FgirNativeUnit *next;
    size_t grown;
    size_t current;
    if (!prog || capacity > FGIR_MAX_NATIVE_UNITS || prog->native_unit_capacity < 0) return -1;
    current = (size_t)prog->native_unit_capacity;
    if (capacity <= current) return 0;
    /* Native-unit records are relatively large (4 KiB); allocate the first
     * slot on demand instead of reserving eight records up front. */
    grown = current ? current : 1u;
    while (grown < capacity) {
        if (grown > FGIR_MAX_NATIVE_UNITS / 2u) {
            grown = FGIR_MAX_NATIVE_UNITS;
            break;
        }
        grown *= 2u;
    }
    if (grown < capacity || grown > SIZE_MAX / sizeof(*next)) return -1;
    next = (FgirNativeUnit *)realloc(prog->native_units, grown * sizeof(*next));
    if (!next) return -1;
    memset(next + current, 0, (grown - current) * sizeof(*next));
    prog->native_units = next;
    prog->native_unit_capacity = (int)grown;
    return 0;
}


int flat_fgir_reserve_exports(FgirProgram *prog, size_t capacity) {
    char (*next)[FGIR_MAX_FUNC_NAME];
    size_t current;
    size_t grown;

    if (!prog || capacity > FGIR_MAX_FUNCTIONS || prog->export_capacity < 0) return -1;
    current = (size_t)prog->export_capacity;
    if (capacity <= current) return 0;

    /* Export records are small and most modules expose only a few symbols. */
    grown = current ? current : 1u;
    while (grown < capacity) {
        if (grown > FGIR_MAX_FUNCTIONS / 2u) {
            grown = FGIR_MAX_FUNCTIONS;
            break;
        }
        grown *= 2u;
    }
    if (grown < capacity || grown > SIZE_MAX / sizeof(*next)) return -1;
    next = (char (*)[FGIR_MAX_FUNC_NAME])realloc(prog->exports, grown * sizeof(*next));
    if (!next) return -1;
    memset(next + current, 0, (grown - current) * sizeof(*next));
    prog->exports = next;
    prog->export_capacity = (int)grown;
    return 0;
}

int flat_fgir_reserve_optional_natives(FgirProgram *prog, size_t capacity) {
    FlatOptionalNativeDecl *next;
    size_t current;
    size_t grown;

    if (!prog || capacity > FGIR_MAX_OPTIONAL_NATIVES || prog->optional_native_capacity < 0) {
        return -1;
    }
    current = (size_t)prog->optional_native_capacity;
    if (capacity <= current) return 0;

    /* Optional-native records are fixed-width; allocate only when the first
     * unique non-core module is added and grow geometrically up to 32. */
    grown = current ? current : 1u;
    while (grown < capacity) {
        if (grown > FGIR_MAX_OPTIONAL_NATIVES / 2u) {
            grown = FGIR_MAX_OPTIONAL_NATIVES;
            break;
        }
        grown *= 2u;
    }
    if (grown < capacity || grown > SIZE_MAX / sizeof(*next)) return -1;
    next = (FlatOptionalNativeDecl *)realloc(prog->optional_natives, grown * sizeof(*next));
    if (!next) return -1;
    memset(next + current, 0, (grown - current) * sizeof(*next));
    prog->optional_natives = next;
    prog->optional_native_capacity = (int)grown;
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
