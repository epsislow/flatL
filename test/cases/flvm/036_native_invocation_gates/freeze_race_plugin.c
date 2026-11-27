#include "flat_native_plugin.h"

#include <time.h>

static int test_handler(FlatNativeCtx *ctx) {
    (void)ctx;
    return 43;
}

int flat_native_plugin_init(FlatNativePluginRegisterFn reg) {
    struct timespec pause = {0, 5000000L};
    if (!reg) return -1;
    nanosleep(&pause, NULL);
    reg(FLAT_NATIVE_CSV_READER, test_handler);
    return 0;
}

FlatNativeThreadCapability flat_native_plugin_thread_capability(void) {
    return FLAT_NATIVE_THREAD_CAP_THREAD_SAFE;
}
