#ifndef FLAT_FGIR_H
#define FLAT_FGIR_H

#include <stddef.h>
#include <stdio.h>

#define FGIR_MAX_LABEL 128
#define FGIR_MAX_LINE 512
#define FGIR_MAX_BLOCKS 64
#define FGIR_MAX_INSTR 128
#define FGIR_MAX_PARAMS 8

typedef enum {
    FGIR_OK,
    FGIR_FAIL,
    FGIR_RETURN,
    FGIR_ASSIGN,
    FGIR_GUARD,
    FGIR_CALL,
    FGIR_FOREACH,
    FGIR_RAISE_PANIC
} FgirInstrKind;

typedef struct {
    FgirInstrKind kind;
    char text[FGIR_MAX_LINE];
    int index; /* 1-based within block */
    char call_target[FGIR_MAX_LABEL];
    char call_out[64];
    char foreach_item[64];
    char foreach_block[FGIR_MAX_LABEL];
} FgirInstr;

typedef struct {
    char label[FGIR_MAX_LABEL];
    char params[FGIR_MAX_PARAMS][64];
    int param_count;
    FgirInstr instr[FGIR_MAX_INSTR];
    int instr_count;
} FgirBlock;

typedef struct {
    FgirBlock blocks[FGIR_MAX_BLOCKS];
    int block_count;
    int main_index;
} FgirProgram;

int flat_parse_file(const char *path, FgirProgram *out, char *err, size_t err_size);
void flat_fgir_dump(const FgirProgram *prog, FILE *out);
int flat_compile_fgir(const FgirProgram *prog, const char *out_path, char *err, size_t err_size);

#endif
