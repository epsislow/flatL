#include "flat_native_capability.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define THREAD_COUNT 8
#define QUERY_ROUNDS 50000

static atomic_int g_failures = ATOMIC_VAR_INIT(0);

static void check_capability(const char *module, FlatNativeThreadCapability expected) {
    FlatNativeThreadCapability actual = flat_native_module_worker_policy(module);
    if (actual != expected) {
        atomic_fetch_add_explicit(&g_failures, 1, memory_order_relaxed);
    }
}

static void *query_worker(void *arg) {
    int i;
    (void)arg;
    for (i = 0; i < QUERY_ROUNDS; ++i) {
        check_capability("math", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE);
        check_capability("dom", FLAT_NATIVE_THREAD_CAP_SERIALIZED);
        check_capability("task", FLAT_NATIVE_THREAD_CAP_SERIALIZED);
        check_capability("csv", FLAT_NATIVE_THREAD_CAP_SERIALIZED);
    }
    return NULL;
}

int main(void) {
    pthread_t threads[THREAD_COUNT];
    int i;

    if (flat_native_module_thread_capability("dom") != FLAT_NATIVE_THREAD_CAP_PENDING) return 1;
    if (flat_native_module_worker_policy("dom") != FLAT_NATIVE_THREAD_CAP_SERIALIZED) return 2;
    if (flat_native_module_worker_policy("task") != FLAT_NATIVE_THREAD_CAP_SERIALIZED) return 3;
    if (flat_native_module_worker_policy("channel") != FLAT_NATIVE_THREAD_CAP_SERIALIZED) return 4;
    if (flat_native_module_worker_policy("unknown-plugin") != FLAT_NATIVE_THREAD_CAP_UNSUPPORTED)
        return 5;

    if (flat_native_module_set_thread_capability("csv", FLAT_NATIVE_THREAD_CAP_SERIALIZED) != 0)
        return 6;
    if (flat_native_module_worker_policy("csv") != FLAT_NATIVE_THREAD_CAP_SERIALIZED) return 7;
    if (flat_native_module_set_thread_capability("math", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE) != 0)
        return 8;

    if (flat_native_module_capabilities_freeze() != 0 ||
        !flat_native_module_capabilities_is_frozen()) return 9;
    if (flat_native_module_set_thread_capability("math", FLAT_NATIVE_THREAD_CAP_UNSUPPORTED) == 0)
        return 10;

    for (i = 0; i < THREAD_COUNT; ++i) {
        if (pthread_create(&threads[i], NULL, query_worker, NULL) != 0) return 11;
    }
    for (i = 0; i < THREAD_COUNT; ++i) {
        if (pthread_join(threads[i], NULL) != 0) return 12;
    }
    if (atomic_load_explicit(&g_failures, memory_order_relaxed) != 0) return 13;

    puts("native-capabilities-ok workers=8 queries=1600000");
    return 0;
}
