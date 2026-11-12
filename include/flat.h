#ifndef FLAT_H
#define FLAT_H

#define FLAT_MAGIC "FLAT"
#define FLAT_TOOLCHAIN_VERSION "0.4.0"

typedef enum {
    OP_ASSIGN  = 0x01,
    OP_GUARD   = 0x02,
    OP_CALL    = 0x03,
    OP_RETURN  = 0x04,
    OP_FOREACH = 0x05,
    OP_OK      = 0x06,
    OP_FAIL    = 0x07,
    OP_PANIC   = 0x08,
    OP_IO      = 0x09, /* legacy: full call text + bind */
    OP_NATIVE  = 0x0A, /* uint16 id + inner\\0 + bind\\0 */
    OP_FREE    = 0x0B, /* var name\\0 */
    OP_FOR     = 0x0C, /* uint32 loop block IP + original for(...) line NUL-terminated */
    OP_JUMP          = 0x0D, /* uint32 target block IP */
    OP_CALL_BLOCK    = 0x0E, /* block id expr NUL + bind name NUL (may be empty) */
    OP_NEXT          = 0x0F, /* next for/for-each iteration (was OP_RETURN in loop body) */
    OP_CALL_FUNCTION = 0x10, /* u16 func_index + u8 argc + u8 global + ret\\0 + formal names\\0… */
    OP_PANIC_RESET   = 0x11,
    OP_PANIC_REASON  = 0x12  /* bind var name NUL-terminated */
} OpCode;

/* Line modifiers on native / assign-native lines (see SPECIFICATION.md). */
#define FLAT_LINE_MOD_NONE  0
#define FLAT_LINE_MOD_Q     1
#define FLAT_LINE_MOD_TILDE 2

/* OP_NATIVE payload: uint16 id + inner\\0 + bind\\0 + mod u8 + or_ip u32 (or_ip for ~ only) */
/* OP_ASSIGN payload: mod u8 + assign line\\0 */

#endif
