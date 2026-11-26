#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"
#include "flat_native_capability.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TASK_COUNT 128
#define INITIALLY_STARTED (TASK_COUNT / 2)

typedef struct TestContext {
    FlatRuntime *runtime;
    FlatTaskId *tasks;
    atomic_int scheduler_done;
    atomic_int overlap_rejected;
    atomic_int running_cancel_rejected;
    atomic_int running_destroy_rejected;
    atomic_uint query_count;
    int scheduler_rc;
    uint64_t turns;
} TestContext;


typedef struct CleanupProbeContext {
    FlatRuntime *runtime;
    FlatTaskId task_id;
    int cancel;
    atomic_int cleanup_started;
    atomic_int cleanup_done;
    atomic_int query_ready;
    atomic_uint query_count;
    int cleanup_rc;
} CleanupProbeContext;

static void pause_one_ms(void) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000L;
    (void)nanosleep(&ts, NULL);
}

static void *cleanup_probe_main(void *opaque) {
    CleanupProbeContext *ctx = (CleanupProbeContext *)opaque;
    atomic_store_explicit(&ctx->cleanup_started, 1, memory_order_release);
    ctx->cleanup_rc = ctx->cancel
        ? flat_runtime_task_cancel(ctx->runtime, ctx->task_id)
        : flat_runtime_task_destroy(ctx->runtime, ctx->task_id);
    atomic_store_explicit(&ctx->cleanup_done, 1, memory_order_release);
    return NULL;
}

static void *cleanup_query_main(void *opaque) {
    CleanupProbeContext *ctx = (CleanupProbeContext *)opaque;
    atomic_store_explicit(&ctx->query_ready, 1, memory_order_release);
    while (!atomic_load_explicit(&ctx->cleanup_done, memory_order_acquire)) {
        (void)flat_runtime_task_state(ctx->runtime, ctx->task_id);
        atomic_fetch_add_explicit(&ctx->query_count, 1, memory_order_relaxed);
    }
    return NULL;
}

/* Hold the DOM invocation gate so VM teardown blocks there. Concurrent task
 * queries must continue, proving scheduler_mutex is not held across teardown. */
static int verify_vm_teardown_outside_scheduler_lock(FlatRuntime *runtime,
                                                       FlatTaskId task_id, int cancel) {
    CleanupProbeContext ctx;
    pthread_t cleanup_thread, query_thread;
    int cleanup_created = 0, query_created = 0;
    unsigned before, after;
    int rc = -1;
    memset(&ctx, 0, sizeof ctx);
    ctx.runtime = runtime;
    ctx.task_id = task_id;
    ctx.cancel = cancel;
    atomic_init(&ctx.cleanup_started, 0);
    atomic_init(&ctx.cleanup_done, 0);
    atomic_init(&ctx.query_ready, 0);
    atomic_init(&ctx.query_count, 0);

    if (flat_native_module_gate_enter("dom") != 0) return -1;
    if (pthread_create(&query_thread, NULL, cleanup_query_main, &ctx) != 0) goto done;
    query_created = 1;
    while (!atomic_load_explicit(&ctx.query_ready, memory_order_acquire)) pause_one_ms();
    if (pthread_create(&cleanup_thread, NULL, cleanup_probe_main, &ctx) != 0) goto done;
    cleanup_created = 1;
    while (!atomic_load_explicit(&ctx.cleanup_started, memory_order_acquire)) pause_one_ms();

    /* Allow the cleanup thread to reach the held DOM gate, then observe whether
     * the query thread can still acquire the runtime scheduler mutex. */
    for (int i = 0; i < 30; ++i) pause_one_ms();
    before = atomic_load_explicit(&ctx.query_count, memory_order_relaxed);
    for (int i = 0; i < 30; ++i) pause_one_ms();
    after = atomic_load_explicit(&ctx.query_count, memory_order_relaxed);
    if (after > before) rc = 0;

done:
    (void)flat_native_module_gate_leave("dom");
    if (cleanup_created) pthread_join(cleanup_thread, NULL);
    if (query_created) pthread_join(query_thread, NULL);
    if (rc == 0 && ctx.cleanup_rc != 0) rc = -1;
    return rc;
}

