#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"

#include <stdio.h>
#include <string.h>

static int load_bytecode(const char *path, FlatBytecode *bc) {
    memset(bc, 0, sizeof *bc);
    return flat_fb_load_file(path, bc);
}

static int expect_lifecycle(FlatRuntime *runtime, FlatRuntimeLifecycle expected) {
    FlatRuntimeLifecycle actual = flat_runtime_lifecycle(runtime);
    if (actual != expected) {
        fprintf(stderr, "lifecycle expected %d got %d\n", (int)expected, (int)actual);
        return -1;
    }
    return 0;
}

static int expect_status(FlatRuntime *runtime, FlatRuntimeShutdownStatus expected) {
    FlatRuntimeShutdownStatus actual = flat_runtime_shutdown_status(runtime);
    if (actual != expected) {
        fprintf(stderr, "shutdown status expected %d got %d\n", (int)expected, (int)actual);
        return -1;
    }
    return 0;
}

static int expect_state(FlatRuntime *runtime, FlatTaskId id, FlatTaskState expected) {
    FlatTaskState actual = flat_runtime_task_state(runtime, id);
    if (actual != expected) {
        fprintf(stderr, "task %llu state expected %d got %d\n",
                (unsigned long long)id, (int)expected, (int)actual);
        return -1;
    }
    return 0;
}

static int expect_cancelled(FlatRuntime *runtime, FlatTaskId id) {
    FlatVmResult result;
    return expect_state(runtime, id, FLAT_TASK_CANCELLED) == 0 &&
           flat_runtime_task_result(runtime, id, &result) == 0 &&
           result.state == FLAT_VM_CANCELLED ? 0 : -1;
}

