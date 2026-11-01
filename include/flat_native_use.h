#ifndef FLAT_NATIVE_USE_H
#define FLAT_NATIVE_USE_H

#include "fgir.h"

#include <stdint.h>

int flat_native_use_scan_file(const char *path, FlatOptionalNativeDecl *decls, int decl_max,
                              int *decl_count_out, char *err, size_t err_size);
int flat_native_use_add_unit(FgirProgram *prog, const FlatOptionalNativeDecl *decls, int decl_count,
                             int *unit_index_out, char *err, size_t err_size);
int flat_native_use_build_fnat_union(FgirProgram *prog);
int flat_native_use_validate_program(const FgirProgram *prog, char *err, size_t err_size);
int flat_native_resolve_module_name(const char *mod_in, const FlatOptionalNativeDecl *decls,
                                    int decl_count, char *mod_out, size_t mod_out_sz);
int flat_native_split_call_decls(const char *line, const FlatOptionalNativeDecl *decls,
                                 int decl_count, uint16_t *out_id, char *inner, size_t inner_max,
                                 char *err, size_t err_size);
int flat_native_split_call_prog(const char *line, const FgirProgram *prog, int native_unit_index,
                                uint16_t *out_id, char *inner, size_t inner_max, char *err,
                                size_t err_size);
#endif
