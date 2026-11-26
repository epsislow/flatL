#include "fb_format.h"
#include "flat_channel.h"
#include "flat_task.h"
#include "flat_runtime.h"
#include "flat_value.h"
#include "flat_runtime_log.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    static FlatBytecode owner_bc, writer_bc, reader_bc, locker_bc, fail_owner_bc;
    static FlatBytecode lock_while_holding_bc, set_while_holding_bc, var_while_holding_bc;
    static FlatBytecode waiter_channel_two_bc;
    FlatRuntime *runtime = NULL;
    FlatTaskId owner = 0, writer = 0, reader = 0, locker = 0;
    FlatTaskId cancelled_owner = 0, cancel_waiter = 0;
    FlatTaskId failed_owner = 0, fail_waiter = 0;
    FlatTaskId destroy_owner = 0, destroy_waiter = 0;
    FlatTaskId wake_owner = 0, wake_waiter = 0;
    FlatTaskId second_channel_owner = 0, nested_waiter = 0, set_waiter = 0;
    FlatTaskId var_waiter = 0, channel_probe = 0;
    FlatTaskId multi_owner = 0, channel_one_waiter = 0, channel_two_waiter = 0;
    FlatTaskId graph_owner = 0, graph_channel_waiter = 0;
    FlatTaskId graph_waiter_on_channel = 0, graph_waiter_on_owner = 0;
    FlatChannelId channel = 0, second_channel = 0;
    FlatCollectionPool *coll = NULL;
    FlatStringPool *pool = NULL;
    FlatValue value;
    uint64_t turns = 0;
    int i, rc = 0;

    memset(&owner_bc, 0, sizeof owner_bc);
    memset(&writer_bc, 0, sizeof writer_bc);
    memset(&reader_bc, 0, sizeof reader_bc);
    memset(&locker_bc, 0, sizeof locker_bc);
    memset(&fail_owner_bc, 0, sizeof fail_owner_bc);
    memset(&lock_while_holding_bc, 0, sizeof lock_while_holding_bc);
    memset(&set_while_holding_bc, 0, sizeof set_while_holding_bc);
    memset(&var_while_holding_bc, 0, sizeof var_while_holding_bc);
    memset(&waiter_channel_two_bc, 0, sizeof waiter_channel_two_bc);
    memset(&value, 0, sizeof value);
    if (argc != 10 || flat_fb_load_file(argv[1], &owner_bc) != 0 ||
        flat_fb_load_file(argv[2], &writer_bc) != 0 ||
        flat_fb_load_file(argv[3], &reader_bc) != 0 ||
        flat_fb_load_file(argv[4], &locker_bc) != 0 ||
        flat_fb_load_file(argv[5], &fail_owner_bc) != 0 ||
        flat_fb_load_file(argv[6], &lock_while_holding_bc) != 0 ||
        flat_fb_load_file(argv[7], &set_while_holding_bc) != 0 ||
        flat_fb_load_file(argv[8], &var_while_holding_bc) != 0 ||
        flat_fb_load_file(argv[9], &waiter_channel_two_bc) != 0) return 2;
    runtime = flat_runtime_create(NULL);
    coll = flat_coll_create();
    pool = flat_pool_create();
    if (!runtime || !coll || !pool || flat_runtime_channel_create(runtime, &channel) != 0 ||
        channel != 1) { rc = 3; goto done; }
    flat_value_set_number(&value, 7.0);
    if (flat_runtime_channel_set(runtime, channel, 0, "seed", &value, coll, pool, NULL) != 0) {
        rc = 4; goto done;
    }
    if (flat_runtime_task_create(runtime, &owner_bc, NULL, &owner) != 0 ||
        flat_runtime_task_create(runtime, &writer_bc, NULL, &writer) != 0 ||
        flat_runtime_task_create(runtime, &reader_bc, NULL, &reader) != 0 ||
        flat_runtime_task_create(runtime, &locker_bc, NULL, &locker) != 0) { rc = 5; goto done; }

    /* Mixed wait graph: a Channel waiter is itself the target of task.wait,
     * while another task waits directly for the Channel owner. Releasing the
     * Channel wakes only its direct waiter; completing that waiter wakes its
     * dependent; completing the owner wakes the independent task.wait edge. */
    if (flat_runtime_task_create(runtime, &reader_bc, NULL, &graph_owner) != 0 ||
        flat_runtime_task_create(runtime, &locker_bc, NULL, &graph_channel_waiter) != 0 ||
        flat_runtime_task_create(runtime, &reader_bc, NULL, &graph_waiter_on_channel) != 0 ||
        flat_runtime_task_create(runtime, &reader_bc, NULL, &graph_waiter_on_owner) != 0 ||
        flat_runtime_channel_lock(runtime, channel, graph_owner) != 0 ||
        flat_runtime_task_start(runtime, graph_channel_waiter) != 0) { rc = 43; goto done; }
    for (i = 0; i < 16 && flat_runtime_task_state(runtime, graph_channel_waiter) != FLAT_TASK_WAITING; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) { rc = 44; goto done; }
    }
    if (flat_runtime_task_state(runtime, graph_channel_waiter) != FLAT_TASK_WAITING ||
        flat_runtime_task_start(runtime, graph_waiter_on_channel) != 0 ||
        flat_runtime_task_start(runtime, graph_waiter_on_owner) != 0 ||
        flat_runtime_task_wait(runtime, graph_waiter_on_channel, graph_channel_waiter) != 0 ||
        flat_runtime_task_wait(runtime, graph_waiter_on_owner, graph_owner) != 0 ||
        flat_runtime_task_state(runtime, graph_waiter_on_channel) != FLAT_TASK_WAITING ||
        flat_runtime_task_state(runtime, graph_waiter_on_owner) != FLAT_TASK_WAITING) {
        rc = 45; goto done;
    }
    if (flat_runtime_channel_unlock(runtime, channel, graph_owner) != 0 ||
        flat_runtime_task_state(runtime, graph_channel_waiter) != FLAT_TASK_RUNNABLE ||
        flat_runtime_task_state(runtime, graph_waiter_on_channel) != FLAT_TASK_WAITING ||
        flat_runtime_task_state(runtime, graph_waiter_on_owner) != FLAT_TASK_WAITING) {
        rc = 46; goto done;
    }
    for (i = 0; i < 16 && flat_runtime_task_state(runtime, graph_channel_waiter) != FLAT_TASK_COMPLETED; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) { rc = 47; goto done; }
    }
    if (flat_runtime_task_state(runtime, graph_channel_waiter) != FLAT_TASK_COMPLETED ||
        flat_runtime_task_state(runtime, graph_waiter_on_channel) != FLAT_TASK_RUNNABLE ||
        flat_runtime_task_state(runtime, graph_waiter_on_owner) != FLAT_TASK_WAITING) {
        rc = 48; goto done;
    }
    for (i = 0; i < 16 && flat_runtime_task_state(runtime, graph_waiter_on_channel) != FLAT_TASK_COMPLETED; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) { rc = 51; goto done; }
    }
    if (flat_runtime_task_state(runtime, graph_waiter_on_channel) != FLAT_TASK_COMPLETED ||
        flat_runtime_task_state(runtime, graph_waiter_on_owner) != FLAT_TASK_WAITING ||
        flat_runtime_task_start(runtime, graph_owner) != 0) {
        rc = 48; goto done;
    }
    for (i = 0; i < 16 && flat_runtime_task_state(runtime, graph_owner) != FLAT_TASK_COMPLETED; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) { rc = 49; goto done; }
    }
    if (flat_runtime_task_state(runtime, graph_owner) != FLAT_TASK_COMPLETED ||
        flat_runtime_task_state(runtime, graph_waiter_on_owner) != FLAT_TASK_RUNNABLE) {
        rc = 52; goto done;
    }
    for (i = 0; i < 16 && flat_runtime_task_state(runtime, graph_waiter_on_owner) != FLAT_TASK_COMPLETED; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) { rc = 53; goto done; }
    }
    if (flat_runtime_task_state(runtime, graph_waiter_on_owner) != FLAT_TASK_COMPLETED ||
        flat_runtime_task_destroy(runtime, graph_waiter_on_owner) != 0 ||
        flat_runtime_task_destroy(runtime, graph_waiter_on_channel) != 0 ||
        flat_runtime_task_destroy(runtime, graph_channel_waiter) != 0 ||
        flat_runtime_task_destroy(runtime, graph_owner) != 0) {
        rc = 50; goto done;
    }
    graph_waiter_on_owner = graph_waiter_on_channel = graph_channel_waiter = graph_owner = 0;

    /* One owner may hold two Channel transactions. A waiter on Channel 1 is
     * woken by explicit commit; a waiter on Channel 2 is woken by terminal
     * cleanup, which aborts the owner's remaining transaction. */
    if (flat_runtime_channel_create(runtime, &second_channel) != 0 || second_channel != 2 ||
        flat_runtime_task_create(runtime, &owner_bc, NULL, &multi_owner) != 0 ||
        flat_runtime_task_create(runtime, &locker_bc, NULL, &channel_one_waiter) != 0 ||
        flat_runtime_task_create(runtime, &waiter_channel_two_bc, NULL, &channel_two_waiter) != 0 ||
        flat_runtime_channel_lock(runtime, channel, multi_owner) != 0 ||
        flat_runtime_channel_lock(runtime, second_channel, multi_owner) != 0 ||
        flat_runtime_task_start(runtime, channel_one_waiter) != 0 ||
        flat_runtime_task_start(runtime, channel_two_waiter) != 0) { rc = 36; goto done; }
    for (i = 0; i < 16 && flat_runtime_task_state(runtime, channel_one_waiter) != FLAT_TASK_WAITING; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) { rc = 37; goto done; }
    }
    for (i = 0; i < 16 && flat_runtime_task_state(runtime, channel_two_waiter) != FLAT_TASK_WAITING; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) { rc = 38; goto done; }
    }
    if (flat_runtime_task_state(runtime, channel_one_waiter) != FLAT_TASK_WAITING ||
        flat_runtime_task_state(runtime, channel_two_waiter) != FLAT_TASK_WAITING ||
        flat_runtime_task_start(runtime, multi_owner) != 0) { rc = 39; goto done; }
    for (i = 0; i < 16 && flat_runtime_task_state(runtime, multi_owner) != FLAT_TASK_COMPLETED; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) { rc = 40; goto done; }
    }
    if (flat_runtime_task_state(runtime, multi_owner) != FLAT_TASK_COMPLETED ||
        flat_runtime_task_state(runtime, channel_one_waiter) != FLAT_TASK_RUNNABLE ||
        flat_runtime_task_state(runtime, channel_two_waiter) != FLAT_TASK_RUNNABLE ||
        flat_runtime_schedule(runtime, 1, 0, &turns) != 0 || turns == 0 ||
        flat_runtime_task_state(runtime, channel_one_waiter) != FLAT_TASK_COMPLETED ||
        flat_runtime_task_state(runtime, channel_two_waiter) != FLAT_TASK_COMPLETED) {
        fprintf(stderr, "multi-channel wakeup: owner=%d waiter1=%d waiter2=%d\n",
                (int)flat_runtime_task_state(runtime, multi_owner),
                (int)flat_runtime_task_state(runtime, channel_one_waiter),
                (int)flat_runtime_task_state(runtime, channel_two_waiter));
        rc = 41; goto done;
    }
    if (flat_runtime_task_destroy(runtime, channel_one_waiter) != 0 ||
        flat_runtime_task_destroy(runtime, channel_two_waiter) != 0 ||
        flat_runtime_task_destroy(runtime, multi_owner) != 0 ||
        flat_runtime_channel_destroy(runtime, second_channel) != 0) { rc = 42; goto done; }
    second_channel = 0;

    /* The host pre-acquires the transaction on behalf of a CREATED owner task.
     * Writer, reader, and locker each encounter contention in their first native
     * operation. Each must WAIT and retry that same OP_NATIVE after unlock. */
    if (flat_runtime_channel_lock(runtime, channel, owner) != 0 ||
        flat_runtime_task_start(runtime, writer) != 0) { rc = 6; goto done; }
    for (i = 0; i < 16 && flat_runtime_task_state(runtime, writer) != FLAT_TASK_WAITING; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) { rc = 7; goto done; }
    }
    if (flat_runtime_task_state(runtime, writer) != FLAT_TASK_WAITING ||
        flat_runtime_task_start(runtime, reader) != 0) { rc = 8; goto done; }
    for (i = 0; i < 16 && flat_runtime_task_state(runtime, reader) != FLAT_TASK_WAITING; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) { rc = 9; goto done; }
    }
    if (flat_runtime_task_state(runtime, reader) != FLAT_TASK_WAITING ||
        flat_runtime_task_start(runtime, locker) != 0) { rc = 10; goto done; }
    for (i = 0; i < 16 && flat_runtime_task_state(runtime, locker) != FLAT_TASK_WAITING; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) { rc = 11; goto done; }
    }
    if (flat_runtime_task_state(runtime, locker) != FLAT_TASK_WAITING) { rc = 12; goto done; }
    if (flat_runtime_task_destroy(runtime, owner) == 0) { rc = 13; goto done; }
    if (flat_runtime_task_start(runtime, owner) != 0 ||
        flat_runtime_schedule(runtime, 1, 0, &turns) != 0 || turns == 0) { rc = 14; goto done; }
    if (flat_runtime_task_state(runtime, owner) != FLAT_TASK_COMPLETED ||
        flat_runtime_task_state(runtime, writer) != FLAT_TASK_COMPLETED ||
        flat_runtime_task_state(runtime, reader) != FLAT_TASK_COMPLETED ||
        flat_runtime_task_state(runtime, locker) != FLAT_TASK_COMPLETED) {
        fprintf(stderr, "contention tasks: owner=%d writer=%d reader=%d locker=%d\n",
                (int)flat_runtime_task_state(runtime, owner),
                (int)flat_runtime_task_state(runtime, writer),
                (int)flat_runtime_task_state(runtime, reader),
                (int)flat_runtime_task_state(runtime, locker));
        rc = 15; goto done;
    }
    if (flat_runtime_channel_var(runtime, channel, 0, "written", coll, pool,
                                 NULL, &value) != 0 || value.tag != VAL_NUMBER ||
        value.u.number.payload.f64 != 11.0) { rc = 16; goto done; }
    flat_value_release(coll, pool, &value);
    value.tag = VAL_EMPTY;
    if (flat_runtime_channel_var(runtime, channel, 0, "acquired", coll, pool,
                                 NULL, &value) != 0 || value.tag != VAL_NUMBER ||
        value.u.number.payload.f64 != 1.0) { rc = 17; goto done; }
    flat_value_release(coll, pool, &value);
    value.tag = VAL_EMPTY;

    /* A waiter must also be woken when its transaction owner is cancelled.
     * Cancellation rolls back the owner's transaction before retrying the
     * original channel.lock instruction in the waiter. */
    if (flat_runtime_task_create(runtime, &owner_bc, NULL, &cancelled_owner) != 0 ||
        flat_runtime_task_create(runtime, &locker_bc, NULL, &cancel_waiter) != 0 ||
        flat_runtime_channel_lock(runtime, channel, cancelled_owner) != 0 ||
        flat_runtime_task_start(runtime, cancel_waiter) != 0) { rc = 18; goto done; }
    for (i = 0; i < 16 && flat_runtime_task_state(runtime, cancel_waiter) != FLAT_TASK_WAITING; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) { rc = 19; goto done; }
    }
    if (flat_runtime_task_state(runtime, cancel_waiter) != FLAT_TASK_WAITING ||
        flat_runtime_task_cancel(runtime, cancelled_owner) != 0 ||
        flat_runtime_task_state(runtime, cancelled_owner) != FLAT_TASK_CANCELLED ||
        flat_runtime_schedule(runtime, 1, 0, &turns) != 0 || turns == 0 ||
        flat_runtime_task_state(runtime, cancel_waiter) != FLAT_TASK_COMPLETED) {
        fprintf(stderr, "cancel wakeup: owner=%d waiter=%d\n",
                (int)flat_runtime_task_state(runtime, cancelled_owner),
                (int)flat_runtime_task_state(runtime, cancel_waiter));
        rc = 20; goto done;
    }
    /* A VM failure in the transaction owner follows the same rollback and
     * dependency-wakeup contract as cancellation. */
    if (flat_runtime_task_create(runtime, &fail_owner_bc, NULL, &failed_owner) != 0 ||
        flat_runtime_task_create(runtime, &locker_bc, NULL, &fail_waiter) != 0 ||
        flat_runtime_channel_lock(runtime, channel, failed_owner) != 0 ||
        flat_runtime_task_start(runtime, fail_waiter) != 0) { rc = 22; goto done; }
    for (i = 0; i < 16 && flat_runtime_task_state(runtime, fail_waiter) != FLAT_TASK_WAITING; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) { rc = 23; goto done; }
    }
    if (flat_runtime_task_state(runtime, fail_waiter) != FLAT_TASK_WAITING ||
        flat_runtime_task_start(runtime, failed_owner) != 0 ||
        flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1 ||
        flat_runtime_task_state(runtime, failed_owner) != FLAT_TASK_FAILED ||
        flat_runtime_task_state(runtime, fail_waiter) != FLAT_TASK_RUNNABLE ||
        flat_runtime_schedule(runtime, 1, 0, &turns) != 0 || turns == 0 ||
        flat_runtime_task_state(runtime, fail_waiter) != FLAT_TASK_COMPLETED) {
        fprintf(stderr, "failure wakeup: owner=%d waiter=%d\n",
                (int)flat_runtime_task_state(runtime, failed_owner),
                (int)flat_runtime_task_state(runtime, fail_waiter));
        rc = 24; goto done;
    }

    /* A task with a Channel waiter cannot be destroyed. Cancelling and
     * destroying the waiter must release its Channel reference; destroying
     * the owner then aborts its still-open transaction and permits Channel
     * destruction. */
    if (flat_runtime_task_create(runtime, &owner_bc, NULL, &destroy_owner) != 0 ||
        flat_runtime_task_create(runtime, &locker_bc, NULL, &destroy_waiter) != 0 ||
        flat_runtime_channel_lock(runtime, channel, destroy_owner) != 0 ||
        flat_runtime_task_start(runtime, destroy_waiter) != 0) { rc = 25; goto done; }
    for (i = 0; i < 16 && flat_runtime_task_state(runtime, destroy_waiter) != FLAT_TASK_WAITING; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) { rc = 26; goto done; }
    }
    if (flat_runtime_task_state(runtime, destroy_waiter) != FLAT_TASK_WAITING ||
        flat_runtime_task_destroy(runtime, destroy_owner) == 0 ||
        flat_runtime_channel_destroy(runtime, channel) == 0 ||
        flat_runtime_task_cancel(runtime, destroy_waiter) != 0 ||
        flat_runtime_task_state(runtime, destroy_waiter) != FLAT_TASK_CANCELLED ||
        flat_runtime_task_destroy(runtime, destroy_waiter) != 0 ||
        flat_runtime_task_destroy(runtime, destroy_owner) != 0) {
        rc = 27; goto done;
    }

    /* A waiter woken by commit is RUNNABLE, not destroyable yet. Cancellation
     * before its retry must still release the Channel reference cleanly. */
    if (flat_runtime_task_create(runtime, &owner_bc, NULL, &wake_owner) != 0 ||
        flat_runtime_task_create(runtime, &locker_bc, NULL, &wake_waiter) != 0 ||
        flat_runtime_channel_lock(runtime, channel, wake_owner) != 0 ||
        flat_runtime_task_start(runtime, wake_waiter) != 0) { rc = 28; goto done; }
    for (i = 0; i < 16 && flat_runtime_task_state(runtime, wake_waiter) != FLAT_TASK_WAITING; ++i) {
        if (flat_runtime_schedule(runtime, 1, 1, &turns) != 0 || turns != 1) { rc = 29; goto done; }
    }
    if (flat_runtime_task_state(runtime, wake_waiter) != FLAT_TASK_WAITING ||
        flat_runtime_channel_unlock(runtime, channel, wake_owner) != 0 ||
        flat_runtime_task_state(runtime, wake_waiter) != FLAT_TASK_RUNNABLE ||
        flat_runtime_task_destroy(runtime, wake_waiter) == 0 ||
        flat_runtime_task_cancel(runtime, wake_waiter) != 0 ||
        flat_runtime_task_destroy(runtime, wake_waiter) != 0 ||
        flat_runtime_task_destroy(runtime, wake_owner) != 0 ||
        flat_runtime_channel_destroy(runtime, channel) != 0) {
        rc = 30; goto done;
    }
    channel = 0;

    /* A task must not suspend on Channel 2 while it owns a transaction on
     * Channel 1. The failed operation must trigger normal task cleanup, which
     * rolls back Channel 1 instead of leaving it locked behind a WAITING task. */
    if (flat_runtime_channel_create(runtime, &channel) != 0 ||
        flat_runtime_channel_create(runtime, &second_channel) != 0 ||
        channel != 3 || second_channel != 4 ||
        flat_runtime_task_create(runtime, &owner_bc, NULL, &second_channel_owner) != 0 ||
        flat_runtime_channel_lock(runtime, second_channel, second_channel_owner) != 0) {
        rc = 31; goto done;
    }
    {
        const FlatBytecode *programs[] = {
            &lock_while_holding_bc, &set_while_holding_bc, &var_while_holding_bc
        };
        FlatTaskId *task_ids[] = { &nested_waiter, &set_waiter, &var_waiter };
        size_t pi;
        for (pi = 0; pi < sizeof programs / sizeof programs[0]; ++pi) {
            if (flat_runtime_task_create(runtime, programs[pi], NULL, task_ids[pi]) != 0 ||
                flat_runtime_task_start(runtime, *task_ids[pi]) != 0 ||
                flat_runtime_schedule(runtime, 1, 0, &turns) != 0 || turns == 0 ||
                flat_runtime_task_state(runtime, *task_ids[pi]) != FLAT_TASK_FAILED) {
                fprintf(stderr, "nested Channel operation %lu was not rejected: state=%d\n",
                        (unsigned long)pi, (int)flat_runtime_task_state(runtime, *task_ids[pi]));
                rc = 32 + (int)pi; goto done;
            }
        }
    }
    if (flat_runtime_task_create(runtime, &owner_bc, NULL, &channel_probe) != 0 ||
        flat_runtime_channel_lock(runtime, channel, channel_probe) != 0 ||
        flat_runtime_channel_unlock(runtime, channel, channel_probe) != 0 ||
        flat_runtime_task_destroy(runtime, channel_probe) != 0 ||
        flat_runtime_task_destroy(runtime, nested_waiter) != 0 ||
        flat_runtime_task_destroy(runtime, set_waiter) != 0 ||
        flat_runtime_task_destroy(runtime, var_waiter) != 0 ||
        flat_runtime_task_destroy(runtime, second_channel_owner) != 0 ||
        flat_runtime_channel_destroy(runtime, channel) != 0 ||
        flat_runtime_channel_destroy(runtime, second_channel) != 0) {
        rc = 35; goto done;
    }
    channel = 0;
    second_channel = 0;

    if (flat_runtime_task_destroy(runtime, fail_waiter) != 0 ||
        flat_runtime_task_destroy(runtime, failed_owner) != 0 ||
        flat_runtime_task_destroy(runtime, cancel_waiter) != 0 ||
        flat_runtime_task_destroy(runtime, cancelled_owner) != 0 ||
        flat_runtime_task_destroy(runtime, locker) != 0 ||
        flat_runtime_task_destroy(runtime, reader) != 0 ||
        flat_runtime_task_destroy(runtime, writer) != 0 ||
        flat_runtime_task_destroy(runtime, owner) != 0) { rc = 21; goto done; }
    if (flat_runtime_destroy(runtime) != 0) { rc = 19; goto done; }
    runtime = NULL;

