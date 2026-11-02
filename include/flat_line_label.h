#ifndef FLAT_LINE_LABEL_H
#define FLAT_LINE_LABEL_H

#include "fgir.h"

#include <stddef.h>

/* Strip optional EOL ` #.ident` from line (mutates line). Returns 1 if label found. */
int flat_line_label_strip_eol(char *line, char *label_out, size_t label_sz, const char *path,
                              int line_no, char *err, size_t err_size);

/* Per-unit: duplicate (block, ident) → error. */
int flat_line_label_validate_unit(const FgirProgram *unit, const char *path, char *err,
                                  size_t err_size);

/* Instruction index (1-based) for `#block` + line ident, or -1. */
int flat_line_label_instr_index(const FgirProgram *prog, const char *block_label,
                                const char *line_ident);

/* True if suffix is a 1-based decimal instruction index (e.g. "23"). */
int flat_line_label_suffix_is_index(const char *suffix);

/* Resolve handler label `#or.#B.suffix` or `#panic.#B.suffix`. */
int flat_line_label_resolve_handler(const FgirProgram *prog, const char *handler_label,
                                    char *canonical_out, size_t canonical_sz, char *err,
                                    size_t err_size);

#endif
