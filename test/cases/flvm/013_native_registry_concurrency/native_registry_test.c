#include "flat_native_plugin.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define THREAD_COUNT 8
#define ITERATIONS 12

typedef struct {
    const char *search_dir;
    const FlatNativeRuntimeContext *runtime;
    atomic_int *failures;
} WorkerArgs;

static void *worker(void *opaque) {
    WorkerArgs *args = (WorkerArgs *)opaque;
    int i;
    for (i = 0; i < ITERATIONS; i++) {
        char err[256];
        flat_native_runtime_set_search_dir(args->search_dir);
        flat_native_runtime_init();
        if (flat_native_runtime_prepare(args->runtime, FLAT_NATIVE_MATH_POW,
                                        err, sizeof err) != 0) {
            fprintf(stderr, "prepare failed: %s\n", err);
            atomic_fetch_add(args->failures, 1);
            break;
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    pthread_t threads[THREAD_COUNT];
    WorkerArgs args;
    FlatOptionalNativeDecl decl;
    FlatOptionalNativeDecl list_decl;
    FlatNativeRuntimeContext runtime;
    FlatNativeRuntimeContext list_runtime;
    FlatNativeRuntimeContext denied = {0};
    atomic_int failures = 0;
    char err[256];
    int i;

    if (argc != 4) {
        fprintf(stderr, "usage: %s plugin-search-dir bad-plugin-dir sandbox-dir\n", argv[0]);
        return 2;
    }

    /* Failed plugin initialization must not publish any staged function pointer. */
    if (setenv("FLAT_NATIVE_PATH", "", 1) != 0 || chdir(argv[3]) != 0) {
        fprintf(stderr, "unable to prepare isolated plugin test directory\n");
        return 3;
    }
    memset(&list_decl, 0, sizeof list_decl);
    snprintf(list_decl.module, sizeof list_decl.module, "%s", FLAT_NATIVE_MODULE_LIST);
    list_runtime.manifest = &list_decl;
    list_runtime.manifest_count = 1;
    flat_native_runtime_set_search_dir(argv[2]);
    for (i = 0; i < 2; i++) {
        if (flat_native_runtime_prepare(&list_runtime, FLAT_NATIVE_LIST_EMPTY,
                                        err, sizeof err) == 0) {
            fprintf(stderr, "failed plugin initializer published a handler\n");
            return 4;
        }
    }

    memset(&decl, 0, sizeof decl);
    snprintf(decl.module, sizeof decl.module, "%s", FLAT_NATIVE_MODULE_MATH);
    runtime.manifest = &decl;
    runtime.manifest_count = 1;

    args.search_dir = argv[1];
    args.runtime = &runtime;
    args.failures = &failures;

    for (i = 0; i < THREAD_COUNT; i++) {
        if (pthread_create(&threads[i], NULL, worker, &args) != 0) {
            fprintf(stderr, "pthread_create failed\n");
            return 5;
        }
    }
    for (i = 0; i < THREAD_COUNT; i++) {
        pthread_join(threads[i], NULL);
    }
    if (atomic_load(&failures) != 0) {
        return 6;
    }

    /* Loading a process-wide handler must not authorize a different VM. */
    if (flat_native_runtime_prepare(&denied, FLAT_NATIVE_MATH_POW,
                                   err, sizeof err) == 0) {
        fprintf(stderr, "empty manifest unexpectedly authorized math\n");
        return 7;
    }
    if (strstr(err, "not declared") == NULL) {
        fprintf(stderr, "unexpected authorization error: %s\n", err);
        return 8;
    }

    puts("native-registry-concurrency-ok");
    return 0;
}
