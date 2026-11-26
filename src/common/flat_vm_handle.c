#include "vm.h"
#include "flat_host_config.h"
#include "flat_vm_internal.h"
#include "flat_task_native.h"
#include "flat_native.h"
#include "flat_native_plugin.h"
#include "flat_native_capability.h"
#include "flat_http_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct FlatVm {
    FlatBytecode bytecode;
    FlatHostConfig host;
    FlatVmOptions options;
    FlatVmState state;
    FlatVmResult result;
    void *execution_state;
    FlatTaskNativeBridge task_bridge;
};

static FlatVm *flat_vm_create_internal(const FlatBytecode *bc, const FlatVmOptions *opts,
                                       const char *entry_label) {
    FlatVm *vm;
    if (!bc || !bc->data || bc->size == 0 || bc->block_count == 0) return NULL;
    vm = (FlatVm *)calloc(1, sizeof *vm);
    if (!vm) return NULL;
    if (flat_fb_load_buffer(bc->data, bc->size, &vm->bytecode) != 0) {
        free(vm);
        return NULL;
    }
    if (opts) {
        vm->options.trace = opts->trace;
        vm->options.trace_vars = opts->trace_vars;
        vm->options.max_steps = opts->max_steps;
        vm->options.max_jumps = opts->max_jumps;
        vm->options.no_jump_limit = opts->no_jump_limit;
    }
    if (opts && opts->host_config) {
        if (flat_env_context_clone(&vm->host.env, &opts->host_config->env) != 0) {
            flat_fb_free(&vm->bytecode);
            free(vm);
            return NULL;
        }
        vm->host.path = opts->host_config->path;
        if (vm->host.path.sep != '/' && vm->host.path.sep != '\\') {
            flat_host_config_destroy(&vm->host);
            flat_fb_free(&vm->bytecode);
            free(vm);
            return NULL;
        }
    } else if (flat_host_config_capture_staging(&vm->host) != 0) {
        flat_host_config_destroy(&vm->host);
        flat_fb_free(&vm->bytecode);
        free(vm);
        return NULL;
    }
    {
        FlatVmOptions init_opts = vm->options;
        init_opts.host_config = &vm->host;
        vm->execution_state = flat_vm_state_create_at_label(&vm->bytecode, &init_opts, entry_label);
        if (!vm->execution_state) {
            flat_host_config_destroy(&vm->host);
            flat_fb_free(&vm->bytecode);
            free(vm);
            return NULL;
        }
    }
    vm->state = FLAT_VM_READY;
    vm->result.state = FLAT_VM_READY;
    vm->result.termination = FLAT_TERM_UNKNOWN;
    return vm;
}

FlatVm *flat_vm_create(const FlatBytecode *bc, const FlatVmOptions *opts) {
    return flat_vm_create_internal(bc, opts, NULL);
}

FlatVm *flat_vm_create_at_label(const FlatBytecode *bc, const FlatVmOptions *opts,
                                const char *entry_label) {
    if (!entry_label || !entry_label[0]) return NULL;
    return flat_vm_create_internal(bc, opts, entry_label);
}

int flat_vm_worker_preflight(FlatVm *vm, char *err, size_t err_size) {
    FlatNativeRuntimeContext context;
    int i;
    if (err && err_size) err[0] = '\0';
    if (!vm) {
        if (err && err_size) snprintf(err, err_size, "invalid VM worker preflight");
        return -1;
    }
    context.manifest = vm->bytecode.optional_natives;
    context.manifest_count = vm->bytecode.optional_native_count;
    for (i = 0; i < context.manifest_count; ++i) {
        char module[FGIR_NATIVE_MODULE_LEN];
        int id;
        int found = 0;
        for (id = 0; id < FLAT_NATIVE_COUNT; ++id) {
            if (flat_native_module_for_id((uint16_t)id, module, sizeof module) == 0 &&
                strcmp(module, context.manifest[i].module) == 0) {
                if (flat_native_runtime_prepare(&context, (uint16_t)id, err, err_size) != 0)
                    return -1;
                found = 1;
                break;
            }
        }
        if (!found) {
            if (err && err_size) snprintf(err, err_size, "unknown native module %s in VM manifest",
                                          context.manifest[i].module);
            return -1;
        }
        {
            FlatNativeThreadCapability policy =
                flat_native_module_worker_policy(context.manifest[i].module);
            if (policy != FLAT_NATIVE_THREAD_CAP_THREAD_SAFE &&
                policy != FLAT_NATIVE_THREAD_CAP_SERIALIZED) {
                if (err && err_size)
                    snprintf(err, err_size, "native module %s is unsupported in worker mode",
                             context.manifest[i].module);
                return -1;
            }
        }
        if (strcmp(context.manifest[i].module, "http") == 0 && flat_http_prepare() != 0) {
            if (err && err_size) snprintf(err, err_size, "HTTP transport initialization failed");
            return -1;
        }
    }
    return 0;
}

