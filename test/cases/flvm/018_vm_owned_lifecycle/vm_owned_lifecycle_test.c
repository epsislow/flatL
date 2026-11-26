#include "fb_format.h"
#include "flat_host_config.h"
#include "vm.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    FlatBytecode bc;
    FlatHostConfig host;
    FlatVmOptions opts;
    FlatVmResult result;
    FlatVm *vm;
    int rc;

    if (argc != 2) return 2;
    memset(&bc, 0, sizeof bc);
    memset(&host, 0, sizeof host);
    memset(&opts, 0, sizeof opts);
    memset(&result, 0, sizeof result);

    if (flat_fb_load_file(argv[1], &bc) != 0) return 3;
    if (flat_host_config_init(&host) != 0) {
        flat_fb_free(&bc);
        return 4;
    }
    if (flat_env_context_override_set(&host.env, "FLAT_VM_OWNED", "snapshot") != 0) {
        flat_host_config_destroy(&host);
        flat_fb_free(&bc);
        return 5;
    }
    opts.host_config = &host;
    vm = flat_vm_create(&bc, &opts);
    flat_host_config_destroy(&host);
    if (!vm) {
        flat_fb_free(&bc);
        return 6;
    }

    /* The VM must no longer depend on caller-owned bytecode after creation. */
    flat_fb_free(&bc);
    if (flat_vm_state(vm) != FLAT_VM_READY) {
        flat_vm_destroy(vm);
        return 7;
    }
    rc = flat_vm_execute(vm, &result);
    if (rc != 0 || result.exit_code != 0 ||
        (result.termination != FLAT_TERM_OK && result.termination != FLAT_TERM_COMPLETE) ||
        result.state != FLAT_VM_COMPLETED || flat_vm_state(vm) != FLAT_VM_COMPLETED ||
        !flat_vm_is_terminal(vm)) {
        fprintf(stderr, "owned VM execution failed: rc=%d term=%d state=%d diagnostic='%s'\n",
                rc, (int)result.termination, (int)result.state, result.diagnostic);
        flat_vm_destroy(vm);
        return 8;
    }
    if (flat_vm_execute(vm, &result) != -1) {
        flat_vm_destroy(vm);
        return 9;
    }
    if (flat_vm_destroy(vm) != 0) return 10;

    /* A never-executed VM already owns initialized resources and must release them. */
    if (flat_fb_load_file(argv[1], &bc) != 0) return 11;
    vm = flat_vm_create(&bc, NULL);
    flat_fb_free(&bc);
    if (!vm || flat_vm_state(vm) != FLAT_VM_READY) {
        if (vm) flat_vm_destroy(vm);
        return 12;
    }
    if (flat_vm_destroy(vm) != 0) return 13;
    puts("vm-owned-lifecycle-ok");
    return 0;
}
