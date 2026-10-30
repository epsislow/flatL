#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_plugin.h"

int flat_native_plugin_init(FlatNativePluginRegisterFn reg) {
    if (!reg) {
        return -1;
    }
    reg(FLAT_NATIVE_OBJ_EMPTY, flat_native_obj_empty);
    reg(FLAT_NATIVE_OBJ_GET, flat_native_obj_get);
    reg(FLAT_NATIVE_OBJ_SET, flat_native_obj_set);
    reg(FLAT_NATIVE_OBJ_HAS, flat_native_obj_has);
    reg(FLAT_NATIVE_OBJ_LEN, flat_native_obj_len);
    reg(FLAT_NATIVE_OBJ_REMOVE, flat_native_obj_remove);
    reg(FLAT_NATIVE_OBJ_KEYS, flat_native_obj_keys);
    reg(FLAT_NATIVE_OBJ_VALUES, flat_native_obj_values);
    return 0;
}
