#include "fb_format.h"
#include "vm.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    FlatBytecode bc;
    FlatVmOptions opts;
    FlatVmResult result;
    FlatVm *vm;
    unsigned steps = 0;
    int rc;

    if (argc != 2) return 2;
    memset(&bc, 0, sizeof bc);
    memset(&opts, 0, sizeof opts);
    memset(&result, 0, sizeof result);
    if (flat_fb_load_file(argv[1], &bc) != 0) return 3;

    vm = flat_vm_create(&bc, &opts);
    flat_fb_free(&bc);
    if (!vm || flat_vm_state(vm) != FLAT_VM_READY) {
        if (vm) flat_vm_destroy(vm);
        return 4;
    }

    rc = flat_vm_step(vm, 1, &result);
    if (rc != 0 || result.state != FLAT_VM_YIELDED ||
        flat_vm_state(vm) != FLAT_VM_YIELDED || flat_vm_is_terminal(vm)) {
        fprintf(stderr, "first step did not yield: rc=%d state=%d term=%d\n", rc,
                (int)result.state, (int)result.termination);
        flat_vm_destroy(vm);
        return 5;
    }
    steps++;

    while (flat_vm_state(vm) == FLAT_VM_YIELDED && steps < 32) {
        rc = flat_vm_step(vm, 1, &result);
        if (rc != 0) {
            fprintf(stderr, "step failed: rc=%d state=%d diagnostic='%s'\n", rc,
                    (int)result.state, result.diagnostic);
            flat_vm_destroy(vm);
            return 6;
        }
        steps++;
    }

    if (flat_vm_state(vm) != FLAT_VM_COMPLETED || !flat_vm_is_terminal(vm) ||
        result.state != FLAT_VM_COMPLETED || result.exit_code != 0 ||
        (result.termination != FLAT_TERM_OK && result.termination != FLAT_TERM_COMPLETE) ||
        steps < 4 || steps >= 32) {
        fprintf(stderr, "stepped VM did not complete: steps=%u state=%d term=%d rc=%d\n",
                steps, (int)result.state, (int)result.termination, result.exit_code);
        flat_vm_destroy(vm);
        return 7;
    }
    if (flat_vm_step(vm, 1, &result) != -1) {
        flat_vm_destroy(vm);
        return 8;
    }
    if (flat_vm_destroy(vm) != 0) return 9;
    printf("vm-stepping-ok steps=%u\n", steps);
    return 0;
}
