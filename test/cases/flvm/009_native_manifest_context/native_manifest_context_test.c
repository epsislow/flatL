#include "flat_native_plugin.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    FlatOptionalNativeDecl manifest_a[1] = {{{0}}};
    FlatOptionalNativeDecl manifest_b[1] = {{{0}}};
    FlatNativeRuntimeContext a;
    FlatNativeRuntimeContext b;
    FlatNativeRuntimeContext empty = {0};

    snprintf(manifest_a[0].module, sizeof manifest_a[0].module, "%s", FLAT_NATIVE_MODULE_MATH);
    snprintf(manifest_b[0].module, sizeof manifest_b[0].module, "%s", FLAT_NATIVE_MODULE_LIST);
    a.manifest = manifest_a;
    a.manifest_count = 1;
    b.manifest = manifest_b;
    b.manifest_count = 1;

    if (!flat_native_runtime_manifest_has_module(&a, FLAT_NATIVE_MODULE_MATH)) return 1;
    if (flat_native_runtime_manifest_has_module(&a, FLAT_NATIVE_MODULE_LIST)) return 2;
    if (!flat_native_runtime_manifest_has_module(&b, FLAT_NATIVE_MODULE_LIST)) return 3;
    if (flat_native_runtime_manifest_has_module(&b, FLAT_NATIVE_MODULE_MATH)) return 4;
    if (flat_native_runtime_manifest_has_module(&empty, FLAT_NATIVE_MODULE_MATH)) return 5;

    /* A lookup in one VM must not mutate or replace the other VM's manifest. */
    if (!flat_native_runtime_manifest_has_module(&a, FLAT_NATIVE_MODULE_MATH) ||
        !flat_native_runtime_manifest_has_module(&b, FLAT_NATIVE_MODULE_LIST)) return 6;

    puts("native-manifest-context-isolation-ok");
    return 0;
}
