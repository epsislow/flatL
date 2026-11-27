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
    uint16_t native_id;
    int expected_result;
    atomic_int *ready;
    atomic_int *go;
    int failed;
} DispatchProbe;

static void *dom_destroy_worker(void *arg) {
    DomDestroyProbe *probe = (DomDestroyProbe *)arg;
    atomic_store(&probe->started, 1);
    flat_dom_runtime_destroy(probe->runtime);
    atomic_store(&probe->done, 1);
    return NULL;
}

static void *first_use_dispatch_worker(void *arg) {
    DispatchProbe *probe = (DispatchProbe *)arg;
    int result;
    atomic_fetch_add_explicit(probe->ready, 1, memory_order_release);
    while (!atomic_load_explicit(probe->go, memory_order_acquire)) {
    }
    result = flat_native_invoke(probe->native_id, &probe->ctx);
    if (result != probe->expected_result) probe->failed = 1;
    return NULL;
}

typedef struct {
    FlatNativeRuntimeContext *runtime;
    atomic_int ready;
    atomic_int go;
    int prepare_result;
    int freeze_result;
    FlatNativeThreadCapability policy;
    char error[256];
} PluginFreezeRace;

static void *plugin_prepare_worker(void *arg) {
    PluginFreezeRace *race = (PluginFreezeRace *)arg;
    atomic_fetch_add_explicit(&race->ready, 1, memory_order_release);
    while (!atomic_load_explicit(&race->go, memory_order_acquire)) {
    }
    race->prepare_result = flat_native_runtime_worker_policy(
        race->runtime, FLAT_NATIVE_CSV_READER, &race->policy,
        race->error, sizeof(race->error));
    return NULL;
}

