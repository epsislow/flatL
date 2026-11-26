#include "fb_format.h"
#include "vm.h"

#define _POSIX_C_SOURCE 200809L
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

static int check_one(const char *path, unsigned *total_yields) {
    FlatBytecode bc;
    FlatVmOptions opts;
    FlatVmResult stepped;
    FlatVm *vm;
    int baseline_term = FLAT_TERM_UNKNOWN;
    int baseline_rc;
    int caller_termination = 314;
    char caller_diagnostic[32] = "caller-owned";
    unsigned calls = 0;
    unsigned yields = 0;
    char baseline_output[65536];
    char stepped_output[65536];
    FILE *capture;
    int saved_stdout;
    int rc = 0;

    memset(&bc, 0, sizeof bc);
    memset(&opts, 0, sizeof opts);
    memset(&stepped, 0, sizeof stepped);
    if (flat_fb_load_file(path, &bc) != 0) return 10;

    /* Capture the run-to-completion result and all observable stdout effects. */
    opts.out_termination = &baseline_term;
    capture = capture_begin(&saved_stdout);
    if (!capture) { flat_fb_free(&bc); return 18; }
    baseline_rc = flat_vm_run_opts(&bc, &opts);
    if (capture_end(capture, saved_stdout, baseline_output, sizeof baseline_output) != 0) {
        flat_fb_free(&bc); return 19;
    }
    if (baseline_rc != 0 ||
        (baseline_term != FLAT_TERM_OK && baseline_term != FLAT_TERM_COMPLETE)) {
        fprintf(stderr, "baseline failed: %s rc=%d term=%d\n", path,
                baseline_rc, baseline_term);
        flat_fb_free(&bc);
        return 11;
    }

    /* These output pointers are creation-time inputs only and must not be retained. */
    opts.out_termination = &caller_termination;
    opts.out_panic_msg = caller_diagnostic;
    opts.out_panic_msg_sz = sizeof caller_diagnostic;
    vm = flat_vm_create(&bc, &opts);
    flat_fb_free(&bc);
    if (!vm) return 12;
    caller_termination = 271;
    strcpy(caller_diagnostic, "still-caller-owned");

    capture = capture_begin(&saved_stdout);
    if (!capture) { flat_vm_destroy(vm); return 20; }
    while (!flat_vm_is_terminal(vm) && calls < 10000) {
        uint64_t budget = (uint64_t)(calls % 3u) + 1u;
        rc = flat_vm_step(vm, budget, &stepped);
        calls++;
        if (rc != 0) {
            fprintf(stderr, "step failed: %s rc=%d state=%d term=%d diagnostic=%s\n",
                    path, rc, (int)stepped.state, (int)stepped.termination,
                    stepped.diagnostic);
            (void)capture_end(capture, saved_stdout, stepped_output, sizeof stepped_output);
            flat_vm_destroy(vm);
            return 13;
        }
        if (stepped.state == FLAT_VM_YIELDED) yields++;
        else if (!flat_vm_is_terminal(vm)) {
            fprintf(stderr, "unexpected intermediate state: %s state=%d\n",
                    path, (int)stepped.state);
            (void)capture_end(capture, saved_stdout, stepped_output, sizeof stepped_output);
            flat_vm_destroy(vm);
            return 14;
        }
    }

    if (capture_end(capture, saved_stdout, stepped_output, sizeof stepped_output) != 0) {
        flat_vm_destroy(vm); return 21;
    }

    if (strcmp(baseline_output, stepped_output) != 0) {
        fprintf(stderr, "stepped output differs: %s\n--- baseline ---\n%s\n--- stepped ---\n%s\n",
                path, baseline_output, stepped_output);
        flat_vm_destroy(vm);
        return 22;
    }

    if (calls >= 10000 || flat_vm_state(vm) != FLAT_VM_COMPLETED ||
        stepped.exit_code != baseline_rc || stepped.termination != (FlatTermination)baseline_term ||
        yields == 0) {
        fprintf(stderr,
                "stepped result mismatch: %s calls=%u yields=%u state=%d rc=%d term=%d baseline=%d/%d\n",
                path, calls, yields, (int)flat_vm_state(vm), stepped.exit_code,
                (int)stepped.termination, baseline_rc, baseline_term);
        flat_vm_destroy(vm);
        return 15;
    }

    if (caller_termination != 271 || strcmp(caller_diagnostic, "still-caller-owned") != 0) {
        fprintf(stderr, "VM retained caller output pointers: %s term=%d diagnostic=%s\n",
                path, caller_termination, caller_diagnostic);
        flat_vm_destroy(vm);
        return 17;
    }

    *total_yields += yields;
    if (flat_vm_destroy(vm) != 0) return 16;
    return 0;
}

int main(int argc, char **argv) {
    unsigned yields = 0;
    int i;
    if (argc != 2) return 2;
    for (i = 1; i < argc; i++) {
        int rc = check_one(argv[i], &yields);
        if (rc != 0) return rc;
    }
    printf("vm-stepping-control-flow-ok yields=%u output-equivalent=1\n", yields);
    return 0;
}
