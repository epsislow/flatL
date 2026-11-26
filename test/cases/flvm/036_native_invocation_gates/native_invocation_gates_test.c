#include "flat_native_capability.h"
#include "flat_native_plugin.h"
#include "flat_dom_runtime.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define THREADS 8
#define LOOPS 80

typedef struct {
    atomic_int *active;
    atomic_int *maximum;
    atomic_int *failed;
} GateStress;

static void pause_ns(long ns) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = ns;
    nanosleep(&ts, NULL);
}

static void update_max(atomic_int *maximum, int value) {
    int old = atomic_load_explicit(maximum, memory_order_relaxed);
    while (value > old && !atomic_compare_exchange_weak_explicit(
               maximum, &old, value, memory_order_relaxed, memory_order_relaxed)) {
    }
}

static void *serialized_worker(void *arg) {
    GateStress *stress = (GateStress *)arg;
    int i;
    for (i = 0; i < LOOPS; ++i) {
        int active;
        if (flat_native_module_gate_enter("dom") != 0) {
            atomic_store(stress->failed, 1);
            return NULL;
        }
        active = atomic_fetch_add_explicit(stress->active, 1, memory_order_relaxed) + 1;
        update_max(stress->maximum, active);
        if (active != 1) atomic_store(stress->failed, 1);
        pause_ns(100000L);
        atomic_fetch_sub_explicit(stress->active, 1, memory_order_relaxed);
        if (flat_native_module_gate_leave("dom") != 0) {
            atomic_store(stress->failed, 1);
            return NULL;
        }
    }
    return NULL;
}

typedef struct {
    atomic_int *started;
    atomic_int *done;
} GateProbe;

static void *independent_gate_worker(void *arg) {
    GateProbe *probe = (GateProbe *)arg;
    atomic_store(probe->started, 1);
    if (flat_native_module_gate_enter("dom") != 0) return NULL;
    atomic_store(probe->done, 1);
    (void)flat_native_module_gate_leave("dom");
    return NULL;
}

typedef struct {
    struct FlatDomRuntime *runtime;
    atomic_int started;
    atomic_int done;
} DomDestroyProbe;

typedef struct {
    FlatNativeCtx ctx;
    int failed;
} DispatchProbe;

static void *dom_destroy_worker(void *arg) {
    DomDestroyProbe *probe = (DomDestroyProbe *)arg;
    atomic_store(&probe->started, 1);
    flat_dom_runtime_destroy(probe->runtime);
    atomic_store(&probe->done, 1);
    return NULL;
}

static void *serialized_dispatch_worker(void *arg) {
    DispatchProbe *probe = (DispatchProbe *)arg;
    if (flat_native_invoke(FLAT_NATIVE_LIST_EMPTY, &probe->ctx) != 0) {
        probe->failed = 1;
    }
    return NULL;
}

int main(int argc, char **argv) {
    pthread_t threads[THREADS];
    GateStress stress;
    atomic_int active = 0, maximum = 0, failed = 0;
    int i;

    if (argc != 2) return 12;
    if (setenv("FLAT_NATIVE_PATH", "", 1) != 0) return 13;

    stress.active = &active;
    stress.maximum = &maximum;
    stress.failed = &failed;
    for (i = 0; i < THREADS; ++i) {
        if (pthread_create(&threads[i], NULL, serialized_worker, &stress) != 0) return 2;
    }
    for (i = 0; i < THREADS; ++i) pthread_join(threads[i], NULL);
    if (atomic_load(&failed) || atomic_load(&maximum) != 1) {
        fprintf(stderr, "serialized gate overlap: max=%d failed=%d\n",
                atomic_load(&maximum), atomic_load(&failed));
        return 3;
    }

    /* Different module gates must not block one another. */
    {
        pthread_t thread;
        atomic_int started = 0, done = 0;
        GateProbe probe = { &started, &done };
        if (flat_native_module_gate_enter("io") != 0) return 4;
        if (pthread_create(&thread, NULL, independent_gate_worker, &probe) != 0) return 5;
        for (i = 0; i < 1000 && !atomic_load(&started); ++i) pause_ns(1000000L);
        for (i = 0; i < 1000 && !atomic_load(&done); ++i) pause_ns(1000000L);
        (void)flat_native_module_gate_leave("io");
        pthread_join(thread, NULL);
        if (!atomic_load(&done)) {
            fprintf(stderr, "independent module gate was blocked\n");
            return 6;
        }
    }

    /* VM teardown must acquire the same DOM gate as DOM native invocations. */
    {
        pthread_t thread;
        DomDestroyProbe probe;
        probe.runtime = flat_dom_runtime_create();
        atomic_init(&probe.started, 0);
        atomic_init(&probe.done, 0);
        if (!probe.runtime) return 7;
        if (flat_native_module_gate_enter("dom") != 0) return 8;
        if (pthread_create(&thread, NULL, dom_destroy_worker, &probe) != 0) return 9;
        for (i = 0; i < 1000 && !atomic_load(&probe.started); ++i) pause_ns(1000000L);
        pause_ns(20000000L);
        if (atomic_load(&probe.done)) {
            (void)flat_native_module_gate_leave("dom");
            pthread_join(thread, NULL);
            fprintf(stderr, "DOM teardown bypassed invocation gate\n");
            return 10;
        }
        (void)flat_native_module_gate_leave("dom");
        pthread_join(thread, NULL);
        if (!atomic_load(&probe.done)) return 11;
    }

    /* First-use plugin loading must publish SERIALIZED before dispatcher gate selection. */
    {
        FlatOptionalNativeDecl decl;
        FlatNativeRuntimeContext runtime;
        DispatchProbe probes[THREADS];
        pthread_t dispatch_threads[THREADS];
        if (snprintf(decl.module, sizeof(decl.module), "%s", "list") >=
            (int)sizeof(decl.module)) return 14;
        decl.alias[0] = '\0';
        runtime.manifest = &decl;
        runtime.manifest_count = 1;
        flat_native_runtime_set_search_dir(argv[1]);
        for (i = 0; i < THREADS; ++i) {
            memset(&probes[i], 0, sizeof(probes[i]));
            probes[i].ctx.native_runtime = &runtime;
            if (pthread_create(&dispatch_threads[i], NULL, serialized_dispatch_worker,
                               &probes[i]) != 0) return 15;
        }
        for (i = 0; i < THREADS; ++i) {
            pthread_join(dispatch_threads[i], NULL);
            if (probes[i].failed) {
                fprintf(stderr, "serialized plugin dispatch failed at first use\n");
                return 16;
            }
        }
    }

    puts("native-invocation-gates-ok serialized=exclusive independent=parallel dom-teardown=guarded first-use-dispatch=serialized");
    return 0;
}