static void *plugin_freeze_worker(void *arg) {
    PluginFreezeRace *race = (PluginFreezeRace *)arg;
    atomic_fetch_add_explicit(&race->ready, 1, memory_order_release);
    while (!atomic_load_explicit(&race->go, memory_order_acquire)) {
    }
    race->freeze_result = flat_native_runtime_freeze_worker_capabilities();
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

    /* Concurrent first use of different modules must stage and publish each
       plugin's handlers and capability metadata as one serialized transaction.
       The list plugin is SERIALIZED; the object plugin is THREAD_SAFE. */
    {
        FlatOptionalNativeDecl decls[2];
        FlatNativeRuntimeContext runtimes[2];
        DispatchProbe probes[THREADS * 2];
        pthread_t dispatch_threads[THREADS * 2];
        atomic_int ready = 0;
        atomic_int go = 0;
        int thread_count = 0;
        if (snprintf(decls[0].module, sizeof(decls[0].module), "%s", "list") >=
                (int)sizeof(decls[0].module) ||
            snprintf(decls[1].module, sizeof(decls[1].module), "%s", "obj") >=
                (int)sizeof(decls[1].module)) return 14;
        decls[0].alias[0] = '\0';
        decls[1].alias[0] = '\0';
        for (i = 0; i < 2; ++i) {
            runtimes[i].manifest = &decls[i];
            runtimes[i].manifest_count = 1;
        }
        flat_native_runtime_set_search_dir(argv[1]);
        for (i = 0; i < THREADS * 2; ++i) {
            int module_index = i < THREADS ? 0 : 1;
            memset(&probes[i], 0, sizeof(probes[i]));
            probes[i].native_id = module_index == 0 ? FLAT_NATIVE_LIST_EMPTY
                                                     : FLAT_NATIVE_OBJ_EMPTY;
            probes[i].expected_result = module_index == 0 ? 0 : 42;
            probes[i].ctx.native_runtime = &runtimes[module_index];
            probes[i].ready = &ready;
            probes[i].go = &go;
            if (pthread_create(&dispatch_threads[i], NULL, first_use_dispatch_worker,
                               &probes[i]) != 0) break;
            thread_count++;
        }
        if (thread_count != THREADS * 2) {
            atomic_store_explicit(&go, 1, memory_order_release);
            for (i = 0; i < thread_count; ++i) pthread_join(dispatch_threads[i], NULL);
            return 15;
        }
        while (atomic_load_explicit(&ready, memory_order_acquire) != THREADS * 2) {
        }
        atomic_store_explicit(&go, 1, memory_order_release);
        for (i = 0; i < THREADS * 2; ++i) {
            pthread_join(dispatch_threads[i], NULL);
            if (probes[i].failed) {
                fprintf(stderr, "concurrent first-use plugin dispatch failed (index=%d)\n", i);
                return 16;
            }
        }
        if (flat_native_module_worker_policy("list") != FLAT_NATIVE_THREAD_CAP_SERIALIZED ||
            flat_native_module_worker_policy("obj") != FLAT_NATIVE_THREAD_CAP_THREAD_SAFE) {
            fprintf(stderr, "concurrent plugin capability publication mismatch\n");
            return 17;
        }
    }

    /* Loading a third plugin races worker-capability freeze. The only valid
       outcomes are a complete load+policy publication before freeze, or a
       clean rejection because freeze won the loader lock. */
    {
        FlatOptionalNativeDecl decl;
        FlatNativeRuntimeContext runtime;
        PluginFreezeRace race;
        pthread_t loader_thread, freeze_thread;
        memset(&race, 0, sizeof(race));
        if (snprintf(decl.module, sizeof(decl.module), "%s", "csv") >=
            (int)sizeof(decl.module)) return 18;
        decl.alias[0] = '\0';
        runtime.manifest = &decl;
        runtime.manifest_count = 1;
        race.runtime = &runtime;
        atomic_init(&race.ready, 0);
        atomic_init(&race.go, 0);
        race.prepare_result = -3;
        race.freeze_result = -3;
        race.policy = FLAT_NATIVE_THREAD_CAP_UNKNOWN;
        if (pthread_create(&loader_thread, NULL, plugin_prepare_worker, &race) != 0) return 19;
        if (pthread_create(&freeze_thread, NULL, plugin_freeze_worker, &race) != 0) {
            atomic_store_explicit(&race.go, 1, memory_order_release);
            pthread_join(loader_thread, NULL);
            return 20;
        }
        while (atomic_load_explicit(&race.ready, memory_order_acquire) != 2) {
        }
        atomic_store_explicit(&race.go, 1, memory_order_release);
        pthread_join(loader_thread, NULL);
        pthread_join(freeze_thread, NULL);
        if (race.freeze_result != 0 || !flat_native_module_capabilities_is_frozen()) return 21;
        if (race.prepare_result == 0) {
            FlatNativeCtx ctx;
            memset(&ctx, 0, sizeof(ctx));
            ctx.native_runtime = &runtime;
            if (race.policy != FLAT_NATIVE_THREAD_CAP_THREAD_SAFE ||
                flat_native_invoke(FLAT_NATIVE_CSV_READER, &ctx) != 43) {
                fprintf(stderr, "plugin load completed without consistent policy/handler publication\n");
                return 22;
            }
        } else if (race.prepare_result == -1) {
            if (!strstr(race.error, "not loaded before worker capability freeze")) {
                fprintf(stderr, "plugin/freeze race failed for an unexpected reason: %s\n",
                        race.error);
                return 23;
            }
            if (flat_native_runtime_prepare(&runtime, FLAT_NATIVE_CSV_READER,
                                            race.error, sizeof(race.error)) == 0) {
                fprintf(stderr, "plugin was partially loaded after freeze won\n");
                return 24;
            }
        } else {
            fprintf(stderr, "plugin/freeze race returned invalid status %d\n",
                    race.prepare_result);
            return 25;
        }
    }

    puts("native-invocation-gates-ok serialized=exclusive independent=parallel dom-teardown=guarded first-use-multi-module=ok load-freeze-race=atomic");
    return 0;
}
