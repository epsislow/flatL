#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_plugin.h"

int flat_native_plugin_init(FlatNativePluginRegisterFn reg) {
    if (!reg) {
        return -1;
    }
    reg(FLAT_NATIVE_MATH_TRIG_SIN, flat_native_math_trig_sin);
    reg(FLAT_NATIVE_MATH_TRIG_COS, flat_native_math_trig_cos);
    reg(FLAT_NATIVE_MATH_TRIG_TAN, flat_native_math_trig_tan);
    reg(FLAT_NATIVE_MATH_TRIG_SQRT, flat_native_math_trig_sqrt);
    return 0;
}
