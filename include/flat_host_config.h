#ifndef FLAT_HOST_CONFIG_H
#define FLAT_HOST_CONFIG_H

#include "flat_env.h"
#include "flat_path.h"

/*
 * Host configuration for one logical FlatL runtime instance.
 *
 * Configure before passing it to flat_vm_run_opts(). The VM takes an owned
 * snapshot at run start, so the configuration must not be mutated or
 * destroyed concurrently with that call. It may be reused by sequential or
 * concurrent VM runs while it remains immutable.
 */
typedef struct FlatHostConfig {
    FlatEnvContext env;
    FlatPathContext path;
} FlatHostConfig;

/* Initialize an independent configuration with host-platform defaults. */
int flat_host_config_init(FlatHostConfig *config);

/*
 * Capture the legacy process staging values into an independent config.
 * The host must not mutate legacy env/path staging while this snapshot is made.
 */
int flat_host_config_capture_staging(FlatHostConfig *config);

void flat_host_config_destroy(FlatHostConfig *config);

#endif