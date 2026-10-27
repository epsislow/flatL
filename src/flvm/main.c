#include "fb_format.h"
#include "flat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_version(void) {
    printf("flvm %s (flatL VM)\n", FLAT_TOOLCHAIN_VERSION);
}

static int usage(const char *prog) {
    fprintf(stderr, "Usage: %s --version\n", prog);
    fprintf(stderr, "       %s program.fb\n", prog);
    return 2;
}

int main(int argc, char **argv) {
    FlatBytecode bc;
    int rc;

    if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)) {
        print_version();
        return 0;
    }
    if (argc != 2) {
        return usage(argv[0]);
    }

    if (flat_fb_load_file(argv[1], &bc) != 0) {
        return 1;
    }
    rc = flat_vm_run(&bc);
    flat_fb_free(&bc);
    return rc;
}
