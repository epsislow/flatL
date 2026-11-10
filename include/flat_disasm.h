#ifndef FLAT_DISASM_H
#define FLAT_DISASM_H

#include "fb_format.h"

#include <stdio.h>

typedef struct {
    int summary;
    int sections_only;
    int no_ffun;
    int no_natives;
    int block_id; /* -1 = all blocks */
    char block_label[FGIR_MAX_LABEL]; /* optional #label filter */
} FlatDisasmOpts;

void flat_disasm_opts_init(FlatDisasmOpts *opts);

/* Disassemble loaded bytecode to out. Returns 0 on success. */
int flat_disasm_print(const FlatBytecode *bc, FILE *out, const FlatDisasmOpts *opts);

/* Load path and disassemble to out. */
int flat_disasm_file(const char *path, FILE *out, const FlatDisasmOpts *opts);

int flat_disasm_usage_fbdump(const char *prog);

/* Parse flags from argv[start..argc-1]; sets *path_out to first non-option. Returns 0, 1 for -h, 2 for error. */
int flat_disasm_parse_args(int argc, char **argv, int start, FlatDisasmOpts *opts,
                            const char **path_out);

#endif
