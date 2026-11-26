#define _POSIX_C_SOURCE 200809L
#include "fb_format.h"
#include "vm.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static FILE *capture_begin(int *saved_stdout) {
    FILE *capture = tmpfile();
    if (!capture) return NULL;
    fflush(stdout);
    *saved_stdout = dup(STDOUT_FILENO);
    if (*saved_stdout < 0 || dup2(fileno(capture), STDOUT_FILENO) < 0) {
        if (*saved_stdout >= 0) close(*saved_stdout);
        fclose(capture);
        return NULL;
    }
    return capture;
}

static int capture_end(FILE *capture, int saved_stdout, char *out, size_t out_sz) {
    size_t n;
    int rc = 0;
    fflush(stdout);
    if (dup2(saved_stdout, STDOUT_FILENO) < 0) rc = -1;
    close(saved_stdout);
    rewind(capture);
    n = fread(out, 1, out_sz - 1, capture);
    out[n] = '\0';
    if (!feof(capture)) rc = -1;
    fclose(capture);
    return rc;
}

static int load_vm(const char *path, FlatVm **out) {
    FlatBytecode bc;
    FlatVmOptions opts;
    int rc;
    memset(&bc, 0, sizeof bc);
    memset(&opts, 0, sizeof opts);
    if (flat_fb_load_file(path, &bc) != 0) return -1;
    *out = flat_vm_create(&bc, &opts);
    flat_fb_free(&bc);
    if (!*out) return -1;
    rc = flat_vm_state(*out) == FLAT_VM_READY ? 0 : -1;
    if (rc != 0) flat_vm_destroy(*out);
    return rc;
}

static int check_fail(const char *path) {
    FlatVm *vm = NULL;
    FlatVmResult result;
    FILE *capture;
    int saved_stdout;
    int rc;
    char output[256];
    memset(&result, 0, sizeof result);
    if (load_vm(path, &vm) != 0) return 10;
    capture = capture_begin(&saved_stdout);
    if (!capture) { flat_vm_destroy(vm); return 11; }
    rc = flat_vm_step(vm, 1, &result);
    if (rc != 0 || result.state != FLAT_VM_YIELDED || flat_vm_state(vm) != FLAT_VM_YIELDED) {
        (void)capture_end(capture, saved_stdout, output, sizeof output);
        flat_vm_destroy(vm);
        return 12;
    }
    rc = flat_vm_step(vm, 1, &result);
    if (capture_end(capture, saved_stdout, output, sizeof output) != 0) {
        flat_vm_destroy(vm); return 13;
    }
    if (rc != 2 || result.exit_code != 2 || result.termination != FLAT_TERM_FAIL ||
        result.state != FLAT_VM_FAILED || flat_vm_state(vm) != FLAT_VM_FAILED ||
        !flat_vm_is_terminal(vm) || strcmp(output, "before-fail\n") != 0) {
        fprintf(stderr, "FAIL lifecycle mismatch rc=%d term=%d state=%d output='%s' diag='%s'\n",
                rc, (int)result.termination, (int)result.state, output, result.diagnostic);
        flat_vm_destroy(vm);
        return 14;
    }
    if (flat_vm_step(vm, 1, &result) != -1 || flat_vm_destroy(vm) != 0) return 15;
    return 0;
}

static int check_panic(const char *path) {
    FlatVm *vm = NULL;
    FlatVmResult result;
    int rc;
    memset(&result, 0, sizeof result);
    if (load_vm(path, &vm) != 0) return 20;
    rc = flat_vm_step(vm, 0, &result);
    if (rc != 1 || result.exit_code != 1 || result.termination != FLAT_TERM_PANIC ||
        result.state != FLAT_VM_FAILED || flat_vm_state(vm) != FLAT_VM_FAILED ||
        !flat_vm_is_terminal(vm) || result.diagnostic[0] == '\0') {
        fprintf(stderr, "PANIC lifecycle mismatch rc=%d term=%d state=%d diag='%s'\n",
                rc, (int)result.termination, (int)result.state, result.diagnostic);
        flat_vm_destroy(vm);
        return 21;
    }
    if (flat_vm_step(vm, 1, &result) != -1 || flat_vm_destroy(vm) != 0) return 22;
    return 0;
}

static int check_destroy_after_yield(const char *path) {
    FlatVm *vm = NULL;
    FlatVmResult result;
    FILE *capture;
    int saved_stdout;
    char output[256];
    int rc;
    memset(&result, 0, sizeof result);
    if (load_vm(path, &vm) != 0) return 30;
    capture = capture_begin(&saved_stdout);
    if (!capture) { flat_vm_destroy(vm); return 31; }
    rc = flat_vm_step(vm, 1, &result);
    if (rc != 0 || result.state != FLAT_VM_YIELDED || flat_vm_state(vm) != FLAT_VM_YIELDED) {
        (void)capture_end(capture, saved_stdout, output, sizeof output);
        flat_vm_destroy(vm);
        return 32;
    }
    if (flat_vm_destroy(vm) != 0) {
        (void)capture_end(capture, saved_stdout, output, sizeof output);
        return 33;
    }
    if (capture_end(capture, saved_stdout, output, sizeof output) != 0) return 34;
    if (strcmp(output, "before-destroy\n") != 0) {
        fprintf(stderr, "destroy-after-yield output mismatch: '%s'\n", output);
        return 35;
    }
    return 0;
}

int main(int argc, char **argv) {
    int rc;
    if (argc != 4) return 2;
    rc = check_fail(argv[1]);
    if (rc != 0) return rc;
    rc = check_panic(argv[2]);
    if (rc != 0) return rc;
    rc = check_destroy_after_yield(argv[3]);
    if (rc != 0) return rc;
    puts("vm-failure-lifecycle-ok fail=1 panic=1 destroy-after-yield=1");
    return 0;
}
