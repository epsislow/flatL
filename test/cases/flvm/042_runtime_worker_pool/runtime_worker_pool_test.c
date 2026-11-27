#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"
#include "flat_native_capability.h"
#include "flat_native_plugin.h"
#include "flat_native.h"

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TASKS 96
#define WORKERS 4

/* Test-only hooks compiled into this harness via FLAT_RUNTIME_TESTING. */
void flat_runtime_test_worker_fail_create_after(int successful_creates);
void flat_runtime_test_worker_fail_join_after(int successful_joins);
void flat_runtime_test_worker_join_delay_ms(int delay_ms);

static int load_bytecode(const char *path, FlatBytecode *bc) {
    memset(bc, 0, sizeof *bc);
    return flat_fb_load_file(path, bc);
}

static void pause_one_ms(void) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000L;
    (void)nanosleep(&ts, NULL);
}

static int run_drain_case(const FlatBytecode *bc) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    FlatTaskId ids[TASKS];
    size_t i;
    int rc = -1;
    memset(ids, 0, sizeof ids);
    if (!runtime) return -1;
    for (i = 0; i < TASKS; ++i) {
        if (flat_runtime_task_create(runtime, bc, NULL, &ids[i]) != 0 ||
            flat_runtime_task_start(runtime, ids[i]) != 0) { fprintf(stderr, "DRAIN setup task %zu failed\n", i); goto done; }
    }
    if (flat_runtime_workers_start(runtime, 0, 1) == 0 ||
        flat_runtime_workers_start(runtime, WORKERS, 0) == 0 ||
        flat_runtime_workers_start(runtime, WORKERS, 1) != 0 ||
        flat_runtime_workers_start(runtime, WORKERS, 1) == 0 ||
        flat_runtime_schedule(runtime, 1, 1, NULL) == 0 ||
        flat_runtime_run(runtime, bc, NULL) == 0 ||
        flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_DRAIN) != 0) {
        fprintf(stderr, "DRAIN startup/admission checks failed\n"); goto done;
    }

    for (i = 0; i < 10000 &&
         flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++i)
        pause_one_ms();
    if (flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE) {
        fprintf(stderr, "DRAIN wait failed status=%d life=%d\n", (int)flat_runtime_shutdown_status(runtime), (int)flat_runtime_lifecycle(runtime)); goto done;
    }
    for (i = 0; i < TASKS; ++i) {
        if (flat_runtime_task_state(runtime, ids[i]) != FLAT_TASK_COMPLETED) { fprintf(stderr, "DRAIN task %zu state=%d\n", i, (int)flat_runtime_task_state(runtime, ids[i])); goto done; }
    }
    for (i = 0; i < TASKS; ++i) {
        if (flat_runtime_task_destroy(runtime, ids[i]) != 0) { fprintf(stderr, "DRAIN destroy task %zu failed\n", i); goto done; }
        ids[i] = 0;
    }
    if (flat_runtime_destroy(runtime) == 0) {
        runtime = NULL;
        fprintf(stderr, "DRAIN runtime destroy accepted before worker join\n"); goto done;
    }
    if (flat_runtime_workers_join(runtime) != 0) {
        fprintf(stderr, "DRAIN worker join failed\n"); goto done;
    }
    if (flat_runtime_destroy(runtime) != 0) { fprintf(stderr, "DRAIN runtime destroy failed\n"); goto done; }
    runtime = NULL;
    rc = 0;
done:
    if (runtime) {
        (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        for (i = 0; i < 1000 && flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++i)
            pause_one_ms();
        if (flat_runtime_lifecycle(runtime) == FLAT_RUNTIME_STOPPED)
            (void)flat_runtime_workers_join(runtime);
        for (i = 0; i < TASKS; ++i) if (ids[i]) (void)flat_runtime_task_destroy(runtime, ids[i]);
        (void)flat_runtime_destroy(runtime);
    }
    return rc;
}

