#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"
#include "flat_native_capability.h"

#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TASK_COUNT 48
#define WORKER_COUNT 4

typedef struct DestroyProbe {
    FlatRuntime *runtime;
    FlatTaskId task;
    atomic_int started;
    int rc;
} DestroyProbe;

static void pause_one_ms(void) {
    struct timespec ts = {0, 1000000L};
    (void)nanosleep(&ts, NULL);
}

static int load_bytecode(const char *path, FlatBytecode *bc) {
    memset(bc, 0, sizeof(*bc));
    return flat_fb_load_file(path, bc);
}

static int wait_complete(FlatRuntime *runtime) {
    unsigned i;
    for (i = 0; i < 30000; ++i) {
        if (flat_runtime_shutdown_status(runtime) == FLAT_RUNTIME_SHUTDOWN_COMPLETE) return 0;
        pause_one_ms();
    }
    return -1;
}

static void *destroy_created_task(void *opaque) {
    DestroyProbe *probe = (DestroyProbe *)opaque;
    atomic_store_explicit(&probe->started, 1, memory_order_release);
    probe->rc = flat_runtime_task_destroy(probe->runtime, probe->task);
    return NULL;
}

/* A CREATED task may be destroyed while worker mode is active. Its VM teardown
 * can block on the serialized DOM gate, but once cleanup_owned is published a
 * concurrent cancel request must be rejected rather than reported as accepted. */
static int run_destroy_cancel_race(const FlatBytecode *bc) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    FlatTaskId running = 0, destroying = 0;
    DestroyProbe probe;
    pthread_t destroy_thread;
    unsigned i;
    int gate_held = 0, workers_started = 0, thread_started = 0;
    int cancel_rc = -99;
    int rc = -1;
    memset(&probe, 0, sizeof probe);
    atomic_init(&probe.started, 0);
    if (!runtime) return -301;
    if (flat_runtime_task_create(runtime, bc, NULL, &running) != 0 ||
        flat_runtime_task_start(runtime, running) != 0 ||
        flat_runtime_task_create(runtime, bc, NULL, &destroying) != 0) {
        fprintf(stderr, "DOM destroy/cancel race task setup failed\n");
        rc = -302; goto done;
    }
    if (flat_native_module_gate_enter("dom") != 0) {
        fprintf(stderr, "DOM destroy/cancel race gate enter failed\n");
        rc = -303; goto done;
    }
    gate_held = 1;
    if (flat_runtime_workers_start(runtime, 2, 1) != 0) {
        fprintf(stderr, "DOM destroy/cancel race workers_start failed\n");
        rc = -304; goto done;
    }
    workers_started = 1;
    for (i = 0; i < 10000 &&
         flat_runtime_task_state(runtime, running) != FLAT_TASK_RUNNING; ++i)
        pause_one_ms();
    if (flat_runtime_task_state(runtime, running) != FLAT_TASK_RUNNING) {
        fprintf(stderr, "DOM destroy/cancel race worker did not enter DOM\n");
        rc = -305; goto done;
    }

    probe.runtime = runtime;
    probe.task = destroying;
    if (pthread_create(&destroy_thread, NULL, destroy_created_task, &probe) != 0) {
        fprintf(stderr, "DOM destroy/cancel race destroy thread creation failed\n");
        rc = -306; goto done;
    }
    thread_started = 1;
    for (i = 0; i < 30000 &&
         !atomic_load_explicit(&probe.started, memory_order_acquire); ++i)
        pause_one_ms();
    if (!atomic_load_explicit(&probe.started, memory_order_acquire)) {
        fprintf(stderr, "DOM destroy/cancel race destroy thread did not start\n");
        rc = -307; goto done;
    }
    /* Give the destroyer time to publish cleanup_owned and block at DOM teardown. */
    for (i = 0; i < 30; ++i) pause_one_ms();
    cancel_rc = flat_runtime_task_cancel_request(runtime, destroying);
    (void)flat_native_module_gate_leave("dom");
    gate_held = 0;

    if (flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_DRAIN) != 0 ||
        wait_complete(runtime) != 0) {
        fprintf(stderr, "DOM destroy/cancel race DRAIN failed\n");
        rc = -308; goto done;
    }
    if (flat_runtime_workers_join(runtime) != 0) {
        fprintf(stderr, "DOM destroy/cancel race workers_join failed\n");
        rc = -309; goto done;
    }
    workers_started = 0;
    pthread_join(destroy_thread, NULL);
    thread_started = 0;
    if (cancel_rc != -1 || probe.rc != 0 ||
        flat_runtime_task_state(runtime, destroying) != (FlatTaskState)0 ||
        flat_runtime_task_state(runtime, running) != FLAT_TASK_COMPLETED) {
        fprintf(stderr, "DOM destroy/cancel race mismatch cancel=%d destroy=%d\n",
                cancel_rc, probe.rc);
        rc = -310; goto done;
    }
    if (flat_runtime_task_destroy(runtime, running) != 0) {
        fprintf(stderr, "DOM destroy/cancel race running task destroy failed\n");
        rc = -311; goto done;
    }
    running = 0;
    if (flat_runtime_destroy(runtime) != 0) {
        fprintf(stderr, "DOM destroy/cancel race runtime destroy failed\n");
        rc = -312; goto done;
    }
    runtime = NULL;
    rc = 0;
