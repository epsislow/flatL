#ifndef FLAT_FUNC_TAG_H
#define FLAT_FUNC_TAG_H

#include "fb_format.h"
#include "fgir.h"

#include <stddef.h>

#define FLAT_FUNC_CALL_FI_DYNAMIC 0xffffu

/* Canonical tag key from const literal tagExpr (def or call). User defs accept any tokens. */
int flat_func_tag_canonical_from_literal(const char *tag_expr, char *canonical, size_t canon_sz,
                                         char *err, size_t err_sz);
int flat_func_tag_canonical_from_runtime_string(const char *spec, char *canonical, size_t canon_sz);

int flat_func_tag_key_empty(int has_tag_section, const char *canonical);

int flat_func_overload_key_equal(const FgirFunction *a, const FgirFunction *b);

int flat_func_check_new_def_overload(const FgirProgram *prog, int new_ix, char *err, size_t err_sz);

int flat_func_name_used(const FgirProgram *prog, const char *name);

int flat_func_resolve_call_index(const FgirProgram *prog, const char *name, int argc,
                                 int call_has_tag, const char *call_tag_expr, int *runtime_dynamic,
                                 char *err, size_t err_sz);

int flat_func_resolve_call_sites(FgirProgram *prog, char *err, size_t err_sz);

int flat_func_tag_validate_program(const FgirProgram *prog, char *err, size_t err_sz);

uint16_t flat_fb_resolve_function_index(const FlatBytecode *bc, const char *name, uint8_t argc,
                                        int call_has_tag, const char *tag_canonical,
                                        char *panic, size_t panic_sz);

#endif