static void *scheduler_main(void *opaque) {
    TestContext *ctx = (TestContext *)opaque;
    ctx->scheduler_rc = flat_runtime_schedule(ctx->runtime, 1, 0, &ctx->turns);
    atomic_store_explicit(&ctx->scheduler_done, 1, memory_order_release);
    return NULL;
}

static void *query_main(void *opaque) {
    TestContext *ctx = (TestContext *)opaque;
    int overlap_attempted = 0;
    while (!atomic_load_explicit(&ctx->scheduler_done, memory_order_acquire)) {
        size_t i;
        for (i = 0; i < TASK_COUNT; ++i) {
            FlatTaskState state = flat_runtime_task_state(ctx->runtime, ctx->tasks[i]);
            if (state < FLAT_TASK_CREATED || state > FLAT_TASK_CANCELLED) return (void *)1;
            atomic_fetch_add_explicit(&ctx->query_count, 1, memory_order_relaxed);
            if (state == FLAT_TASK_RUNNING && !overlap_attempted) {
                uint64_t ignored = 0;
                int rc;
                size_t start_index;
                overlap_attempted = 1;
                if (flat_runtime_task_cancel(ctx->runtime, ctx->tasks[i]) == -1)
                    atomic_store_explicit(&ctx->running_cancel_rejected, 1, memory_order_release);
                else
                    return (void *)4;
                if (flat_runtime_task_destroy(ctx->runtime, ctx->tasks[i]) == -1)
                    atomic_store_explicit(&ctx->running_destroy_rejected, 1, memory_order_release);
                else
                    return (void *)5;
                for (start_index = INITIALLY_STARTED; start_index < TASK_COUNT; ++start_index) {
                    if (flat_runtime_task_start(ctx->runtime, ctx->tasks[start_index]) != 0)
                        return (void *)3;
                }
                rc = flat_runtime_schedule(ctx->runtime, 1, 1, &ignored);
                if (rc == -1)
                    atomic_store_explicit(&ctx->overlap_rejected, 1, memory_order_release);
                else
                    return (void *)2;
            }
            if (atomic_load_explicit(&ctx->scheduler_done, memory_order_acquire)) break;
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    FlatBytecode bc;
    FlatBytecode dom_bc;
    FlatRuntime *runtime = NULL;
    FlatTaskId *tasks = NULL;
    TestContext ctx;
    pthread_t scheduler_thread, query_thread;
    int scheduler_created = 0, query_created = 0;
    void *query_result = NULL;
    int rc = 0;
    size_t i;

    FlatTaskId dom_task = 0;
    uint64_t dom_turns = 0;
    memset(&bc, 0, sizeof bc);
    memset(&dom_bc, 0, sizeof dom_bc);
    memset(&ctx, 0, sizeof ctx);
    if (argc != 3 || flat_fb_load_file(argv[1], &bc) != 0 ||
        flat_fb_load_file(argv[2], &dom_bc) != 0) return 2;
    runtime = flat_runtime_create(NULL);
    tasks = (FlatTaskId *)calloc(TASK_COUNT, sizeof *tasks);
    if (!runtime || !tasks) { rc = 3; goto done; }
    ctx.runtime = runtime;
    ctx.tasks = tasks;
    atomic_init(&ctx.scheduler_done, 0);
    atomic_init(&ctx.overlap_rejected, 0);
    atomic_init(&ctx.running_cancel_rejected, 0);
    atomic_init(&ctx.running_destroy_rejected, 0);
    atomic_init(&ctx.query_count, 0);

    for (i = 0; i < TASK_COUNT; ++i) {
        if (flat_runtime_task_create(runtime, &bc, NULL, &tasks[i]) != 0 ||
            (i < INITIALLY_STARTED && flat_runtime_task_start(runtime, tasks[i]) != 0)) {
            rc = 4;
            goto done;
        }
    }
    if (pthread_create(&query_thread, NULL, query_main, &ctx) != 0) { rc = 5; goto done; }
    query_created = 1;
    if (pthread_create(&scheduler_thread, NULL, scheduler_main, &ctx) != 0) { rc = 6; goto done; }
    scheduler_created = 1;

    pthread_join(scheduler_thread, NULL);
    scheduler_created = 0;
    pthread_join(query_thread, &query_result);
    query_created = 0;

    if (ctx.scheduler_rc != 0 || ctx.turns < TASK_COUNT || query_result != NULL ||
        !atomic_load_explicit(&ctx.overlap_rejected, memory_order_acquire) ||
        !atomic_load_explicit(&ctx.running_cancel_rejected, memory_order_acquire) ||
        !atomic_load_explicit(&ctx.running_destroy_rejected, memory_order_acquire) ||
        atomic_load_explicit(&ctx.query_count, memory_order_relaxed) == 0) {
        rc = 7;
        goto done;
    }
    for (i = 0; i < TASK_COUNT; ++i) {
        FlatVmResult result;
        if (flat_runtime_task_state(runtime, tasks[i]) != FLAT_TASK_COMPLETED ||
            flat_runtime_task_result(runtime, tasks[i], &result) != 0 ||
            result.termination != FLAT_TERM_OK ||
            flat_runtime_task_destroy(runtime, tasks[i]) != 0) {
            rc = 8;
            goto done;
        }
        tasks[i] = 0;
    }
    if (flat_runtime_destroy(runtime) != 0) { rc = 9; goto done; }
    runtime = NULL;

    runtime = flat_runtime_create(NULL);
    if (!runtime || flat_runtime_task_create(runtime, &dom_bc, NULL, &dom_task) != 0 ||
        flat_runtime_task_start(runtime, dom_task) != 0 ||
        flat_runtime_schedule(runtime, 2, 1, &dom_turns) != 0 || dom_turns != 1 ||
        flat_runtime_task_state(runtime, dom_task) != FLAT_TASK_RUNNABLE ||
        verify_vm_teardown_outside_scheduler_lock(runtime, dom_task, 1) != 0 ||
        flat_runtime_task_state(runtime, dom_task) != FLAT_TASK_CANCELLED ||
        flat_runtime_task_destroy(runtime, dom_task) != 0) {
        rc = 10;
        goto done;
    }
    dom_task = 0;
    if (flat_runtime_task_create(runtime, &dom_bc, NULL, &dom_task) != 0 ||
        flat_runtime_task_state(runtime, dom_task) != FLAT_TASK_CREATED ||
        verify_vm_teardown_outside_scheduler_lock(runtime, dom_task, 0) != 0 ||
        flat_runtime_task_state(runtime, dom_task) != (FlatTaskState)0) {
        rc = 11;
        goto done;
    }
    dom_task = 0;
    if (flat_runtime_destroy(runtime) != 0) { rc = 12; goto done; }
    runtime = NULL;

done:
    if (scheduler_created) pthread_join(scheduler_thread, NULL);
    if (query_created) pthread_join(query_thread, NULL);
    if (runtime) {
        if (dom_task) {
            (void)flat_runtime_task_cancel(runtime, dom_task);
            (void)flat_runtime_task_destroy(runtime, dom_task);
        }
        for (i = 0; i < TASK_COUNT; ++i) {
            if (tasks && tasks[i]) {
                (void)flat_runtime_task_cancel(runtime, tasks[i]);
                (void)flat_runtime_task_destroy(runtime, tasks[i]);
            }
        }
        (void)flat_runtime_destroy(runtime);
    }
    free(tasks);
    flat_fb_free(&dom_bc);
    flat_fb_free(&bc);
    if (rc) fprintf(stderr, "runtime scheduler mutex test failed: %d\n", rc);
    else printf("runtime-scheduler-mutex-ok tasks=%d started-during-run=%d queries=%u overlap=reject running-cancel=reject running-destroy=reject teardown=outside-lock single-driver=preserved\n",
                TASK_COUNT, TASK_COUNT - INITIALLY_STARTED,
                atomic_load_explicit(&ctx.query_count, memory_order_relaxed));
    return rc;
}
