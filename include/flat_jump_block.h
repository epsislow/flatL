#ifndef FLAT_JUMP_BLOCK_H
#define FLAT_JUMP_BLOCK_H

#include <stddef.h>
#include <stdint.h>

#define FLAT_JUMP_BLOCK_MAX_KW "jump:blockMax"

/* Header line: jump:blockMax N. Returns 1 if parsed, 0 if not this directive, -1 on error. */
int flat_parse_jump_block_max_directive(const char *line, uint32_t *out_n, char *err, size_t err_sz);

/*
 * Block header line: strip optional trailing "jump:blockMax N".
 * out_override_set: 0 = absent (inherit), 1 = present (value in out_override).
 */
int flat_block_header_strip_jump_block_max(char *line, int *out_override_set, uint32_t *out_override,
                                           char *err, size_t err_sz);

#endif
