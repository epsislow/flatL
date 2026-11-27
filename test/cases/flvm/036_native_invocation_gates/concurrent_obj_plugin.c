#include "flat_native_plugin.h"

#include <time.h>

static int test_handler(FlatNativeCtx *ctx) {
    (void)ctx;
    return 42;
}

int flat_native_plugin_init(FlatNativePluginRegisterFn reg) {
    struct timespec pause = {0, 5000000L};
    if (!reg) return -1;
    /* Keep initialization open briefly so another module's first-use caller
       reaches the process-wide loader while this module is being staged. */
    nanosleep(&pause, NULL);
    reg(FLAT_NATIVE_OBJ_EMPTY, test_handler);
    return 0;
}

FlatNativeThreadCapability flat_native_plugin_thread_capability(void) {
    return FLAT_NATIVE_THREAD_CAP_THREAD_SAFE;
}
