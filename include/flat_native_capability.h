#ifndef FLAT_NATIVE_CAPABILITY_H
#define FLAT_NATIVE_CAPABILITY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLAT_NATIVE_THREAD_CAP_UNKNOWN = 0,
    FLAT_NATIVE_THREAD_CAP_THREAD_SAFE = 1,
    FLAT_NATIVE_THREAD_CAP_SERIALIZED = 2,
    FLAT_NATIVE_THREAD_CAP_UNSUPPORTED = 3,
    FLAT_NATIVE_THREAD_CAP_PENDING = 4
} FlatNativeThreadCapability;

/* Process-wide module capability metadata. Configure before worker startup. */
FlatNativeThreadCapability flat_native_module_thread_capability(const char *module);
/* PENDING is conservatively mapped to SERIALIZED; UNKNOWN maps to UNSUPPORTED. */
FlatNativeThreadCapability flat_native_module_worker_policy(const char *module);
/* Plugin loader may publish a module declaration before worker startup. */
int flat_native_module_set_thread_capability(const char *module,
                                             FlatNativeThreadCapability capability);
/* Freeze capability declarations when a future worker pool starts. */
int flat_native_module_capabilities_freeze(void);
int flat_native_module_capabilities_is_frozen(void);
/* Process-wide invocation gate for one native module. The dispatcher acquires
 * it only for SERIALIZED/PENDING modules; teardown paths such as DOM must use
 * the same gate when they call into the module's dependency directly. */
int flat_native_module_gate_enter(const char *module);
int flat_native_module_gate_leave(const char *module);

#ifdef __cplusplus
}
#endif
#endif
