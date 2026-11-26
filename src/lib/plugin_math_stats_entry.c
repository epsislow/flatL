#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_plugin.h"

int flat_native_plugin_init(FlatNativePluginRegisterFn reg) {
    if (!reg) {
        return -1;
    }
    reg(FLAT_NATIVE_MATH_STATS_MEAN, flat_native_math_stats_mean);
    reg(FLAT_NATIVE_MATH_STATS_MEDIAN, flat_native_math_stats_median);
    reg(FLAT_NATIVE_MATH_STATS_VAR, flat_native_math_stats_var);
    reg(FLAT_NATIVE_MATH_STATS_STDEV, flat_native_math_stats_stdev);
    return 0;
}

/* All mutable execution state is supplied by the calling VM context. */
FlatNativeThreadCapability flat_native_plugin_thread_capability(void) {
    return FLAT_NATIVE_THREAD_CAP_THREAD_SAFE;
}
