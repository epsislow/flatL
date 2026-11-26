#include "flat_native_plugin.h"

static int should_never_be_published(FlatNativeCtx *ctx) {
    (void)ctx;
    return 0;
}

int flat_native_plugin_init(FlatNativePluginRegisterFn reg) {
    reg(FLAT_NATIVE_LIST_EMPTY, should_never_be_published);
    return -1;
}