static int run_cancel_created_case(const FlatBytecode *bc, const FlatBytecode *unsupported_bc) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    FlatTaskId ids[TASKS];
    FlatTaskId rejected_id = 0;
    size_t i;
    int rc = -1;
    memset(ids, 0, sizeof ids);
    if (!runtime) return -1;
    if (flat_runtime_workers_start(runtime, WORKERS, 1) != 0) {
        fprintf(stderr, "CANCEL worker startup failed\n"); goto done;
    }
    for (i = 0; i < TASKS; ++i)
        if (flat_runtime_task_create(runtime, bc, NULL, &ids[i]) != 0) { fprintf(stderr, "CANCEL create %zu failed\n", i); goto done; }
    if (flat_runtime_task_create(runtime, unsupported_bc, NULL, &rejected_id) == 0) {
        fprintf(stderr, "CANCEL accepted unsupported task after worker startup\n"); goto done;
    }
    if (flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL) != 0) { fprintf(stderr, "CANCEL shutdown request failed\n"); goto done; }
    for (i = 0; i < 10000 &&
         flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++i)
        pause_one_ms();
    if (flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE) {
        fprintf(stderr, "CANCEL wait failed status=%d life=%d\n", (int)flat_runtime_shutdown_status(runtime), (int)flat_runtime_lifecycle(runtime)); goto done;
    }
    for (i = 0; i < TASKS; ++i) {
        if (flat_runtime_task_state(runtime, ids[i]) != FLAT_TASK_CANCELLED) { fprintf(stderr, "CANCEL task %zu state=%d\n", i, (int)flat_runtime_task_state(runtime, ids[i])); goto done; }
    }
    for (i = 0; i < TASKS; ++i) {
        if (flat_runtime_task_destroy(runtime, ids[i]) != 0) { fprintf(stderr, "CANCEL destroy task %zu failed\n", i); goto done; }
        ids[i] = 0;
    }
    if (flat_runtime_destroy(runtime) == 0) {
        runtime = NULL;
        fprintf(stderr, "CANCEL runtime destroy accepted before worker join\n"); goto done;
    }
    if (flat_runtime_workers_join(runtime) != 0) {
        fprintf(stderr, "CANCEL worker join failed\n"); goto done;
    }
    if (flat_runtime_destroy(runtime) != 0) { fprintf(stderr, "CANCEL runtime destroy failed\n"); goto done; }
    runtime = NULL;
    rc = 0;
done:
    if (runtime) {
        (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        for (i = 0; i < 1000 && flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++i)
            pause_one_ms();
        if (flat_runtime_lifecycle(runtime) == FLAT_RUNTIME_STOPPED)
            (void)flat_runtime_workers_join(runtime);
        for (i = 0; i < TASKS; ++i) if (ids[i]) (void)flat_runtime_task_destroy(runtime, ids[i]);
        if (rejected_id) (void)flat_runtime_task_destroy(runtime, rejected_id);
        (void)flat_runtime_destroy(runtime);
    }
    return rc;
}