done:
    if (gate_held) (void)flat_native_module_gate_leave("dom");
    if (thread_started) pthread_join(destroy_thread, NULL);
    if (runtime) {
        (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        if (workers_started) {
            (void)wait_complete(runtime);
            if (flat_runtime_lifecycle(runtime) == FLAT_RUNTIME_STOPPED)
                (void)flat_runtime_workers_join(runtime);
        } else {
            unsigned turns;
            for (turns = 0; turns < 30000 &&
                 flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++turns)
                (void)flat_runtime_schedule(runtime, 1, 0, NULL);
        }
        if (running) (void)flat_runtime_task_destroy(runtime, running);
        if (destroying && !thread_started) (void)flat_runtime_task_destroy(runtime, destroying);
        (void)flat_runtime_destroy(runtime);
    }
    return rc;
}

static int run_parallel_documents(const FlatBytecode *bc) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    FlatTaskId tasks[TASK_COUNT];
    unsigned i;
    int workers_started = 0;
    int rc = -1;
    memset(tasks, 0, sizeof tasks);
    if (!runtime) return -101;
    for (i = 0; i < TASK_COUNT; ++i) {
        if (flat_runtime_task_create(runtime, bc, NULL, &tasks[i]) != 0 ||
            flat_runtime_task_start(runtime, tasks[i]) != 0) { fprintf(stderr, "DOM parallel setup failed at %u\n", i); rc = -102; goto done; }
    }
    if (flat_runtime_workers_start(runtime, WORKER_COUNT, 8) != 0) { fprintf(stderr, "DOM parallel workers_start failed\n"); rc = -103; goto done; }
    workers_started = 1;
    if (flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_DRAIN) != 0) { fprintf(stderr, "DOM parallel DRAIN failed\n"); rc = -104; goto done; }
    if (wait_complete(runtime) != 0) { fprintf(stderr, "DOM parallel shutdown timeout status=%d life=%d\n", (int)flat_runtime_shutdown_status(runtime), (int)flat_runtime_lifecycle(runtime)); rc = -105; goto done; }
    for (i = 0; i < TASK_COUNT; ++i)
        if (flat_runtime_task_state(runtime, tasks[i]) != FLAT_TASK_COMPLETED) { fprintf(stderr, "DOM parallel task %u state=%d\n", i, (int)flat_runtime_task_state(runtime, tasks[i])); rc = -106; goto done; }
    if (flat_runtime_workers_join(runtime) != 0) { fprintf(stderr, "DOM parallel workers_join failed\n"); rc = -107; goto done; }
    for (i = 0; i < TASK_COUNT; ++i) {
        if (flat_runtime_task_destroy(runtime, tasks[i]) != 0) { fprintf(stderr, "DOM parallel task destroy %u failed\n", i); rc = -108; goto done; }
        tasks[i] = 0;
    }
    if (flat_runtime_destroy(runtime) != 0) { fprintf(stderr, "DOM parallel runtime destroy failed\n"); rc = -109; goto done; }
    runtime = NULL;
    rc = 0;
done:
    if (runtime) {
        (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        if (workers_started) {
            (void)wait_complete(runtime);
            if (flat_runtime_lifecycle(runtime) == FLAT_RUNTIME_STOPPED)
                (void)flat_runtime_workers_join(runtime);
        } else {
            unsigned turns;
            for (turns = 0; turns < 30000 &&
                 flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++turns)
                (void)flat_runtime_schedule(runtime, 1, 0, NULL);
        }
        for (i = 0; i < TASK_COUNT; ++i)
            if (tasks[i]) (void)flat_runtime_task_destroy(runtime, tasks[i]);
        (void)flat_runtime_destroy(runtime);
    }
    return rc;
}

