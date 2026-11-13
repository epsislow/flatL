#include "flat_native_tag.h"

#include "flat_native_use.h"
#include "flat_value.h"

#include <stdio.h>

static int validate_native_call_line(const FgirProgram *prog, int native_unit_index,
                                     const char *call_line, char *err, size_t err_sz) {
    char inner[FGIR_MAX_LINE];
    uint16_t native_id;
    char terr[64];

    terr[0] = '\0';
    if (flat_native_split_call_prog(call_line, prog, native_unit_index, &native_id, inner,
                                    sizeof inner, terr, sizeof terr) != 0) {
        return 0;
    }
    if (flat_native_tag_compile_check(native_id, inner, terr, sizeof terr) != 0) {
        snprintf(err, err_sz, "%s", terr[0] ? terr : "type.tag.arg");
        return -1;
    }
    return 0;
}

int flat_native_tag_validate_program(const FgirProgram *prog, char *err, size_t err_sz) {
    int bi, ii;

    if (!prog) {
        return -1;
    }
    for (bi = 0; bi < prog->block_count; bi++) {
        const FgirBlock *b = &prog->blocks[bi];
        for (ii = 0; ii < b->instr_count; ii++) {
            const FgirInstr *ins = &b->instr[ii];

            if (ins->kind == FGIR_IO) {
                if (validate_native_call_line(prog, b->native_unit_index, ins->text, err,
                                              err_sz) != 0) {
                    return -1;
                }
                continue;
            }
            if (ins->kind == FGIR_ASSIGN) {
                char line_buf[FGIR_MAX_LINE];
                char *bind;
                char *value;
                FlatAssignSplitForm form;
                int is_const = 0;

                snprintf(line_buf, sizeof line_buf, "%s", ins->text);
                if (flat_assign_strip_const_prefix(line_buf, &is_const) != 0) {
                    continue;
                }
                if (flat_assign_split_line(line_buf, &bind, &value, &form) != 0) {
                    continue;
                }
                if (!flat_native_is_call_line(value)) {
                    continue;
                }
                if (validate_native_call_line(prog, b->native_unit_index, value, err, err_sz) !=
                    0) {
                    return -1;
                }
            }
        }
    }
    return 0;
}