int flat_vm_set_task_bridge(FlatVm *vm, const FlatTaskNativeBridge *bridge) {
    if (!vm || !bridge || vm->state != FLAT_VM_READY) return -1;
    vm->task_bridge = *bridge;
    return flat_vm_state_set_task_bridge(vm->execution_state, &vm->task_bridge);
}

int flat_vm_set_initial_args(FlatVm *vm, const FlatValue *args, size_t arg_count,
                             FlatCollectionPool *source_coll, FlatStringPool *source_pool,
                             FlatFileTable *source_files) {
    if (!vm || vm->state != FLAT_VM_READY) return -1;
    return flat_vm_state_set_initial_args(vm->execution_state, args, arg_count,
                                          source_coll, source_pool, source_files);
}

int flat_vm_step(FlatVm *vm, uint64_t instruction_budget, FlatVmResult *result) {
    FlatVmOptions run_opts;
    int rc;
    int termination = FLAT_TERM_UNKNOWN;

    if (!vm || (vm->state != FLAT_VM_READY && vm->state != FLAT_VM_YIELDED)) return -1;
    vm->state = FLAT_VM_RUNNING;
    run_opts = vm->options;
    run_opts.host_config = &vm->host;
    run_opts.out_termination = &termination;
    run_opts.out_panic_msg = vm->result.diagnostic;
    run_opts.out_panic_msg_sz = sizeof vm->result.diagnostic;
    vm->result.diagnostic[0] = '\0';

    rc = flat_vm_state_step(vm->execution_state, &run_opts, instruction_budget);
    if (rc == FLAT_VM_STATE_YIELDED || rc == FLAT_VM_STATE_TASK_WAITING) {
        vm->state = FLAT_VM_YIELDED;
        vm->result.exit_code = 0;
        vm->result.termination = FLAT_TERM_UNKNOWN;
        vm->result.state = FLAT_VM_YIELDED;
        vm->result.diagnostic[0] = '\0';
        if (result) *result = vm->result;
        return 0;
    }

    vm->result.exit_code = rc;
    vm->result.termination = (FlatTermination)termination;
    vm->state = rc == 0 ? FLAT_VM_COMPLETED : FLAT_VM_FAILED;
    vm->result.state = vm->state;
    if (result) *result = vm->result;
    return rc;
}

int flat_vm_execute(FlatVm *vm, FlatVmResult *result) {
    /* A zero instruction budget preserves the historical run-to-completion API. */
    if (!vm || vm->state != FLAT_VM_READY) return -1;
    return flat_vm_step(vm, 0, result);
}

int flat_vm_cancel(FlatVm *vm, FlatVmResult *result) {
    if (!vm || (vm->state != FLAT_VM_READY && vm->state != FLAT_VM_YIELDED)) return -1;
    if (vm->execution_state) {
        flat_vm_state_destroy(vm->execution_state);
        vm->execution_state = NULL;
    }
    vm->state = FLAT_VM_CANCELLED;
    vm->result.exit_code = 1;
    vm->result.termination = FLAT_TERM_CANCELLED;
    vm->result.state = FLAT_VM_CANCELLED;
    snprintf(vm->result.diagnostic, sizeof vm->result.diagnostic, "VM cancelled");
    if (result) *result = vm->result;
    return 0;
}

int flat_vm_destroy(FlatVm *vm) {
    if (!vm || vm->state == FLAT_VM_RUNNING) return -1;
    flat_vm_state_destroy(vm->execution_state);
    vm->execution_state = NULL;
    flat_host_config_destroy(&vm->host);
    flat_fb_free(&vm->bytecode);
    memset(vm, 0, sizeof *vm);
    free(vm);
    return 0;
}

FlatVmState flat_vm_state(const FlatVm *vm) {
    return vm ? vm->state : (FlatVmState)0;
}

int flat_vm_is_terminal(const FlatVm *vm) {
    return vm && (vm->state == FLAT_VM_COMPLETED || vm->state == FLAT_VM_FAILED ||
                  vm->state == FLAT_VM_CANCELLED);
}
