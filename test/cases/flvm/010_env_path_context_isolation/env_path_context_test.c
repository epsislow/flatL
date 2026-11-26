#include "flat_env.h"
#include "flat_host_config.h"
#include "flat_path.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    FlatEnvContext env_a = {0}, env_b = {0};
    FlatPathContext path_a, path_b;
    FlatHostConfig host_a, host_b;
    FlatHostConfig captured;
    FlatEnvContext vm_snapshot = {0};
    char value[64];
    char joined[FLAT_PATH_MAX];
    const char *parts[] = {"alpha", "beta"};
    int found = 0;

    flat_env_reset();
    flat_env_set_use_process(0);
    if (flat_env_override_set("FLAT_CONTEXT_TEST", "first") != 0) return 1;
    if (flat_env_args_push("arg-first") != 0) return 2;
    if (flat_env_context_init(&env_a) != 0) return 3;

    if (flat_env_override_set("FLAT_CONTEXT_TEST", "second") != 0) return 4;
    if (flat_env_args_clear() != 0 || flat_env_args_push("arg-second") != 0) return 5;
    if (flat_env_context_init(&env_b) != 0) return 6;

    if (flat_env_context_lookup(&env_a, "FLAT_CONTEXT_TEST", value, sizeof value, &found) != 0 ||
        !found || strcmp(value, "first") != 0) return 7;
    if (flat_env_context_lookup(&env_b, "FLAT_CONTEXT_TEST", value, sizeof value, &found) != 0 ||
        !found || strcmp(value, "second") != 0) return 8;
    if (flat_env_context_argc(&env_a) != 1 ||
        strcmp(flat_env_context_arg(&env_a, 0), "arg-first") != 0) return 9;
    if (flat_env_context_argc(&env_b) != 1 ||
        strcmp(flat_env_context_arg(&env_b, 0), "arg-second") != 0) return 10;

    flat_path_sep_reset();
    flat_path_context_init(&path_a);
    if (flat_path_sep_set('\\') != 0) return 11;
    flat_path_context_init(&path_b);
    if (flat_path_join_ctx(&path_a, parts, 2, joined, sizeof joined, NULL) != 0 ||
        strcmp(joined, "alpha/beta") != 0) return 12;
    if (flat_path_join_ctx(&path_b, parts, 2, joined, sizeof joined, NULL) != 0 ||
        strcmp(joined, "alpha\\beta") != 0) return 13;
    if (flat_path_context_set(&path_a, '\\') != 0) return 14;
    if (flat_path_join_ctx(&path_a, parts, 2, joined, sizeof joined, NULL) != 0 ||
        strcmp(joined, "alpha\\beta") != 0) return 15;

    flat_env_context_destroy(&env_a);
    flat_env_context_destroy(&env_b);

    /* Independent host configs no longer need to share process staging. */
    if (flat_host_config_init(&host_a) != 0 || flat_host_config_init(&host_b) != 0) return 16;
    if (flat_env_context_set_use_process(&host_a.env, 0) != 0 ||
        flat_env_context_set_use_process(&host_b.env, 0) != 0) return 17;
    if (flat_env_context_override_set(&host_a.env, "HOST_CONFIG_TEST", "A") != 0 ||
        flat_env_context_override_set(&host_b.env, "HOST_CONFIG_TEST", "B") != 0) return 18;
    if (flat_env_context_args_push(&host_a.env, "a-arg") != 0 ||
        flat_env_context_args_push(&host_b.env, "b-arg") != 0) return 19;
    if (flat_path_context_set(&host_a.path, '/') != 0 ||
        flat_path_context_set(&host_b.path, '\\') != 0) return 20;
    if (flat_env_context_clone(&vm_snapshot, &host_a.env) != 0) return 21;
    if (flat_env_context_override_set(&host_a.env, "HOST_CONFIG_TEST", "A-mutated") != 0) return 22;
    if (flat_env_context_lookup(&vm_snapshot, "HOST_CONFIG_TEST", value, sizeof value, &found) != 0 ||
        !found || strcmp(value, "A") != 0) return 23;
    if (flat_env_context_lookup(&host_b.env, "HOST_CONFIG_TEST", value, sizeof value, &found) != 0 ||
        !found || strcmp(value, "B") != 0) return 24;
    if (flat_path_context_sep(&host_a.path) != '/' || flat_path_context_sep(&host_b.path) != '\\') return 25;
    if (flat_host_config_capture_staging(&captured) != 0) return 26;
    if (flat_env_context_lookup(&captured.env, "FLAT_CONTEXT_TEST", value, sizeof value, &found) != 0 ||
        !found || strcmp(value, "second") != 0) return 27;
    if (flat_path_context_sep(&captured.path) != '\\') return 28;
    flat_env_context_destroy(&vm_snapshot);
    flat_host_config_destroy(&host_a);
    flat_host_config_destroy(&host_b);
    flat_host_config_destroy(&captured);
    flat_env_reset();
    puts("env-path-host-config-isolation-ok");
    return 0;
}