static int run_worker_startup_failure_case(void) {
    size_t fail_index;
    for (fail_index = 0; fail_index < WORKERS; ++fail_index) {
        FlatRuntime *runtime = flat_runtime_create(NULL);
        int start_rc;
        if (!runtime) return -1;
        flat_runtime_test_worker_fail_create_after((int)fail_index);
        start_rc = flat_runtime_workers_start(runtime, WORKERS, 1);
        if (start_rc == 0 || flat_runtime_destroy(runtime) != 0) {
            fprintf(stderr, "worker create failure at index %zu did not roll back cleanly\n", fail_index);
            if (start_rc == 0) {
                (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
                (void)flat_runtime_workers_join(runtime);
            }
            return -1;
        }
    }

    /* A failed join during partial-start rollback must retain the worker array
     * and the unjoined handle so workers_join() can retry safely. */
    {
        FlatRuntime *runtime = flat_runtime_create(NULL);
        if (!runtime) return -1;
        flat_runtime_test_worker_fail_create_after(2);
        flat_runtime_test_worker_fail_join_after(0);
        if (flat_runtime_workers_start(runtime, WORKERS, 1) == 0 ||
            flat_runtime_destroy(runtime) == 0 ||
            flat_runtime_workers_join(runtime) != 0 ||
            flat_runtime_destroy(runtime) != 0) {
            fprintf(stderr, "partial-start join retry/retained ownership failed\n");
            return -1;
        }
    }

    /* A transient join failure after a successful startup must not discard any
     * still-joinable handle; a second workers_join() completes cleanup. */
    {
        FlatRuntime *runtime = flat_runtime_create(NULL);
        if (!runtime || flat_runtime_workers_start(runtime, 3, 1) != 0 ||
            flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL) != 0) {
            if (runtime) (void)flat_runtime_destroy(runtime);
            return -1;
        }
        flat_runtime_test_worker_fail_join_after(1);
        if (flat_runtime_workers_join(runtime) == 0 ||
            flat_runtime_destroy(runtime) == 0 ||
            flat_runtime_workers_join(runtime) != 0 ||
            flat_runtime_destroy(runtime) != 0) {
            fprintf(stderr, "worker join retry after transient failure failed\n");
            return -1;
        }
    }
    return 0;
}

typedef struct {
    FlatRuntime *runtime;
    int result;
} RuntimeJoinProbe;

static void *runtime_join_main(void *arg) {
    RuntimeJoinProbe *probe = (RuntimeJoinProbe *)arg;
    probe->result = flat_runtime_workers_join(probe->runtime);
    return NULL;
}

static int wait_runtime_stopped(FlatRuntime *runtime) {
    size_t i;
    for (i = 0; i < 10000; ++i) {
        if (flat_runtime_shutdown_status(runtime) == FLAT_RUNTIME_SHUTDOWN_COMPLETE)
            return 0;
        pause_one_ms();
    }
    return -1;
}

