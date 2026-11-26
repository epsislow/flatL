#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"

#include <stdio.h>
#include <string.h>

static int load_bytecode(const char *path, FlatBytecode *bc) {
    memset(bc, 0, sizeof *bc);
    return flat_fb_load_file(path, bc);
}

static int expect_state(FlatRuntime *runtime, FlatTaskId id, FlatTaskState expected) {
    FlatTaskState actual = flat_runtime_task_state(runtime, id);
    if (actual != expected) {
        fprintf(stderr, "task %llu state: expected %d, got %d\n",
                (unsigned long long)id, (int)expected, (int)actual);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    FlatBytecode bc, fail_bc;
    FlatRuntime *runtime;
    FlatTaskId target, waiter, late_waiter, cancelled_created;
    FlatTaskId cancel_target, dependent, yielded;
    FlatTaskId fail_target, fail_waiter, dormant_target, cancelled_waiter;
    FlatTaskId cycle_a, cycle_b, cycle_c;
    FlatVmResult result;
    uint64_t turns = 0;
    unsigned i;

    if (argc != 3) return 2;
    if (load_bytecode(argv[1], &bc) != 0) return 3;
    if (load_bytecode(argv[2], &fail_bc) != 0) { flat_fb_free(&bc); return 4; }
    runtime = flat_runtime_create(NULL);
    if (!runtime) {
        flat_fb_free(&fail_bc);
        flat_fb_free(&bc);
        return 5;
    }

    /* A waiter sleeps while its target advances, then wakes on completion. */
    if (flat_runtime_task_create(runtime, &bc, NULL, &target) != 0 ||
        flat_runtime_task_create(runtime, &bc, NULL, &waiter) != 0) return 5;
    if (flat_runtime_task_start(runtime, target) != 0 ||
        flat_runtime_task_start(runtime, waiter) != 0) return 6;
    if (flat_runtime_task_wait(runtime, waiter, waiter) == 0 ||
        flat_runtime_task_wait(runtime, waiter, UINT64_MAX) == 0 ||
        flat_runtime_task_wait(runtime, waiter, target) != 0) return 7;
    if (expect_state(runtime, waiter, FLAT_TASK_WAITING) != 0 ||
        flat_runtime_task_wait(runtime, waiter, target) == 0 ||
        flat_runtime_task_destroy(runtime, target) == 0) return 8;

    for (i = 0; i < 64 && flat_runtime_task_state(runtime, target) != FLAT_TASK_COMPLETED &&
                flat_runtime_task_state(runtime, target) != FLAT_TASK_FAILED; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) return 9;
        if (flat_runtime_task_state(runtime, waiter) != FLAT_TASK_WAITING &&
            flat_runtime_task_state(runtime, target) != FLAT_TASK_COMPLETED) return 10;
    }
    if (expect_state(runtime, target, FLAT_TASK_COMPLETED) != 0 ||
        expect_state(runtime, waiter, FLAT_TASK_RUNNABLE) != 0) return 11;
    if (flat_runtime_schedule(runtime, 2, 0, &turns) != 0 || turns == 0 ||
        expect_state(runtime, waiter, FLAT_TASK_COMPLETED) != 0) return 12;

    /* Waiting for an already terminal target does not suspend the waiter. */
    if (flat_runtime_task_create(runtime, &bc, NULL, &late_waiter) != 0 ||
        flat_runtime_task_start(runtime, late_waiter) != 0 ||
        flat_runtime_task_wait(runtime, late_waiter, target) != 0 ||
        expect_state(runtime, late_waiter, FLAT_TASK_RUNNABLE) != 0) return 13;
    if (flat_runtime_schedule(runtime, 2, 0, &turns) != 0 ||
        expect_state(runtime, late_waiter, FLAT_TASK_COMPLETED) != 0) return 14;

    /* A CREATED task can be cancelled before its first instruction. */
    if (flat_runtime_task_create(runtime, &bc, NULL, &cancelled_created) != 0 ||
        flat_runtime_task_cancel(runtime, cancelled_created) != 0 ||
        expect_state(runtime, cancelled_created, FLAT_TASK_CANCELLED) != 0) return 15;
    if (flat_runtime_task_result(runtime, cancelled_created, &result) != 0 ||
        result.state != FLAT_VM_CANCELLED || result.termination != FLAT_TERM_CANCELLED ||
        flat_runtime_task_cancel(runtime, cancelled_created) == 0 ||
        flat_runtime_task_start(runtime, cancelled_created) == 0) return 16;

    /* Cancelling a target wakes its waiters just like other terminal outcomes. */
    if (flat_runtime_task_create(runtime, &bc, NULL, &cancel_target) != 0 ||
        flat_runtime_task_create(runtime, &bc, NULL, &dependent) != 0 ||
        flat_runtime_task_start(runtime, cancel_target) != 0 ||
        flat_runtime_task_start(runtime, dependent) != 0 ||
        flat_runtime_task_wait(runtime, dependent, cancel_target) != 0 ||
        expect_state(runtime, dependent, FLAT_TASK_WAITING) != 0 ||
        flat_runtime_task_destroy(runtime, cancel_target) == 0) return 17;
    if (flat_runtime_task_cancel(runtime, cancel_target) != 0 ||
        expect_state(runtime, cancel_target, FLAT_TASK_CANCELLED) != 0 ||
        expect_state(runtime, dependent, FLAT_TASK_RUNNABLE) != 0) return 18;
    if (flat_runtime_task_result(runtime, cancel_target, &result) != 0 ||
        result.termination != FLAT_TERM_CANCELLED) return 19;
    if (flat_runtime_schedule(runtime, 2, 0, &turns) != 0 ||
        expect_state(runtime, dependent, FLAT_TASK_COMPLETED) != 0) return 20;

    /* A failed target also wakes its waiters. */
    if (flat_runtime_task_create(runtime, &fail_bc, NULL, &fail_target) != 0 ||
        flat_runtime_task_create(runtime, &bc, NULL, &fail_waiter) != 0 ||
        flat_runtime_task_start(runtime, fail_target) != 0 ||
        flat_runtime_task_start(runtime, fail_waiter) != 0 ||
        flat_runtime_task_wait(runtime, fail_waiter, fail_target) != 0) return 21;
    for (i = 0; i < 64 && flat_runtime_task_state(runtime, fail_target) != FLAT_TASK_FAILED &&
                flat_runtime_task_state(runtime, fail_target) != FLAT_TASK_COMPLETED; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) return 22;
    }
    if (expect_state(runtime, fail_target, FLAT_TASK_FAILED) != 0 ||
        expect_state(runtime, fail_waiter, FLAT_TASK_RUNNABLE) != 0 ||
        flat_runtime_task_result(runtime, fail_target, &result) != 0 ||
        result.termination != FLAT_TERM_FAIL) return 23;
    if (flat_runtime_schedule(runtime, 2, 0, &turns) != 0 ||
        expect_state(runtime, fail_waiter, FLAT_TASK_COMPLETED) != 0) return 24;

    /* A two/three-node wait cycle is rejected before it can deadlock the scheduler. */
    if (flat_runtime_task_create(runtime, &bc, NULL, &cycle_a) != 0 ||
        flat_runtime_task_create(runtime, &bc, NULL, &cycle_b) != 0 ||
        flat_runtime_task_create(runtime, &bc, NULL, &cycle_c) != 0 ||
        flat_runtime_task_start(runtime, cycle_a) != 0 ||
        flat_runtime_task_start(runtime, cycle_b) != 0 ||
        flat_runtime_task_start(runtime, cycle_c) != 0 ||
        flat_runtime_task_wait(runtime, cycle_a, cycle_b) != 0 ||
        flat_runtime_task_wait(runtime, cycle_b, cycle_c) != 0 ||
        flat_runtime_task_wait(runtime, cycle_c, cycle_a) == 0 ||
        expect_state(runtime, cycle_a, FLAT_TASK_WAITING) != 0 ||
        expect_state(runtime, cycle_b, FLAT_TASK_WAITING) != 0 ||
        expect_state(runtime, cycle_c, FLAT_TASK_RUNNABLE) != 0) return 31;
    if (flat_runtime_task_cancel(runtime, cycle_a) != 0 ||
        flat_runtime_task_cancel(runtime, cycle_b) != 0 ||
        flat_runtime_task_cancel(runtime, cycle_c) != 0) return 32;

    /* Cancelling a WAITING task removes its dependency from the target. */
    if (flat_runtime_task_create(runtime, &bc, NULL, &dormant_target) != 0 ||
        flat_runtime_task_create(runtime, &bc, NULL, &cancelled_waiter) != 0 ||
        flat_runtime_task_start(runtime, cancelled_waiter) != 0 ||
        flat_runtime_task_wait(runtime, cancelled_waiter, dormant_target) != 0 ||
        expect_state(runtime, cancelled_waiter, FLAT_TASK_WAITING) != 0 ||
        flat_runtime_task_cancel(runtime, cancelled_waiter) != 0 ||
        expect_state(runtime, cancelled_waiter, FLAT_TASK_CANCELLED) != 0) return 25;
    if (flat_runtime_task_destroy(runtime, cancelled_waiter) != 0 ||
        flat_runtime_task_destroy(runtime, dormant_target) != 0) return 26;

    /* Cancellation after a yield stops execution at the saved instruction boundary. */
    if (flat_runtime_task_create(runtime, &bc, NULL, &yielded) != 0 ||
        flat_runtime_task_start(runtime, yielded) != 0 ||
        flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1 ||
        expect_state(runtime, yielded, FLAT_TASK_RUNNABLE) != 0) return 27;
    if (flat_runtime_task_cancel(runtime, yielded) != 0 ||
        expect_state(runtime, yielded, FLAT_TASK_CANCELLED) != 0 ||
        flat_runtime_task_result(runtime, yielded, &result) != 0 ||
        result.state != FLAT_VM_CANCELLED || result.termination != FLAT_TERM_CANCELLED ||
        flat_runtime_task_cancel(runtime, UINT64_MAX) == 0) return 28;

    if (flat_runtime_task_destroy(runtime, target) != 0 ||
        flat_runtime_task_destroy(runtime, waiter) != 0 ||
        flat_runtime_task_destroy(runtime, late_waiter) != 0 ||
        flat_runtime_task_destroy(runtime, cancelled_created) != 0 ||
        flat_runtime_task_destroy(runtime, cancel_target) != 0 ||
        flat_runtime_task_destroy(runtime, dependent) != 0 ||
        flat_runtime_task_destroy(runtime, yielded) != 0 ||
        flat_runtime_task_destroy(runtime, fail_target) != 0 ||
        flat_runtime_task_destroy(runtime, fail_waiter) != 0 ||
        flat_runtime_task_destroy(runtime, cycle_a) != 0 ||
        flat_runtime_task_destroy(runtime, cycle_b) != 0 ||
        flat_runtime_task_destroy(runtime, cycle_c) != 0) return 29;
    if (flat_runtime_destroy(runtime) != 0) return 30;
    flat_fb_free(&fail_bc);
    flat_fb_free(&bc);
    puts("task-wait-cancel-ok");
    return 0;
}
