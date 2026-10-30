#include "fb_format.h"
#include "flat.h"
#include "flat_native_plugin.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_version(void) {
    printf("flvm %s (flatL VM)\n", FLAT_TOOLCHAIN_VERSION);
}

static int usage(const char *prog) {
    fprintf(stderr, "Usage: %s --version\n", prog);
    fprintf(stderr, "       %s [--trace] [--trace-vars] program.fb\n", prog);
    fprintf(stderr, "\n  --trace       log each opcode to stderr\n");
    fprintf(stderr, "  --trace-vars  log opcode + variable snapshot to stderr\n");
    return 2;
}

int main(int argc, char **argv) {
    FlatBytecode bc;
    FlatVmOptions opts = {0, 0};
    const char *fb_path = NULL;
    int rc;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(argv[i], "--trace") == 0) {
            opts.trace = 1;
            continue;
        }
        if (strcmp(argv[i], "--trace-vars") == 0) {
            opts.trace = 1;
            opts.trace_vars = 1;
            continue;
        }
        if (argv[i][0] == '-') {
            return usage(argv[0]);
        }
        fb_path = argv[i];
    }
    if (!fb_path) {
        return usage(argv[0]);
    }

    flat_native_runtime_set_search_dir_from_argv0(argv[0]);
    if (flat_fb_load_file(fb_path, &bc) != 0) {
        return 1;
    }
    rc = flat_vm_run_opts(&bc, &opts);
    flat_fb_free(&bc);
    return rc;
}