int main(int argc, char **argv) {
    FlatBytecode bc;
    FlatRuntime *runtime = NULL;
    FlatTaskId created = 0, created_cancel = 0, running = 0, target = 0, waiter = 0;
    int rc = 1;
    memset(&bc, 0, sizeof bc);
    if (argc != 2 || load_bytecode(argv[1], &bc) != 0) { rc = 2; goto done; }

    /* DRAIN: only started work belongs to the drain set. CREATED records stay
     * host-owned, are not auto-started, and can be explicitly destroyed. */
    runtime = flat_runtime_create(NULL);
    if (!runtime || flat_runtime_task_create(runtime, &bc, NULL, &created) != 0 ||
        flat_runtime_task_create(runtime, &bc, NULL, &created_cancel) != 0 ||
        flat_runtime_task_create(runtime, &bc, NULL, &running) != 0 ||
        flat_runtime_task_start(runtime, running) != 0 ||
        flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_DRAIN) != 0 ||
        expect_lifecycle(runtime, FLAT_RUNTIME_DRAINING) != 0 ||
        expect_status(runtime, FLAT_RUNTIME_SHUTDOWN_IN_PROGRESS) != 0 ||
        flat_runtime_task_create(runtime, &bc, NULL, &target) == 0 ||
        flat_runtime_task_start(runtime, created) == 0 ||
        flat_runtime_run(runtime, &bc, NULL) == 0) { rc = 3; goto done; }
    if (flat_runtime_schedule(runtime, 16, 0, NULL) != 0 ||
        expect_state(runtime, running, FLAT_TASK_COMPLETED) != 0 ||
        expect_status(runtime, FLAT_RUNTIME_SHUTDOWN_COMPLETE) != 0 ||
        expect_lifecycle(runtime, FLAT_RUNTIME_STOPPED) != 0 ||
        flat_runtime_schedule(runtime, 1, 1, NULL) == 0 ||
        flat_runtime_task_destroy(runtime, running) != 0 ||
        expect_state(runtime, created, FLAT_TASK_CREATED) != 0 ||
        expect_state(runtime, created_cancel, FLAT_TASK_CREATED) != 0 ||
        flat_runtime_task_destroy(runtime, created) != 0 ||
        flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL) != 0 ||
        expect_lifecycle(runtime, FLAT_RUNTIME_CANCELLING) != 0 ||
        flat_runtime_schedule(runtime, 1, 0, NULL) != 0 ||
        expect_cancelled(runtime, created_cancel) != 0 ||
        expect_status(runtime, FLAT_RUNTIME_SHUTDOWN_COMPLETE) != 0 ||
        flat_runtime_task_destroy(runtime, created_cancel) != 0 ||
        flat_runtime_destroy(runtime) != 0) { rc = 4; runtime = NULL; goto done; }
    runtime = NULL;
    created = created_cancel = running = 0;

    /* DRAIN blocked by a wait edge to an unstarted CREATED target. Escalation
     * to CANCEL must cancel both tasks; a later DRAIN cannot downgrade it. */
    runtime = flat_runtime_create(NULL);
    if (!runtime || flat_runtime_task_create(runtime, &bc, NULL, &target) != 0 ||
        flat_runtime_task_create(runtime, &bc, NULL, &waiter) != 0 ||
        flat_runtime_task_start(runtime, waiter) != 0 ||
        flat_runtime_task_wait(runtime, waiter, target) != 0 ||
        flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_DRAIN) != 0 ||
        expect_status(runtime, FLAT_RUNTIME_SHUTDOWN_BLOCKED) != 0 ||
        flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL) != 0 ||
        flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_DRAIN) != 0 ||
        expect_lifecycle(runtime, FLAT_RUNTIME_CANCELLING) != 0 ||
        expect_status(runtime, FLAT_RUNTIME_SHUTDOWN_IN_PROGRESS) != 0 ||
        flat_runtime_schedule(runtime, 1, 0, NULL) != 0 ||
        expect_cancelled(runtime, target) != 0 || expect_cancelled(runtime, waiter) != 0 ||
        expect_status(runtime, FLAT_RUNTIME_SHUTDOWN_COMPLETE) != 0 ||
        flat_runtime_task_destroy(runtime, target) != 0 ||
        flat_runtime_task_destroy(runtime, waiter) != 0 ||
        flat_runtime_destroy(runtime) != 0) { rc = 5; runtime = NULL; goto done; }
    runtime = NULL;
    target = waiter = 0;

    /* CANCEL includes CREATED and RUNNABLE work and remains idempotent. */
    runtime = flat_runtime_create(NULL);
    if (!runtime || flat_runtime_task_create(runtime, &bc, NULL, &created) != 0 ||
        flat_runtime_task_create(runtime, &bc, NULL, &running) != 0 ||
        flat_runtime_task_start(runtime, running) != 0 ||
        flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL) != 0 ||
        flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL) != 0 ||
        expect_lifecycle(runtime, FLAT_RUNTIME_CANCELLING) != 0 ||
        flat_runtime_schedule(runtime, 1, 0, NULL) != 0 ||
        expect_cancelled(runtime, created) != 0 || expect_cancelled(runtime, running) != 0 ||
        expect_status(runtime, FLAT_RUNTIME_SHUTDOWN_COMPLETE) != 0 ||
        flat_runtime_task_destroy(runtime, created) != 0 ||
        flat_runtime_task_destroy(runtime, running) != 0 ||
        flat_runtime_destroy(runtime) != 0) { rc = 6; runtime = NULL; goto done; }
    runtime = NULL;
    created = running = 0;
    rc = 0;

done:
    if (runtime) {
        (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        (void)flat_runtime_schedule(runtime, 16, 0, NULL);
        if (created) (void)flat_runtime_task_destroy(runtime, created);
        if (created_cancel) (void)flat_runtime_task_destroy(runtime, created_cancel);
        if (running) (void)flat_runtime_task_destroy(runtime, running);
        if (target) (void)flat_runtime_task_destroy(runtime, target);
        if (waiter) (void)flat_runtime_task_destroy(runtime, waiter);
        (void)flat_runtime_destroy(runtime);
    }
    flat_fb_free(&bc);
    if (rc) fprintf(stderr, "runtime shutdown test failed: %d\n", rc);
    else puts("runtime-shutdown-ok drain=complete,blocked cancel=escalation,created-cleanup=verified");
    return rc;
}
