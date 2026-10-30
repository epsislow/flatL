#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_plugin.h"

int flat_native_plugin_init(FlatNativePluginRegisterFn reg) {
    if (!reg) {
        return -1;
    }
    reg(FLAT_NATIVE_MATH_POW, flat_native_math_pow);
    reg(FLAT_NATIVE_MATH_SQRT, flat_native_math_sqrt);
    reg(FLAT_NATIVE_MATH_EXP, flat_native_math_exp);
    reg(FLAT_NATIVE_MATH_LOG, flat_native_math_log);
    reg(FLAT_NATIVE_MATH_LOG10, flat_native_math_log10);
    reg(FLAT_NATIVE_MATH_FLOOR, flat_native_math_floor);
    reg(FLAT_NATIVE_MATH_CEIL, flat_native_math_ceil);
    reg(FLAT_NATIVE_MATH_ROUND, flat_native_math_round);
    reg(FLAT_NATIVE_MATH_MOD, flat_native_math_mod);
    reg(FLAT_NATIVE_MATH_FLOOR_MOD, flat_native_math_floor_mod);
    reg(FLAT_NATIVE_MATH_SUM, flat_native_math_sum);
    reg(FLAT_NATIVE_MATH_MIN, flat_native_math_min);
    reg(FLAT_NATIVE_MATH_MAX, flat_native_math_max);
    reg(FLAT_NATIVE_MATH_SUM2, flat_native_math_sum2);
    reg(FLAT_NATIVE_MATH_MIN2, flat_native_math_min2);
    reg(FLAT_NATIVE_MATH_MAX2, flat_native_math_max2);
    reg(FLAT_NATIVE_MATH_NUM_PI, flat_native_math_num_pi);
    reg(FLAT_NATIVE_MATH_NUM_TAU, flat_native_math_num_tau);
    reg(FLAT_NATIVE_MATH_NUM_E, flat_native_math_num_e);
    return 0;
}
