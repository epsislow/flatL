#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_plugin.h"

int flat_native_plugin_init(FlatNativePluginRegisterFn reg) {
    if (!reg) {
        return -1;
    }
    reg(FLAT_NATIVE_LIST_EMPTY, flat_native_list_empty);
    reg(FLAT_NATIVE_LIST_PACK, flat_native_list_pack);
    reg(FLAT_NATIVE_LIST_PUSH, flat_native_list_push);
    reg(FLAT_NATIVE_LIST_GET, flat_native_list_get);
    reg(FLAT_NATIVE_LIST_LEN, flat_native_list_len);
    return 0;
}
