#ifndef FLAT_VM_INTERNAL_H
#define FLAT_VM_INTERNAL_H

#include "fb_format.h"
#include "vm.h"
#include "flat_task_native.h"
#include "flat_file.h"

/* Private bridge between the public FlatVm handle and the VM implementation. */
void *flat_vm_state_create(const FlatBytecode *bc, const FlatVmOptions *opts);
void *flat_vm_state_create_at_label(const FlatBytecode *bc, const FlatVmOptions *opts,
                                    const char *entry_label);
int flat_vm_state_set_task_bridge(void *state, const FlatTaskNativeBridge *bridge);
/* Load and validate every optional native declared by this VM before worker-mode freeze. */
int flat_vm_worker_preflight(FlatVm *vm, char *err, size_t err_size);
int flat_vm_state_set_initial_args(void *state, const FlatValue *args, size_t arg_count,
                                   FlatCollectionPool *source_coll, FlatStringPool *source_pool,
                                   FlatFileTable *source_files);
/* Internal return sentinel: execution yielded without tearing down VM resources. */
#define FLAT_VM_STATE_YIELDED (-1000)
#define FLAT_VM_STATE_TASK_WAITING (-1001)
int flat_vm_state_step(void *state, const FlatVmOptions *opts, uint64_t instruction_budget);
int flat_vm_state_execute(void *state, const FlatVmOptions *opts);
void flat_vm_state_destroy(void *state);
FlatVm *flat_vm_create_at_label(const FlatBytecode *bc, const FlatVmOptions *opts,
                                const char *entry_label);
int flat_vm_set_task_bridge(FlatVm *vm, const FlatTaskNativeBridge *bridge);
int flat_vm_set_initial_args(FlatVm *vm, const FlatValue *args, size_t arg_count,
                             FlatCollectionPool *source_coll, FlatStringPool *source_pool,
                             FlatFileTable *source_files);

#endif
