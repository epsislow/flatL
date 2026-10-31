#ifndef FLAT_VM_H
#define FLAT_VM_H

#include "fb_format.h"

typedef enum FlatTermination {
    FLAT_TERM_UNKNOWN = 0,
    FLAT_TERM_OK = 1,
    FLAT_TERM_COMPLETE = 2,
    FLAT_TERM_FAIL = 3,
    FLAT_TERM_PANIC = 4,
    FLAT_TERM_ERROR = 5
} FlatTermination;

typedef struct FlatVmOptions {
    int trace;
    int trace_vars;
    int *out_termination;
} FlatVmOptions;

int flat_vm_run(const FlatBytecode *bc);
int flat_vm_run_opts(const FlatBytecode *bc, const FlatVmOptions *opts);

#endif
