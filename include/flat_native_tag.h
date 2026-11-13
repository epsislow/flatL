#ifndef FLAT_NATIVE_TAG_H
#define FLAT_NATIVE_TAG_H

#include "flat_native.h"
#include "flat_slot_type.h"
#include "fgir.h"

#include <stddef.h>
#include <stdint.h>

/* Split inner call args at depth-0 ';' into operands (left) and tag expression (right).
 * Mutates buf. *operands always set. If no ';', *has_tag_section=0 and *tag_expr=NULL.
 * If ';' present, *has_tag_section=1 and *tag_expr points into buf (may be empty string). */
void flat_native_split_operands_tag(char *buf, char **operands, char **tag_expr, int *has_tag_section);

/* Compile-time checks on tag section for native call inner (after dotted-call extract). */
int flat_native_tag_compile_check(uint16_t native_id, const char *inner, char *err, size_t err_sz);

/*
 * Runtime: resolve optional type tag into FlatSlotType.
 * has_tag=0 if no tag section or empty tag_set after eval.
 * Returns 0 on success; on failure sets ctx->panic_detail (type.tag.*).
 */
int flat_native_tag_resolve_type(FlatNativeCtx *ctx, uint16_t native_id, FlatSlotType *out_ty,
                                int *has_tag);

/* If tag section has tokens but native does not accept tags -> not_accepted (runtime). */
int flat_native_tag_reject_untagged_native(FlatNativeCtx *ctx, uint16_t native_id);

int flat_native_native_accepts_tags(uint16_t native_id);

int flat_native_tag_validate_program(const FgirProgram *prog, char *err, size_t err_sz);

#endif
