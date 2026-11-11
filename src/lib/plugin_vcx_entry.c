#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_plugin.h"

int flat_native_plugin_init(FlatNativePluginRegisterFn reg) {
    if (!reg) {
        return -1;
    }
    reg(FLAT_NATIVE_VCX_CURRENT, flat_native_vcx_current);
    reg(FLAT_NATIVE_VCX_ISACTIVE, flat_native_vcx_isactive);
    reg(FLAT_NATIVE_VCX_SET, flat_native_vcx_set);
    reg(FLAT_NATIVE_VCX_CREATE, flat_native_vcx_create);
    reg(FLAT_NATIVE_VCX_CREATE_AUTO, flat_native_vcx_create_auto);
    reg(FLAT_NATIVE_VCX_VAR, flat_native_vcx_var);
    reg(FLAT_NATIVE_VCX_HAS_VAR, flat_native_vcx_has_var);
    reg(FLAT_NATIVE_VCX_VARS_COPY, flat_native_vcx_vars_copy);
    reg(FLAT_NATIVE_VCX_VARS_COPY_LIKE, flat_native_vcx_vars_copy_like);
    reg(FLAT_NATIVE_VCX_VARS_USED, flat_native_vcx_vars_used);
    reg(FLAT_NATIVE_VCX_VARS_AVAIL, flat_native_vcx_vars_avail);
    reg(FLAT_NATIVE_VCX_VARS_MAX, flat_native_vcx_vars_max);
    reg(FLAT_NATIVE_VCX_DESTROY, flat_native_vcx_destroy);
    reg(FLAT_NATIVE_VCX_VARS_COPY_BY_NAMES, flat_native_vcx_vars_copy_by_names);
    reg(FLAT_NATIVE_VCX_CALL_INSTR, flat_native_vcx_call_instr);
    reg(FLAT_NATIVE_VCX_DSCHEMA_ALLOW, flat_native_vcx_dschema_allow);
    reg(FLAT_NATIVE_VCX_DSCHEMA_LIST, flat_native_vcx_dschema_list);
    return 0;
}
