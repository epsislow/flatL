#include "fb_format.h"
#include "flat_runtime.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    int file_index;
    if (argc != 4) return 2;

    for (file_index = 1; file_index < argc; ++file_index) {
        FlatBytecode bc;
        int iteration;
        memset(&bc, 0, sizeof bc);
        if (flat_fb_load_file(argv[file_index], &bc) != 0) return 3;

        for (iteration = 0; iteration < 3; ++iteration) {
            FlatVmResult result;
            int rc;
            memset(&result, 0, sizeof result);
            rc = flat_runtime_run_scheduled(&bc, NULL, &result);
            if (rc != 0 || result.exit_code != 0 ||
                result.termination != FLAT_TERM_OK || result.state != FLAT_VM_COMPLETED) {
                fprintf(stderr,
                        "scheduled runtime file %d iteration %d failed: rc=%d exit=%d term=%d state=%d diagnostic=%s\n",
                        file_index, iteration, rc, result.exit_code,
                        (int)result.termination, (int)result.state, result.diagnostic);
                flat_fb_free(&bc);
                return 4;
            }
        }
        flat_fb_free(&bc);
    }

    puts("runtime-scheduled-task-channel-ok");
    return 0;
}
