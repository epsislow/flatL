#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_plugin.h"

int flat_native_plugin_init(FlatNativePluginRegisterFn reg) {
    if (!reg) {
        return -1;
    }
    reg(FLAT_NATIVE_MATH_MDIM_ADD, flat_native_math_mdim_add);
    reg(FLAT_NATIVE_MATH_MDIM_SUBTRACT, flat_native_math_mdim_subtract);
    reg(FLAT_NATIVE_MATH_MDIM_MULTIPLY, flat_native_math_mdim_multiply);
    reg(FLAT_NATIVE_MATH_MDIM_DIVIDE, flat_native_math_mdim_divide);
    reg(FLAT_NATIVE_MATH_MDIM_SUM, flat_native_math_mdim_sum);
    reg(FLAT_NATIVE_MATH_MDIM_MIN, flat_native_math_mdim_min);
    reg(FLAT_NATIVE_MATH_MDIM_MAX, flat_native_math_mdim_max);
    reg(FLAT_NATIVE_MATH_MDIM_AVG, flat_native_math_mdim_avg);
    reg(FLAT_NATIVE_MATH_MDIM_DOT, flat_native_math_mdim_dot);
    reg(FLAT_NATIVE_MATH_MDIM_TRANSPOSE, flat_native_math_mdim_transpose);
    reg(FLAT_NATIVE_MATH_MDIM_MATMUL, flat_native_math_mdim_matmul);
    return 0;
}