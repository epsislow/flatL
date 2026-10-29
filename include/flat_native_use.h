#ifndef FLAT_NATIVE_USE_H
#define FLAT_NATIVE_USE_H

#include "fgir.h"

#include <stdint.h>

int flat_native_use_scan_file(const char *path, FgirProgram *acc, char *err, size_t err_size);
int flat_native_use_merge(FgirProgram *acc, const FlatOptionalNativeDecl *one, char *err,
                          size_t err_size);
int flat_native_use_validate_program(const FgirProgram *prog, char *err, size_t err_size);
int flat_native_resolve_module_name(const char *mod_in, const FgirProgram *prog, char *mod_out,
                                    size_t mod_out_sz);
int flat_native_split_call_prog(const char *line, const FgirProgram *prog, uint16_t *out_id,
                                char *inner, size_t inner_max);

#endif
