#include "flat_native_plugin.h"

#include <stdatomic.h>
#include <time.h>

static atomic_int active = ATOMIC_VAR_INIT(0);
static atomic_int overlap = ATOMIC_VAR_INIT(0);

static int test_handler(FlatNativeCtx *ctx) {
    struct timespec pause = {0, 1000000L};
    int previous;
    (void)ctx;
    previous = atomic_fetch_add_explicit(&active, 1, memory_order_relaxed);
    if (previous != 0) atomic_store_explicit(&overlap, 1, memory_order_relaxed);
    nanosleep(&pause, NULL);
    atomic_fetch_sub_explicit(&active, 1, memory_order_relaxed);
    return atomic_load_explicit(&overlap, memory_order_relaxed) ? -1 : 0;
}

int flat_native_plugin_init(FlatNativePluginRegisterFn reg) {
    if (!reg) return -1;
    reg(FLAT_NATIVE_LIST_EMPTY, test_handler);
    return 0;
}

FlatNativeThreadCapability flat_native_plugin_thread_capability(void) {
    return FLAT_NATIVE_THREAD_CAP_SERIALIZED;
}
