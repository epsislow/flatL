#include "flat_host_config.h"

#include <string.h>

int flat_host_config_init(FlatHostConfig *config) {
    int use_process = 1;
    if (!config) return -1;
    memset(config, 0, sizeof *config);
#ifdef FLAT_WASM_BUILD
    use_process = 0;
#endif
    if (flat_env_context_init_empty(&config->env, use_process) != 0) return -1;
    flat_path_context_init(&config->path);
    return 0;
}

int flat_host_config_capture_staging(FlatHostConfig *config) {
    if (!config) return -1;
    memset(config, 0, sizeof *config);
    if (flat_env_context_init(&config->env) != 0) return -1;
    flat_path_context_init(&config->path);
    return 0;
}

void flat_host_config_destroy(FlatHostConfig *config) {
    if (!config) return;
    flat_env_context_destroy(&config->env);
    memset(&config->path, 0, sizeof config->path);
}