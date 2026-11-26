#ifndef FLAT_VM_H
#define FLAT_VM_H

#include "fb_format.h"

#include <stdint.h>

struct FlatHostConfig;

typedef enum FlatTermination {
    FLAT_TERM_UNKNOWN = 0,
    FLAT_TERM_OK = 1,
    FLAT_TERM_COMPLETE = 2,
    FLAT_TERM_FAIL = 3,
    FLAT_TERM_PANIC = 4,
    FLAT_TERM_ERROR = 5,
    FLAT_TERM_CANCELLED = 6
} FlatTermination;

#define FLAT_VM_DEFAULT_MAX_JUMPS 1000u
#define FLAT_VM_DIAGNOSTIC_MAX 256u

typedef struct FlatVmOptions {
    int trace;
    int trace_vars;
    int *out_termination;
    char *out_panic_msg;     /* optional: last run diagnostic (NUL-terminated) */
    size_t out_panic_msg_sz; /* size of out_panic_msg buffer */
    uint64_t max_steps; /* 0 = unlimited total opcodes */
    uint64_t max_jumps; /* 0 = use FLAT_VM_DEFAULT_MAX_JUMPS */
    int no_jump_limit;  /* ignore jump budget (tests / long runs) */
    const struct FlatHostConfig *host_config; /* optional immutable host snapshot */
} FlatVmOptions;

/* Opaque, heap-stable VM owner with cooperative instruction-budget stepping. */
typedef struct FlatVm FlatVm;

typedef enum FlatVmState {
    FLAT_VM_READY = 1,
    FLAT_VM_RUNNING = 2,
    FLAT_VM_COMPLETED = 3,
    /* Compatibility name for callers that only need to recognize success. */
    FLAT_VM_TERMINAL = FLAT_VM_COMPLETED,
    FLAT_VM_FAILED = 4,
    FLAT_VM_YIELDED = 5,
    FLAT_VM_CANCELLED = 6
} FlatVmState;

typedef struct FlatVmResult {
    int exit_code;
    FlatTermination termination;
    FlatVmState state;
    char diagnostic[FLAT_VM_DIAGNOSTIC_MAX];
} FlatVmResult;

/* Copies bytecode, scalar options and host configuration. Caller retains no borrowed data. */
FlatVm *flat_vm_create(const FlatBytecode *bc, const FlatVmOptions *opts);
/* Executes one cooperative quantum. A zero budget means run until terminal.
 * Returns -1 for an invalid VM or a VM that is already terminal/running. */
int flat_vm_step(FlatVm *vm, uint64_t instruction_budget, FlatVmResult *result);
/* Compatibility helper: executes from READY to a terminal state in one call. */
int flat_vm_execute(FlatVm *vm, FlatVmResult *result);
/* Cancel a READY or YIELDED VM at a safe instruction boundary. */
int flat_vm_cancel(FlatVm *vm, FlatVmResult *result);
/* Destroys a ready, yielded, or terminal VM. Returns -1 while execution is active. */
int flat_vm_destroy(FlatVm *vm);
FlatVmState flat_vm_state(const FlatVm *vm);
/* Returns non-zero for completed or failed VMs. */
int flat_vm_is_terminal(const FlatVm *vm);

int flat_vm_run(const FlatBytecode *bc);
int flat_vm_run_opts(const FlatBytecode *bc, const FlatVmOptions *opts);

#endif
