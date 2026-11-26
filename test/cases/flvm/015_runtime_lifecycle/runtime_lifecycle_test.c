#include "flat_runtime.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static atomic_int g_vm_started = ATOMIC_VAR_INIT(0);
static atomic_int g_vm_release = ATOMIC_VAR_INIT(0);
static atomic_int g_vm_bad_config = ATOMIC_VAR_INIT(0);
static FlatRuntime *g_runtime;
static FlatBytecode g_bytecode;

/* Stub the VM boundary so this test can exercise FlatRuntime lifecycle alone. */
int flat_vm_run_opts(const FlatBytecode *bc, const FlatVmOptions *opts) {
    char value[32];
    int found = 0;
    if (!bc || !opts || !opts->host_config ||
        flat_env_context_lookup(&opts->host_config->env, "RUNTIME_INSTANCE",
                                value, sizeof value, &found) != 0 ||
        !found || strcmp(value, "alpha") != 0 ||
        flat_path_context_sep(&opts->host_config->path) != '\\') {
        atomic_store(&g_vm_bad_config, 1);
    }
    atomic_store(&g_vm_started, 1);
    while (!atomic_load(&g_vm_release)) { }
    return 23;
}

static void *run_main(void *unused) {
    (void)unused;
    return (void *)(long)flat_runtime_run(g_runtime, &g_bytecode, NULL);
}

int main(void) {
    FlatHostConfig config;
    pthread_t thread;
    void *thread_result = NULL;
    memset(&config, 0, sizeof config);
    if (flat_host_config_init(&config) != 0) return 1;
    if (flat_env_context_override_set(&config.env, "RUNTIME_INSTANCE", "alpha") != 0)
        return 2;
    if (flat_path_context_set(&config.path, '\\') != 0) return 3;

    g_runtime = flat_runtime_create(&config);
    if (!g_runtime) return 4;

    /* Runtime owns a deep snapshot; source config can be destroyed now. */
    flat_host_config_destroy(&config);

    if (pthread_create(&thread, NULL, run_main, NULL) != 0) return 5;
    while (!atomic_load(&g_vm_started)) { }

    /* An active runtime cannot be destroyed while a VM call is in flight. */
    if (flat_runtime_destroy(g_runtime) == 0) return 6;
    atomic_store(&g_vm_release, 1);
    pthread_join(thread, &thread_result);
    if ((long)thread_result != 23 || atomic_load(&g_vm_bad_config)) return 7;
    if (flat_runtime_destroy(g_runtime) != 0) return 8;
    g_runtime = NULL;

    /* NULL config captures the legacy staging state independently. */
    if (flat_env_override_set("RUNTIME_INSTANCE", "staging") != 0) return 9;
    g_runtime = flat_runtime_create(NULL);
    if (!g_runtime) return 10;
    if (flat_runtime_destroy(g_runtime) != 0) return 11;

    puts("runtime-lifecycle-ok");
    return 0;
}
