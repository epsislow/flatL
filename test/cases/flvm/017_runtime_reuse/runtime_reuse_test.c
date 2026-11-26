#include "fb_format.h"
#include "flat_host_config.h"
#include "flat_runtime.h"

#include <stdio.h>
#include <string.h>

static int is_success_term(int term) {
    return term == FLAT_TERM_OK || term == FLAT_TERM_COMPLETE;
}

int main(int argc, char **argv) {
    FlatBytecode bc;
    FlatHostConfig config;
    FlatRuntime *runtime;
    FlatVmOptions opts;
    int termination = FLAT_TERM_UNKNOWN;
    char panic_msg[256];
    int rc;
    uint8_t saved_opcode;
    uint32_t entry_ip;
    size_t block_index;

    if (argc != 2) return 2;
    memset(&bc, 0, sizeof bc);
    memset(&config, 0, sizeof config);
    memset(&opts, 0, sizeof opts);
    memset(panic_msg, 0, sizeof panic_msg);

    if (flat_fb_load_file(argv[1], &bc) != 0) return 3;
    if (flat_host_config_init(&config) != 0) {
        flat_fb_free(&bc);
        return 4;
    }
    if (flat_env_context_override_set(&config.env, "FLAT_RUNTIME_REUSE", "yes") != 0) {
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

    /* Force a VM execution error after setup, then restore the shared bytecode. */
    for (block_index = 0; block_index < bc.block_count; ++block_index) {
        if (bc.blocks[block_index].block_id == FLAT_FB_BLOCK_ID_MAIN) break;
    }
    if (block_index >= bc.block_count) block_index = 0;
    entry_ip = bc.blocks[block_index].offset;
    if (entry_ip >= bc.code_size) {
        flat_runtime_destroy(runtime);
        flat_fb_free(&bc);
        return 7;
    }
    saved_opcode = bc.data[entry_ip];
    bc.data[entry_ip] = 0xffu;
    opts.out_termination = &termination;
    opts.out_panic_msg = panic_msg;
    opts.out_panic_msg_sz = sizeof panic_msg;
    rc = flat_runtime_run(runtime, &bc, &opts);
    if (rc == 0 || termination != FLAT_TERM_ERROR || panic_msg[0] == '\0') {
        fprintf(stderr, "expected VM execution failure: rc=%d term=%d panic='%s'\n",
                rc, termination, panic_msg);
        flat_runtime_destroy(runtime);
        flat_fb_free(&bc);
        return 8;
    }
    bc.data[entry_ip] = saved_opcode;

    /* The same runtime must remain usable after the failed VM has been torn down. */
    memset(&opts, 0, sizeof opts);
    termination = FLAT_TERM_UNKNOWN;
    opts.out_termination = &termination;
    opts.out_panic_msg = panic_msg;
    opts.out_panic_msg_sz = sizeof panic_msg;
    rc = flat_runtime_run(runtime, &bc, &opts);
    if (rc != 0 || !is_success_term(termination) || panic_msg[0] != '\0') {
        fprintf(stderr, "runtime reuse failed: rc=%d term=%d panic='%s'\n",
                rc, termination, panic_msg);
        flat_runtime_destroy(runtime);
        flat_fb_free(&bc);
        return 9;
    }

    if (flat_runtime_destroy(runtime) != 0) {
        flat_fb_free(&bc);
        return 10;
    }
    flat_fb_free(&bc);
    puts("runtime-reuse-ok");
    return 0;
}
