#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"
#include "flat_native_capability.h"
#include "flat_native_plugin.h"
#include "flat_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TASKS 96
#define WORKERS 4

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
    puts("runtime-worker-pool-ok workers=4 tasks=96 drain=joined cancel=joined unsupported-native=preflight-rejected");
    return 0;
}