static int run_multi_runtime_worker_mode_case(void) {
    FlatRuntime *first = NULL;
    FlatRuntime *second = NULL;
    int rc = -1;

    /* A failed second pool must release only its own process-wide worker-mode
     * reference; the first runtime continues to keep worker mode active. */
    first = flat_runtime_create(NULL);
    second = flat_runtime_create(NULL);
    if (!first || !second ||
        flat_runtime_workers_start(first, 2, 1) != 0 ||
        !flat_native_worker_mode_is_active()) goto done;

    flat_runtime_test_worker_fail_create_after(1);
    if (flat_runtime_workers_start(second, 3, 1) == 0 ||
        !flat_native_worker_mode_is_active() ||
        flat_runtime_destroy(second) != 0) {
        fprintf(stderr, "second runtime partial-start rollback corrupted worker-mode ownership\n");
        goto done;
    }
    second = NULL;

    if (flat_runtime_shutdown_request(first, FLAT_RUNTIME_SHUTDOWN_CANCEL) != 0 ||
        wait_runtime_stopped(first) != 0 ||
        flat_runtime_workers_join(first) != 0 ||
        flat_native_worker_mode_is_active()) {
        fprintf(stderr, "single runtime worker-mode reference was not released\n");
        goto done;
    }
    if (flat_runtime_destroy(first) != 0) goto done;
    first = NULL;

    /* Two live runtimes hold independent references. Joining one must not
     * disable the process-wide worker guard while the other is still active. */
    first = flat_runtime_create(NULL);
    second = flat_runtime_create(NULL);
    if (!first || !second ||
        flat_runtime_workers_start(first, 2, 1) != 0 ||
        flat_runtime_workers_start(second, 2, 1) != 0 ||
        !flat_native_worker_mode_is_active()) {
        fprintf(stderr, "overlapping runtime worker startup failed\n");
        goto done;
    }
    if (flat_runtime_shutdown_request(first, FLAT_RUNTIME_SHUTDOWN_CANCEL) != 0 ||
        wait_runtime_stopped(first) != 0 ||
        flat_runtime_workers_join(first) != 0 ||
        !flat_native_worker_mode_is_active()) {
        fprintf(stderr, "joining first runtime disabled worker mode for second runtime\n");
        goto done;
    }
    if (flat_runtime_destroy(first) != 0) goto done;
    first = NULL;

    if (flat_runtime_shutdown_request(second, FLAT_RUNTIME_SHUTDOWN_CANCEL) != 0 ||
        wait_runtime_stopped(second) != 0 ||
        flat_runtime_workers_join(second) != 0 ||
        flat_native_worker_mode_is_active()) {
        fprintf(stderr, "last runtime did not release process-wide worker mode\n");
        goto done;
    }
    if (flat_runtime_destroy(second) != 0) goto done;
    second = NULL;

    /* Concurrent joins on distinct runtimes race the process-wide reference
     * decrement. The atomic guard must remain balanced regardless of order. */
    first = flat_runtime_create(NULL);
    second = flat_runtime_create(NULL);
    if (!first || !second ||
        flat_runtime_workers_start(first, 2, 1) != 0 ||
        flat_runtime_workers_start(second, 2, 1) != 0 ||
        flat_runtime_shutdown_request(first, FLAT_RUNTIME_SHUTDOWN_CANCEL) != 0 ||
        flat_runtime_shutdown_request(second, FLAT_RUNTIME_SHUTDOWN_CANCEL) != 0 ||
        wait_runtime_stopped(first) != 0 || wait_runtime_stopped(second) != 0) {
        fprintf(stderr, "concurrent runtime join setup failed\n");
        goto done;
    }
    {
        RuntimeJoinProbe probes[2];
        pthread_t joiners[2];
        int created = 0;
        probes[0].runtime = first; probes[0].result = -99;
        probes[1].runtime = second; probes[1].result = -99;
        if (pthread_create(&joiners[0], NULL, runtime_join_main, &probes[0]) != 0)
            goto done;
        created = 1;
        if (pthread_create(&joiners[1], NULL, runtime_join_main, &probes[1]) != 0) {
            (void)pthread_join(joiners[0], NULL);
            goto done;
        }
        created = 2;
        if (created == 2) {
            (void)pthread_join(joiners[0], NULL);
            (void)pthread_join(joiners[1], NULL);
        }
        if (probes[0].result != 0 || probes[1].result != 0 ||
            flat_native_worker_mode_is_active()) {
            fprintf(stderr, "concurrent runtime joins left worker-mode references active\n");
            goto done;
        }
    }
    if (flat_runtime_destroy(first) != 0) goto done;
    first = NULL;
    if (flat_runtime_destroy(second) != 0) goto done;
    second = NULL;
    rc = 0;

done:
    if (first) {
        (void)flat_runtime_shutdown_request(first, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        if (wait_runtime_stopped(first) == 0) (void)flat_runtime_workers_join(first);
        (void)flat_runtime_destroy(first);
    }
    if (second) {
        (void)flat_runtime_shutdown_request(second, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        if (wait_runtime_stopped(second) == 0) (void)flat_runtime_workers_join(second);
        (void)flat_runtime_destroy(second);
    }
    return rc;
}

typedef struct {
    FlatRuntime *runtime;
    pthread_mutex_t *gate_mutex;
    pthread_cond_t *gate_cond;
    int *ready_count;
    int *released;
    int result;
} SameRuntimeJoinProbe;

static void *same_runtime_join_main(void *arg) {
    SameRuntimeJoinProbe *probe = (SameRuntimeJoinProbe *)arg;
    pthread_mutex_lock(probe->gate_mutex);
    ++*probe->ready_count;
    pthread_cond_broadcast(probe->gate_cond);
    while (!*probe->released)
        pthread_cond_wait(probe->gate_cond, probe->gate_mutex);
    pthread_mutex_unlock(probe->gate_mutex);
    probe->result = flat_runtime_workers_join(probe->runtime);
    return NULL;
}

static int run_same_runtime_concurrent_join_case(void) {
    enum { JOINERS = 4, WORKERS_FOR_JOIN = 16 };
    FlatRuntime *runtime = flat_runtime_create(NULL);
    pthread_mutex_t gate_mutex;
    pthread_cond_t gate_cond;
    pthread_t joiners[JOINERS];
    SameRuntimeJoinProbe probes[JOINERS];
    int ready_count = 0;
    int released = 0;
    int created = 0;
    int successes = 0;
    int failures = 0;
    int rc = -1;
    int i;

    if (!runtime || pthread_mutex_init(&gate_mutex, NULL) != 0) goto done;
    if (pthread_cond_init(&gate_cond, NULL) != 0) {
        pthread_mutex_destroy(&gate_mutex);
        goto done;
    }
    if (flat_runtime_workers_start(runtime, WORKERS_FOR_JOIN, 1) != 0 ||
        flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL) != 0 ||
        wait_runtime_stopped(runtime) != 0) {
        fprintf(stderr, "same-runtime concurrent join setup failed\n");
        goto release_and_cleanup;
    }

    /* Hold the first join after it claims ownership so the other callers
     * deterministically contend while workers_joining is set. */
    flat_runtime_test_worker_join_delay_ms(100);
    for (i = 0; i < JOINERS; ++i) {
        probes[i].runtime = runtime;
        probes[i].gate_mutex = &gate_mutex;
        probes[i].gate_cond = &gate_cond;
        probes[i].ready_count = &ready_count;
        probes[i].released = &released;
        probes[i].result = -99;
        if (pthread_create(&joiners[i], NULL, same_runtime_join_main, &probes[i]) != 0) {
            fprintf(stderr, "same-runtime joiner creation failed at %d\n", i);
            goto release_and_cleanup;
        }
        ++created;
    }

    pthread_mutex_lock(&gate_mutex);
    while (ready_count != created)
        pthread_cond_wait(&gate_cond, &gate_mutex);
    released = 1;
    pthread_cond_broadcast(&gate_cond);
    pthread_mutex_unlock(&gate_mutex);

    for (i = 0; i < created; ++i) pthread_join(joiners[i], NULL);
    created = 0;
    for (i = 0; i < JOINERS; ++i) {
        if (probes[i].result == 0) ++successes;
        else if (probes[i].result == -1) ++failures;
    }
    if (successes != 1 || failures != JOINERS - 1 ||
        flat_runtime_destroy(runtime) != 0) {
        fprintf(stderr,
                "same-runtime concurrent join ownership failed success=%d rejected=%d\n",
                successes, failures);
        goto release_and_cleanup;
    }
    runtime = NULL;
    rc = 0;

release_and_cleanup:
    if (created) {
        pthread_mutex_lock(&gate_mutex);
        released = 1;
        pthread_cond_broadcast(&gate_cond);
        pthread_mutex_unlock(&gate_mutex);
        for (i = 0; i < created; ++i) pthread_join(joiners[i], NULL);
    }
    if (runtime) {
        (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        if (wait_runtime_stopped(runtime) == 0) (void)flat_runtime_workers_join(runtime);
        (void)flat_runtime_destroy(runtime);
    }
    pthread_cond_destroy(&gate_cond);
    pthread_mutex_destroy(&gate_mutex);
    return rc;

done:
    if (runtime) (void)flat_runtime_destroy(runtime);
    return -1;
}

static int run_unsupported_native_case(const FlatBytecode *bc) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    FlatTaskId id = 0;
    int rc = -1;
    if (!runtime || flat_runtime_task_create(runtime, bc, NULL, &id) != 0 ||
        flat_runtime_task_start(runtime, id) != 0) goto done;
    /* Unsupported optional natives are rejected before the first worker starts. */
    if (flat_runtime_workers_start(runtime, 2, 1) == 0 ||
        flat_runtime_lifecycle(runtime) != FLAT_RUNTIME_RUNNING ||
        flat_runtime_task_state(runtime, id) != FLAT_TASK_RUNNABLE ||
        flat_runtime_task_cancel(runtime, id) != 0 ||
        flat_runtime_task_state(runtime, id) != FLAT_TASK_CANCELLED ||
        flat_runtime_task_destroy(runtime, id) != 0) goto done;
    id = 0;
    if (flat_runtime_destroy(runtime) != 0) goto done;
    runtime = NULL;
    rc = 0;
done:
    if (runtime) {
        if (id) {
            if (flat_runtime_task_state(runtime, id) == FLAT_TASK_RUNNABLE ||
                flat_runtime_task_state(runtime, id) == FLAT_TASK_CREATED)
                (void)flat_runtime_task_cancel(runtime, id);
            (void)flat_runtime_task_destroy(runtime, id);
        }
        (void)flat_runtime_destroy(runtime);
    }
    return rc;
}

int main(int argc, char **argv) {
    FlatBytecode bc, unsupported_bc;
    FlatNativeRuntimeContext native_context;
    char native_error[256];
    int rc;
    memset(&unsupported_bc, 0, sizeof unsupported_bc);
    memset(&native_context, 0, sizeof native_context);
    if (argc != 3 || load_bytecode(argv[1], &bc) != 0 ||
        load_bytecode(argv[2], &unsupported_bc) != 0) return 2;
    native_context.manifest = unsupported_bc.optional_natives;
    native_context.manifest_count = unsupported_bc.optional_native_count;
    {
        int prep_rc = flat_native_runtime_prepare(&native_context, FLAT_NATIVE_MATH_CALC,
                                                  native_error, sizeof native_error);
        int set_rc = flat_native_module_set_thread_capability("math", FLAT_NATIVE_THREAD_CAP_UNSUPPORTED);
        FlatNativeThreadCapability math_policy = flat_native_module_worker_policy("math");
        FlatNativeThreadCapability task_policy = flat_native_module_worker_policy("task");
        FlatNativeThreadCapability channel_policy = flat_native_module_worker_policy("channel");
        if (set_rc != 0 || prep_rc != 0 || math_policy != FLAT_NATIVE_THREAD_CAP_UNSUPPORTED ||
            task_policy != FLAT_NATIVE_THREAD_CAP_SERIALIZED ||
            channel_policy != FLAT_NATIVE_THREAD_CAP_SERIALIZED) {
            fprintf(stderr, "native capability test setup failed set=%d prep=%d math=%d task=%d channel=%d: %s\n",
                    set_rc, prep_rc, (int)math_policy, (int)task_policy, (int)channel_policy, native_error);
            rc = -1;
        } else {
        rc = run_unsupported_native_case(&unsupported_bc);
        if (rc != 0) fprintf(stderr, "worker pool unsupported-native preflight case failed\n");
        if (rc == 0) {
            rc = run_worker_startup_failure_case();
            if (rc != 0) fprintf(stderr, "worker startup failure-injection case failed\n");
        }
        if (rc == 0) {
            rc = run_multi_runtime_worker_mode_case();
            if (rc != 0) fprintf(stderr, "multi-runtime worker-mode ownership case failed\n");
        }
        if (rc == 0) {
            rc = run_same_runtime_concurrent_join_case();
            if (rc != 0) fprintf(stderr, "same-runtime concurrent join case failed\n");
        }
        if (rc == 0) {
            rc = run_drain_case(&bc);
            if (rc != 0) fprintf(stderr, "worker pool DRAIN case failed\n");
        }
        if (rc == 0) {
            rc = run_cancel_created_case(&bc, &unsupported_bc);
            if (rc != 0) fprintf(stderr, "worker pool CANCEL case failed\n");
        }
        }
    }
    flat_fb_free(&bc);
    flat_fb_free(&unsupported_bc);
    if (rc != 0) {
        fprintf(stderr, "runtime worker pool test failed: %d\n", rc);
        return 1;
    }
    puts("runtime-worker-pool-ok workers=4 tasks=96 drain=joined cancel=joined unsupported-native=preflight-rejected multi-runtime-worker-mode=balanced");
    return 0;
}
