#ifndef FLAT_H
#define FLAT_H

#define FLAT_MAGIC "FLAT"
#define FLAT_TOOLCHAIN_VERSION "0.2.0"

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
    OP_FREE    = 0x0B  /* var name\\0 */
} OpCode;

#endif