static int run_cancel_during_dom_gate(const FlatBytecode *bc) {
    FlatRuntime *runtime = flat_runtime_create(NULL);
    FlatTaskId task = 0;
    unsigned i;
    int gate_held = 0;
    int workers_started = 0;
    int rc = -1;
    if (!runtime) return -201;
    if (flat_runtime_task_create(runtime, bc, NULL, &task) != 0 ||
        flat_runtime_task_start(runtime, task) != 0) { fprintf(stderr, "DOM cancel task setup failed\n"); rc = -202; goto done; }
    /* Hold the same process-wide gate used by DOM dispatch and VM teardown. */
    if (flat_native_module_gate_enter("dom") != 0) { fprintf(stderr, "DOM cancel gate enter failed\n"); rc = -203; goto done; }
    gate_held = 1;
    if (flat_runtime_workers_start(runtime, 1, 1) != 0) { fprintf(stderr, "DOM cancel workers_start failed\n"); rc = -204; goto done; }
    workers_started = 1;
    for (i = 0; i < 10000 && flat_runtime_task_state(runtime, task) != FLAT_TASK_RUNNING; ++i)
        pause_one_ms();
    if (flat_runtime_task_state(runtime, task) != FLAT_TASK_RUNNING) { fprintf(stderr, "DOM cancel task did not reach RUNNING, state=%d\n", (int)flat_runtime_task_state(runtime, task)); rc = -205; goto done; }
    /* The worker is now blocked entering dom.parseHtml. Cancellation must remain
     * request-only; after gate release the native call returns and VM teardown
     * must acquire the same gate to destroy its DOM runtime safely. */
    if (flat_runtime_task_cancel_request(runtime, task) != 0) { fprintf(stderr, "DOM cancel request failed\n"); rc = -206; goto done; }
    (void)flat_native_module_gate_leave("dom");
    gate_held = 0;
    if (flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL) != 0) { fprintf(stderr, "DOM cancel shutdown request failed\n"); rc = -207; goto done; }
    if (wait_complete(runtime) != 0) { fprintf(stderr, "DOM cancel shutdown timeout status=%d life=%d task=%d\n", (int)flat_runtime_shutdown_status(runtime), (int)flat_runtime_lifecycle(runtime), (int)flat_runtime_task_state(runtime, task)); rc = -208; goto done; }
    if (flat_runtime_task_state(runtime, task) != FLAT_TASK_CANCELLED) { fprintf(stderr, "DOM cancel final task state=%d\n", (int)flat_runtime_task_state(runtime, task)); rc = -209; goto done; }
    if (flat_runtime_workers_join(runtime) != 0) { fprintf(stderr, "DOM cancel workers_join failed\n"); rc = -210; goto done; }
    if (flat_runtime_task_destroy(runtime, task) != 0) { fprintf(stderr, "DOM cancel task destroy failed\n"); rc = -211; goto done; }
    task = 0;
    if (flat_runtime_destroy(runtime) != 0) { fprintf(stderr, "DOM cancel runtime destroy failed\n"); rc = -212; goto done; }
    runtime = NULL;
    rc = 0;
done:
    if (gate_held) (void)flat_native_module_gate_leave("dom");
    if (runtime) {
        (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        if (workers_started) {
            (void)wait_complete(runtime);
            if (flat_runtime_lifecycle(runtime) == FLAT_RUNTIME_STOPPED)
                (void)flat_runtime_workers_join(runtime);
        } else {
            unsigned turns;
            for (turns = 0; turns < 30000 &&
                 flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++turns)
                (void)flat_runtime_schedule(runtime, 1, 0, NULL);
        }
        if (task) (void)flat_runtime_task_destroy(runtime, task);
        (void)flat_runtime_destroy(runtime);
    }
    return rc;
}

int main(int argc, char **argv) {
    FlatBytecode bc;
    int rc;
    memset(&bc, 0, sizeof bc);
    if (argc != 2 || load_bytecode(argv[1], &bc) != 0) return 2;
    if (flat_native_module_worker_policy("dom") != FLAT_NATIVE_THREAD_CAP_SERIALIZED) {
        flat_fb_free(&bc);
        fprintf(stderr, "DOM worker policy is not serialized\n");
        return 3;
    }
    rc = run_parallel_documents(&bc);
    if (rc == 0) rc = run_cancel_during_dom_gate(&bc);
    if (rc == 0) rc = run_destroy_cancel_race(&bc);
    flat_fb_free(&bc);
    if (rc != 0) {
        fprintf(stderr, "worker-dom-lifecycle-failed: %d\n", rc);
        return 1;
    }
    puts("worker-dom-ok workers=4 documents=48 cancel=teardown-gated destroy-cancel-race=guarded");
    return 0;
}
