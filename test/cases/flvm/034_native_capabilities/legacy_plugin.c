#include "flat_native_plugin.h"

static int test_handler(FlatNativeCtx *ctx) {
    (void)ctx;
    return 0;
}

/* Deliberately uses the legacy initializer and exports no capability symbol. */
int flat_native_plugin_init(FlatNativePluginRegisterFn reg) {
    if (!reg) return -1;
    reg(FLAT_NATIVE_OBJ_EMPTY, test_handler);
    return 0;
}
