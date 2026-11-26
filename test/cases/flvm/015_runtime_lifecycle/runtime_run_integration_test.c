#include "fb_format.h"
#include "flat_host_config.h"
#include "flat_runtime.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    FlatBytecode bc;
    FlatHostConfig config;
    FlatRuntime *runtime;
    FlatVmOptions opts;
    int termination = FLAT_TERM_UNKNOWN;
    int rc;

    if (argc != 2) return 2;
    memset(&bc, 0, sizeof bc);
    memset(&config, 0, sizeof config);
    memset(&opts, 0, sizeof opts);

    if (flat_fb_load_file(argv[1], &bc) != 0) return 3;
    if (flat_host_config_init(&config) != 0) {
        flat_fb_free(&bc);
        return 4;
    }
    if (flat_env_context_override_set(&config.env, "FLAT_RUNTIME_API", "active") != 0) {
        flat_host_config_destroy(&config);
        flat_fb_free(&bc);
        return 5;
    }

    runtime = flat_runtime_create(&config);
    flat_host_config_destroy(&config);
    if (!runtime) {
        flat_fb_free(&bc);
        return 6;
    }

    opts.out_termination = &termination;
    rc = flat_runtime_run(runtime, &bc, &opts);
    if (flat_runtime_destroy(runtime) != 0) rc = 7;
    flat_fb_free(&bc);

    if (rc != 0 || (termination != FLAT_TERM_OK && termination != FLAT_TERM_COMPLETE)) {
        fprintf(stderr, "runtime-run-failed rc=%d termination=%d\n", rc, termination);
        return 8;
    }
    puts("runtime-run-integration-ok");
    return 0;
}