done:
    if (value.tag != VAL_EMPTY) flat_value_release(coll, pool, &value);
    flat_pool_destroy(pool);
    flat_coll_destroy(coll);
    if (runtime) {
        if (graph_waiter_on_owner) (void)flat_runtime_task_cancel(runtime, graph_waiter_on_owner);
        if (graph_waiter_on_channel) (void)flat_runtime_task_cancel(runtime, graph_waiter_on_channel);
        if (graph_channel_waiter) (void)flat_runtime_task_cancel(runtime, graph_channel_waiter);
        if (graph_owner) (void)flat_runtime_task_cancel(runtime, graph_owner);
        if (channel_two_waiter) (void)flat_runtime_task_cancel(runtime, channel_two_waiter);
        if (channel_one_waiter) (void)flat_runtime_task_cancel(runtime, channel_one_waiter);
        if (multi_owner) (void)flat_runtime_task_cancel(runtime, multi_owner);
        if (wake_waiter) (void)flat_runtime_task_cancel(runtime, wake_waiter);
        if (wake_owner) (void)flat_runtime_task_cancel(runtime, wake_owner);
        if (channel_probe) (void)flat_runtime_task_cancel(runtime, channel_probe);
        if (nested_waiter) (void)flat_runtime_task_cancel(runtime, nested_waiter);
        if (set_waiter) (void)flat_runtime_task_cancel(runtime, set_waiter);
        if (var_waiter) (void)flat_runtime_task_cancel(runtime, var_waiter);
        if (second_channel_owner) (void)flat_runtime_task_cancel(runtime, second_channel_owner);
        if (destroy_waiter) (void)flat_runtime_task_cancel(runtime, destroy_waiter);
        if (destroy_owner) (void)flat_runtime_task_cancel(runtime, destroy_owner);
        if (fail_waiter) (void)flat_runtime_task_cancel(runtime, fail_waiter);
        if (failed_owner) (void)flat_runtime_task_cancel(runtime, failed_owner);
        if (cancel_waiter) (void)flat_runtime_task_cancel(runtime, cancel_waiter);
        if (cancelled_owner) (void)flat_runtime_task_cancel(runtime, cancelled_owner);
        if (locker) (void)flat_runtime_task_cancel(runtime, locker);
        if (reader) (void)flat_runtime_task_cancel(runtime, reader);
        if (writer) (void)flat_runtime_task_cancel(runtime, writer);
        if (owner) (void)flat_runtime_task_cancel(runtime, owner);
        if (graph_waiter_on_owner) (void)flat_runtime_task_destroy(runtime, graph_waiter_on_owner);
        if (graph_waiter_on_channel) (void)flat_runtime_task_destroy(runtime, graph_waiter_on_channel);
        if (graph_channel_waiter) (void)flat_runtime_task_destroy(runtime, graph_channel_waiter);
        if (graph_owner) (void)flat_runtime_task_destroy(runtime, graph_owner);
        if (channel_two_waiter) (void)flat_runtime_task_destroy(runtime, channel_two_waiter);
        if (channel_one_waiter) (void)flat_runtime_task_destroy(runtime, channel_one_waiter);
        if (multi_owner) (void)flat_runtime_task_destroy(runtime, multi_owner);
        if (wake_waiter) (void)flat_runtime_task_destroy(runtime, wake_waiter);
        if (wake_owner) (void)flat_runtime_task_destroy(runtime, wake_owner);
        if (channel_probe) (void)flat_runtime_task_destroy(runtime, channel_probe);
        if (nested_waiter) (void)flat_runtime_task_destroy(runtime, nested_waiter);
        if (set_waiter) (void)flat_runtime_task_destroy(runtime, set_waiter);
        if (var_waiter) (void)flat_runtime_task_destroy(runtime, var_waiter);
        if (second_channel_owner) (void)flat_runtime_task_destroy(runtime, second_channel_owner);
        if (destroy_waiter) (void)flat_runtime_task_destroy(runtime, destroy_waiter);
        if (destroy_owner) (void)flat_runtime_task_destroy(runtime, destroy_owner);
        if (fail_waiter) (void)flat_runtime_task_destroy(runtime, fail_waiter);
        if (failed_owner) (void)flat_runtime_task_destroy(runtime, failed_owner);
        if (cancel_waiter) (void)flat_runtime_task_destroy(runtime, cancel_waiter);
        if (cancelled_owner) (void)flat_runtime_task_destroy(runtime, cancelled_owner);
        if (locker) (void)flat_runtime_task_destroy(runtime, locker);
        if (reader) (void)flat_runtime_task_destroy(runtime, reader);
        if (writer) (void)flat_runtime_task_destroy(runtime, writer);
        if (owner) (void)flat_runtime_task_destroy(runtime, owner);
        if (channel) (void)flat_runtime_channel_destroy(runtime, channel);
        if (second_channel) (void)flat_runtime_channel_destroy(runtime, second_channel);
        (void)flat_runtime_destroy(runtime);
    }
    flat_fb_free(&owner_bc);
    flat_fb_free(&writer_bc);
    flat_fb_free(&reader_bc);
    flat_fb_free(&locker_bc);
    flat_fb_free(&fail_owner_bc);
    flat_fb_free(&lock_while_holding_bc);
    flat_fb_free(&set_while_holding_bc);
    flat_fb_free(&var_while_holding_bc);
    flat_runtime_log_shutdown();
    if (rc) fprintf(stderr, "Channel lock contention test failed: %d\n", rc);
    else puts("channel-lock-contention-ok");
    return rc;
}
