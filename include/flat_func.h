#ifndef FLAT_FUNC_H
#define FLAT_FUNC_H

#include "fgir.h"

#include <stddef.h>

#define FLAT_FUNC_INSTR_VCX_FIXED 8u
#define FLAT_FUNC_RESERVED_INSTR_VAR "__call_instr_vcx"
#define FLAT_FUNC_RESERVED_CALLE_VAR "__call_calle_vcx"
#define FLAT_FUNC_DEFAULT_EXECUTOR "#fl_executor.unique.scope"
#define FLAT_FUNC_IMPL_PREFIX "#__fn."

int flat_func_meta_init(FgirProgram *prog);
int flat_func_parse_header_line(const char *line, FgirProgram *prog, const char *path, int line_no,
                                char *err, size_t err_size);
int flat_func_parse_def_header(const char *line, FgirFunction *fn, FgirProgram *prog, char *err,
                               size_t err_size);
int flat_func_finish_def(FgirFunction *fn, const FgirBlock *impl, int manual_slots,
                         char *err, size_t err_size);
int flat_func_try_classify_call(const char *line, const FgirProgram *prog, FgirInstr *ins);
int flat_func_reclassify_program(FgirProgram *prog);
int flat_func_prepare_fmod_unit(FgirProgram *mod, const char *import_as, char *err, size_t err_size);
int flat_func_lower_program(FgirProgram *prog, char *err, size_t err_size);
int flat_func_validate_program(const FgirProgram *prog, char *err, size_t err_size);
const FgirFunction *flat_func_find(const FgirProgram *prog, const char *name);
int flat_func_index(const FgirProgram *prog, const char *name);

#endif
