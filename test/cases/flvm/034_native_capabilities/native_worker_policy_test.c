#include "flat_native_plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int add_decl(FlatOptionalNativeDecl *decl, const char *module) {
    memset(decl, 0, sizeof(*decl));
    if (snprintf(decl->module, sizeof(decl->module), "%s", module) >=
        (int)sizeof(decl->module)) return -1;
    return 0;
}

static int expect_policy(const FlatNativeRuntimeContext *runtime, uint16_t id,
                         FlatNativeThreadCapability expected, const char *label) {
    FlatNativeThreadCapability actual = FLAT_NATIVE_THREAD_CAP_UNKNOWN;
    char err[256];
    if (flat_native_runtime_worker_policy(runtime, id, &actual, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s preflight failed: %s\n", label, err);
        return -1;
    }
    if (actual != expected) {
        fprintf(stderr, "%s policy mismatch: got %d expected %d\n", label,
                (int)actual, (int)expected);
        return -1;
    }
    return 0;
}

static int expect_rejected(const FlatNativeRuntimeContext *runtime, uint16_t id,
                           const char *label) {
    FlatNativeThreadCapability actual = FLAT_NATIVE_THREAD_CAP_UNKNOWN;
    char err[256];
    if (flat_native_runtime_worker_policy(runtime, id, &actual, err, sizeof(err)) == 0) {
        fprintf(stderr, "%s unexpectedly admitted with policy %d\n", label, (int)actual);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    FlatOptionalNativeDecl decls[4];
    FlatNativeRuntimeContext runtime;

    if (argc != 3) return 2;
    if (setenv("FLAT_NATIVE_PATH", "", 1) != 0) return 3;
    if (add_decl(&decls[0], FLAT_NATIVE_MODULE_MATH) != 0 ||
        add_decl(&decls[1], FLAT_NATIVE_MODULE_LIST) != 0 ||
        add_decl(&decls[2], FLAT_NATIVE_MODULE_OBJ) != 0 ||
        add_decl(&decls[3], FLAT_NATIVE_MODULE_MATH_TRIG) != 0) return 4;
    runtime.manifest = decls;
    runtime.manifest_count = 4;

    flat_native_runtime_set_search_dir(argv[2]);
    if (expect_policy(&runtime, FLAT_NATIVE_MATH_POW,
                      FLAT_NATIVE_THREAD_CAP_THREAD_SAFE, "bundled math plugin") != 0) return 5;

    flat_native_runtime_set_search_dir(argv[1]);
    if (expect_policy(&runtime, FLAT_NATIVE_LIST_EMPTY,
                      FLAT_NATIVE_THREAD_CAP_SERIALIZED, "declared serialized plugin") != 0)
        return 6;
    if (expect_rejected(&runtime, FLAT_NATIVE_OBJ_EMPTY, "legacy plugin") != 0) return 7;
    if (expect_policy(&runtime, FLAT_NATIVE_TASK_SPAWN,
                      FLAT_NATIVE_THREAD_CAP_SERIALIZED, "task module") != 0) return 8;
    if (flat_native_runtime_freeze_worker_capabilities() != 0) return 9;
    if (expect_rejected(&runtime, FLAT_NATIVE_MATH_TRIG_SIN,
                        "lazy plugin load after capability freeze") != 0) return 10;

    puts("native-worker-policy-ok bundled=thread-safe declared=serialized task=serialized legacy=denied frozen=load-denied");
    return 0;
}
