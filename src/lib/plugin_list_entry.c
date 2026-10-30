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
    reg(FLAT_NATIVE_LIST_PREPEND, flat_native_list_prepend);
    reg(FLAT_NATIVE_LIST_INSERT, flat_native_list_insert);
    reg(FLAT_NATIVE_LIST_SET, flat_native_list_set);
    reg(FLAT_NATIVE_LIST_INDEX_OF, flat_native_list_index_of);
    reg(FLAT_NATIVE_LIST_CONTAINS, flat_native_list_contains);
    reg(FLAT_NATIVE_LIST_REMOVE_AT, flat_native_list_remove_at);
    reg(FLAT_NATIVE_LIST_SLICE, flat_native_list_slice);
    reg(FLAT_NATIVE_LIST_SPLICE, flat_native_list_splice);
    reg(FLAT_NATIVE_LIST_RANGE, flat_native_list_range);
    reg(FLAT_NATIVE_LIST_REPEAT, flat_native_list_repeat);
    return 0;
}
