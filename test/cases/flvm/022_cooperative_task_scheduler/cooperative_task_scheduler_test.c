#define _POSIX_C_SOURCE 200809L
#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int load_bytecode(const char *path, FlatBytecode *bc) {
    memset(bc, 0, sizeof *bc);
    return flat_fb_load_file(path, bc);
}

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

int main(int argc, char **argv) {
    FlatBytecode ok_bc, fail_bc;
    FlatRuntime *runtime;
    FlatTaskId first, second, failing, never_started, next_id;
    FlatVmResult result;
    FlatTaskState state;
    FILE *capture;
    int saved_stdout;
    uint64_t turns = 0;
    char output[512];
    const char expected[] = "step-1\nstep-1\nbefore-fail\nstep-2\nstep-2\nstep-3\nstep-3\n";

    if (argc != 3) return 2;
    if (load_bytecode(argv[1], &ok_bc) != 0) return 3;
    if (load_bytecode(argv[2], &fail_bc) != 0) {
        flat_fb_free(&ok_bc);
        return 4;
    }
    runtime = flat_runtime_create(NULL);
    if (!runtime) {
        flat_fb_free(&fail_bc);
        flat_fb_free(&ok_bc);
        return 5;
    }

    if (flat_runtime_task_create(runtime, &ok_bc, NULL, &first) != 0 ||
        flat_runtime_task_create(runtime, &ok_bc, NULL, &second) != 0 ||
        flat_runtime_task_create(runtime, &fail_bc, NULL, &failing) != 0) return 6;
    flat_fb_free(&ok_bc);
    flat_fb_free(&fail_bc);

    if (first == 0 || second <= first || failing <= second) return 7;
    if (flat_runtime_task_state(runtime, first) != FLAT_TASK_CREATED ||
        flat_runtime_task_start(runtime, first) != 0 ||
        flat_runtime_task_start(runtime, first) == 0 ||
        flat_runtime_task_start(runtime, second) != 0 ||
        flat_runtime_task_start(runtime, failing) != 0) return 8;
    if (flat_runtime_task_result(runtime, first, &result) == 0) return 9;
    if (flat_runtime_destroy(runtime) == 0) return 10; /* Registry still owns tasks. */
    if (flat_runtime_schedule(runtime, 0, 1, &turns) == 0) return 11;

    capture = capture_begin(&saved_stdout);
    if (!capture) return 12;
    if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) {
        (void)capture_end(capture, saved_stdout, output, sizeof output);
        return 13;
    }
    if (flat_runtime_schedule(runtime, 1, 0, &turns) != 0 || turns < 5) {
        (void)capture_end(capture, saved_stdout, output, sizeof output);
        return 14;
    }
    if (capture_end(capture, saved_stdout, output, sizeof output) != 0) return 15;

    if (flat_runtime_task_state(runtime, first) != FLAT_TASK_COMPLETED ||
        flat_runtime_task_state(runtime, second) != FLAT_TASK_COMPLETED ||
        flat_runtime_task_state(runtime, failing) != FLAT_TASK_FAILED) {
        fprintf(stderr, "task states: first=%d second=%d failing=%d turns=%llu\n",
                (int)flat_runtime_task_state(runtime, first),
                (int)flat_runtime_task_state(runtime, second),
                (int)flat_runtime_task_state(runtime, failing),
                (unsigned long long)turns);
        return 16;
    }
    if (strcmp(output, expected) != 0) {
        fprintf(stderr, "unexpected scheduler output: '%s'\n", output);
        return 17;
    }
    if (flat_runtime_task_result(runtime, first, &result) != 0 ||
        result.state != FLAT_VM_COMPLETED || result.exit_code != 0) return 18;
    if (flat_runtime_task_result(runtime, failing, &result) != 0 ||
        result.state != FLAT_VM_FAILED || result.termination != FLAT_TERM_FAIL) return 19;

    if (flat_runtime_task_create(runtime, NULL, NULL, &never_started) == 0) return 20;
    if (load_bytecode(argv[1], &ok_bc) != 0) return 21;
    if (flat_runtime_task_create(runtime, &ok_bc, NULL, &never_started) != 0) {
        flat_fb_free(&ok_bc);
        return 22;
    }
    flat_fb_free(&ok_bc);
    state = flat_runtime_task_state(runtime, never_started);
    if (state != FLAT_TASK_CREATED || flat_runtime_task_destroy(runtime, never_started) != 0) return 23;
    if (load_bytecode(argv[1], &ok_bc) != 0) return 24;
    if (flat_runtime_task_create(runtime, &ok_bc, NULL, &next_id) != 0) {
        flat_fb_free(&ok_bc);
        return 25;
    }
    flat_fb_free(&ok_bc);
    if (next_id <= never_started || flat_runtime_task_destroy(runtime, next_id) != 0) return 26;

    if (flat_runtime_task_destroy(runtime, first) != 0 ||
        flat_runtime_task_destroy(runtime, second) != 0 ||
        flat_runtime_task_destroy(runtime, failing) != 0) return 27;
    if (flat_runtime_task_state(runtime, first) != (FlatTaskState)0) return 28;
    if (flat_runtime_schedule(runtime, 1, 0, &turns) != 0 || turns != 0) return 29;
    if (flat_runtime_destroy(runtime) != 0) return 30;
    puts("cooperative-task-scheduler-ok");
    return 0;
}
