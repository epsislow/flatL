#ifndef FLAT_VM_H
#define FLAT_VM_H

#include "fb_format.h"

#include <stdint.h>

typedef enum FlatTermination {
    FLAT_TERM_UNKNOWN = 0,
    FLAT_TERM_OK = 1,
    FLAT_TERM_COMPLETE = 2,
    FLAT_TERM_FAIL = 3,
    FLAT_TERM_PANIC = 4,
    FLAT_TERM_ERROR = 5
} FlatTermination;

#define FLAT_VM_DEFAULT_MAX_JUMPS 1000u

typedef struct FlatVmOptions {
    int trace;
    int trace_vars;
    int *out_termination;
    char *out_panic_msg;     /* optional: last run diagnostic (NUL-terminated) */
    size_t out_panic_msg_sz; /* size of out_panic_msg buffer */
    uint64_t max_steps; /* 0 = unlimited total opcodes */
    uint64_t max_jumps; /* 0 = use FLAT_VM_DEFAULT_MAX_JUMPS */
    int no_jump_limit;  /* ignore jump budget (tests / long runs) */
} FlatVmOptions;

int flat_vm_run(const FlatBytecode *bc);
int flat_vm_run_opts(const FlatBytecode *bc, const FlatVmOptions *opts);

#endif
