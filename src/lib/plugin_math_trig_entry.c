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
    reg(FLAT_NATIVE_MATH_TRIG_NUM_PI, flat_native_math_trig_num_pi);
    reg(FLAT_NATIVE_MATH_TRIG_NUM_TAU, flat_native_math_trig_num_tau);
    reg(FLAT_NATIVE_MATH_TRIG_DEG_TO_RAD, flat_native_math_trig_deg_to_rad);
    reg(FLAT_NATIVE_MATH_TRIG_RAD_TO_DEG, flat_native_math_trig_rad_to_deg);
    reg(FLAT_NATIVE_MATH_TRIG_SIN_DEG, flat_native_math_trig_sin_deg);
    reg(FLAT_NATIVE_MATH_TRIG_COS_DEG, flat_native_math_trig_cos_deg);
    reg(FLAT_NATIVE_MATH_TRIG_TAN_DEG, flat_native_math_trig_tan_deg);
    return 0;
}
