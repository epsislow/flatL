#include "flat_native_capability.h"
#include "flat_sync.h"

#include <stdatomic.h>
#include <string.h>

typedef struct {
    const char *name;
    FlatNativeThreadCapability default_capability;
    FlatNativeThreadCapability override_capability;
    unsigned char has_override;
    FlatMutex invocation_gate;
} ModuleCapability;

static ModuleCapability g_modules[] = {
    {"io", FLAT_NATIVE_THREAD_CAP_SERIALIZED, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"json", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"str", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"date", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"clock", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"http", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"dom", FLAT_NATIVE_THREAD_CAP_PENDING, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"env", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"path", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"fs", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"enc", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"re", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"hash", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"bytes", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"mem", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"file", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"math.trig", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"math.stats", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"math.mdim", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"math", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"list", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"obj", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"vcx", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"executor", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"csv", FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"task", FLAT_NATIVE_THREAD_CAP_UNSUPPORTED, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {"channel", FLAT_NATIVE_THREAD_CAP_UNSUPPORTED, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
    {NULL, FLAT_NATIVE_THREAD_CAP_UNKNOWN, FLAT_NATIVE_THREAD_CAP_UNKNOWN, 0, FLAT_MUTEX_INITIALIZER},
};

static atomic_flag g_capability_lock = ATOMIC_FLAG_INIT;
static atomic_int g_capabilities_frozen = ATOMIC_VAR_INIT(0);

static void capability_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_capability_lock, memory_order_acquire)) {
    }
}

static void capability_unlock(void) {
    atomic_flag_clear_explicit(&g_capability_lock, memory_order_release);
}

static int module_index(const char *module) {
    int i;
    if (!module || !module[0]) return -1;
    for (i = 0; i < (int)(sizeof g_modules / sizeof g_modules[0]); ++i) {
        if (!g_modules[i].name) break;
        if (strcmp(g_modules[i].name, module) == 0) return i;
    }
    return -1;
}

FlatNativeThreadCapability flat_native_module_thread_capability(const char *module) {
    FlatNativeThreadCapability capability = FLAT_NATIVE_THREAD_CAP_UNSUPPORTED;
    int index = module_index(module);
    if (index < 0) return capability;
    capability_lock();
    capability = g_modules[index].has_override ? g_modules[index].override_capability
                                                : g_modules[index].default_capability;
    capability_unlock();
    return capability;
}

FlatNativeThreadCapability flat_native_module_worker_policy(const char *module) {
    FlatNativeThreadCapability capability = flat_native_module_thread_capability(module);
    if (capability == FLAT_NATIVE_THREAD_CAP_PENDING) return FLAT_NATIVE_THREAD_CAP_SERIALIZED;
    if (capability == FLAT_NATIVE_THREAD_CAP_UNKNOWN) return FLAT_NATIVE_THREAD_CAP_UNSUPPORTED;
    return capability;
}

int flat_native_module_set_thread_capability(const char *module,
                                             FlatNativeThreadCapability capability) {
    int index = module_index(module);
    if (index < 0 || capability < FLAT_NATIVE_THREAD_CAP_THREAD_SAFE ||
        capability > FLAT_NATIVE_THREAD_CAP_PENDING ||
        atomic_load_explicit(&g_capabilities_frozen, memory_order_acquire)) {
        return -1;
    }
    capability_lock();
    if (atomic_load_explicit(&g_capabilities_frozen, memory_order_relaxed)) {
        capability_unlock();
        return -1;
    }
    g_modules[index].override_capability = capability;
    g_modules[index].has_override = 1;
    capability_unlock();
    return 0;
}

int flat_native_module_capabilities_freeze(void) {
    capability_lock();
    atomic_store_explicit(&g_capabilities_frozen, 1, memory_order_release);
    capability_unlock();
    return 0;
}

int flat_native_module_capabilities_is_frozen(void) {
    return atomic_load_explicit(&g_capabilities_frozen, memory_order_acquire) != 0;
}

int flat_native_module_gate_enter(const char *module) {
    int index = module_index(module);
    if (index < 0) return -1;
    return flat_mutex_lock(&g_modules[index].invocation_gate);
}

int flat_native_module_gate_leave(const char *module) {
    int index = module_index(module);
    if (index < 0) return -1;
    return flat_mutex_unlock(&g_modules[index].invocation_gate);
}
