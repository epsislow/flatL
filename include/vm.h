#ifndef FLAT_VM_H
#define FLAT_VM_H

#include "fb_format.h"

typedef struct FlatVmOptions {
    int trace;
    int trace_vars;
} FlatVmOptions;

int flat_vm_run(const FlatBytecode *bc);
int flat_vm_run_opts(const FlatBytecode *bc, const FlatVmOptions *opts);

#endif
