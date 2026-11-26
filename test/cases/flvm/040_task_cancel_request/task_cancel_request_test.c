#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"
#include "flat_native_capability.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct SchedulerContext {
    FlatRuntime *runtime;
    atomic_int started;
    int rc;
    uint64_t turns;
} SchedulerContext;

static int load_bytecode(const char *path, FlatBytecode *bc) {
    memset(bc, 0, sizeof *bc);
    return flat_fb_load_file(path, bc);
}

static void pause_one_ms(void) {
    struct timespec ts = {0, 1000000L};
    (void)nanosleep(&ts, NULL);
}

static void *schedule_one_turn(void *opaque) {
    SchedulerContext *ctx = (SchedulerContext *)opaque;
    atomic_store_explicit(&ctx->started, 1, memory_order_release);
    ctx->rc = flat_runtime_schedule(ctx->runtime, 2, 1, &ctx->turns);
    return NULL;
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
           result.state == FLAT_VM_CANCELLED &&
           result.termination == FLAT_TERM_CANCELLED ? 0 : -1;
}

int main(int argc, char **argv) {
    FlatBytecode bc, dom_bc;
    FlatRuntime *runtime = NULL;
    FlatTaskId created = 0, runnable = 0, target = 0, waiter = 0, running = 0, completed = 0;
    FlatTaskId wake_target = 0, dependent = 0;
    SchedulerContext scheduler;
    pthread_t scheduler_thread;
    int scheduler_created = 0, gate_held = 0, rc = 1;
    unsigned spin;

    memset(&bc, 0, sizeof bc);
    memset(&dom_bc, 0, sizeof dom_bc);
    memset(&scheduler, 0, sizeof scheduler);
    if (argc != 3 || load_bytecode(argv[1], &bc) != 0 ||
        load_bytecode(argv[2], &dom_bc) != 0) { rc = 2; goto done; }
    runtime = flat_runtime_create(NULL);
    if (!runtime) { rc = 3; goto done; }

    /* CREATED: requests are idempotent and do not mutate state/result inline. */
    if (flat_runtime_task_create(runtime, &bc, NULL, &created) != 0 ||
        flat_runtime_task_cancel_request(runtime, created) != 0 ||
        flat_runtime_task_cancel_request(runtime, created) != 0 ||
        expect_state(runtime, created, FLAT_TASK_CREATED) != 0 ||
        flat_runtime_task_start(runtime, created) == 0 ||
        flat_runtime_task_cancel_request(runtime, UINT64_MAX) == 0) { rc = 4; goto done; }
    if (flat_runtime_schedule(runtime, 1, 1, NULL) != 0 ||
        expect_cancelled(runtime, created) != 0 ||
        flat_runtime_task_cancel_request(runtime, created) != 1 ||
        flat_runtime_task_destroy(runtime, created) != 0) { rc = 5; goto done; }
    created = 0;

    /* RUNNABLE: the scheduler claims cancellation before another VM step. */
    if (flat_runtime_task_create(runtime, &bc, NULL, &runnable) != 0 ||
        flat_runtime_task_start(runtime, runnable) != 0 ||
        flat_runtime_task_cancel_request(runtime, runnable) != 0 ||
        expect_state(runtime, runnable, FLAT_TASK_RUNNABLE) != 0 ||
        flat_runtime_schedule(runtime, 1, 1, NULL) != 0 ||
        expect_cancelled(runtime, runnable) != 0 ||
        flat_runtime_task_destroy(runtime, runnable) != 0) { rc = 6; goto done; }
    runnable = 0;

    /* A terminal VM result wins over a later cancellation request. */
    if (flat_runtime_task_create(runtime, &bc, NULL, &completed) != 0 ||
        flat_runtime_task_start(runtime, completed) != 0 ||
        flat_runtime_schedule(runtime, 8, 0, NULL) != 0 ||
        expect_state(runtime, completed, FLAT_TASK_COMPLETED) != 0 ||
        flat_runtime_task_cancel_request(runtime, completed) != 1 ||
        expect_state(runtime, completed, FLAT_TASK_COMPLETED) != 0 ||
        flat_runtime_task_destroy(runtime, completed) != 0) { rc = 13; goto done; }
    completed = 0;

    /* WAITING: remove the wait edge before publishing cancellation terminal. */
    if (flat_runtime_task_create(runtime, &bc, NULL, &target) != 0 ||
        flat_runtime_task_create(runtime, &bc, NULL, &waiter) != 0 ||
        flat_runtime_task_start(runtime, waiter) != 0 ||
        flat_runtime_task_wait(runtime, waiter, target) != 0 ||
        expect_state(runtime, waiter, FLAT_TASK_WAITING) != 0 ||
        flat_runtime_task_cancel_request(runtime, waiter) != 0 ||
        flat_runtime_schedule(runtime, 1, 1, NULL) != 0 ||
        expect_cancelled(runtime, waiter) != 0 ||
        flat_runtime_task_destroy(runtime, waiter) != 0 ||
        flat_runtime_task_destroy(runtime, target) != 0) { rc = 7; goto done; }
    waiter = target = 0;

    /* Cancelling a target wakes its dependent only after cancellation cleanup
     * has committed the terminal result. */
    if (flat_runtime_task_create(runtime, &bc, NULL, &wake_target) != 0 ||
        flat_runtime_task_create(runtime, &bc, NULL, &dependent) != 0 ||
        flat_runtime_task_start(runtime, dependent) != 0 ||
        flat_runtime_task_wait(runtime, dependent, wake_target) != 0 ||
        flat_runtime_task_cancel_request(runtime, wake_target) != 0 ||
        flat_runtime_schedule(runtime, 1, 1, NULL) != 0 ||
        expect_cancelled(runtime, wake_target) != 0 ||
        expect_state(runtime, dependent, FLAT_TASK_RUNNABLE) != 0 ||
        flat_runtime_task_destroy(runtime, wake_target) != 0) { rc = 14; goto done; }
    wake_target = 0;
    if (flat_runtime_schedule(runtime, 8, 0, NULL) != 0 ||
        expect_state(runtime, dependent, FLAT_TASK_COMPLETED) != 0 ||
        flat_runtime_task_destroy(runtime, dependent) != 0) { rc = 15; goto done; }
    dependent = 0;

    /* RUNNING: hold the DOM gate so the VM is inside flat_vm_step(). The
     * request must return without touching the VM; the owner cancels only
     * after the native call returns and the safe quantum boundary is reached. */
    if (flat_runtime_task_create(runtime, &dom_bc, NULL, &running) != 0 ||
        flat_runtime_task_start(runtime, running) != 0 ||
        flat_native_module_gate_enter("dom") != 0) { rc = 8; goto done; }
    gate_held = 1;
    scheduler.runtime = runtime;
    atomic_init(&scheduler.started, 0);
    if (pthread_create(&scheduler_thread, NULL, schedule_one_turn, &scheduler) != 0) {
        rc = 9; goto done;
    }
    scheduler_created = 1;
    for (spin = 0; spin < 5000 &&
         flat_runtime_task_state(runtime, running) != FLAT_TASK_RUNNING; ++spin) pause_one_ms();
    if (flat_runtime_task_state(runtime, running) != FLAT_TASK_RUNNING ||
        flat_runtime_task_cancel_request(runtime, running) != 0 ||
        flat_runtime_task_cancel_request(runtime, running) != 0 ||
        expect_state(runtime, running, FLAT_TASK_RUNNING) != 0) { rc = 10; goto done; }
    (void)flat_native_module_gate_leave("dom");
    gate_held = 0;
    pthread_join(scheduler_thread, NULL);
    scheduler_created = 0;
    if (scheduler.rc != 0 || scheduler.turns != 1 ||
        expect_cancelled(runtime, running) != 0 ||
        flat_runtime_task_destroy(runtime, running) != 0) { rc = 11; goto done; }
    running = 0;

    if (flat_runtime_destroy(runtime) != 0) { rc = 12; runtime = NULL; goto done; }
    runtime = NULL;
    rc = 0;

done:
    if (gate_held) (void)flat_native_module_gate_leave("dom");
    if (scheduler_created) pthread_join(scheduler_thread, NULL);
    if (runtime) {
        FlatTaskId ids[] = {created, runnable, waiter, target, running, completed, wake_target, dependent};
        size_t i;
        for (i = 0; i < sizeof ids / sizeof ids[0]; ++i) if (ids[i]) {
            if (flat_runtime_task_state(runtime, ids[i]) != FLAT_TASK_CANCELLED)
                (void)flat_runtime_task_cancel(runtime, ids[i]);
            (void)flat_runtime_task_destroy(runtime, ids[i]);
        }
        (void)flat_runtime_destroy(runtime);
    }
    flat_fb_free(&dom_bc);
    flat_fb_free(&bc);
    if (rc) fprintf(stderr, "task cancel request test failed: %d\n", rc);
    else puts("task-cancel-request-ok states=created,runnable,waiting,running owner-cleanup=verified");
    return rc;
}
