#include "flat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_version(void) {
    printf("flvm %s (flatL VM)\n", FLAT_TOOLCHAIN_VERSION);
}

static int usage(const char *prog) {
    fprintf(stderr, "Usage: %s --version\n", prog);
    fprintf(stderr, "       %s program.fb   (not implemented yet)\n", prog);
    return 2;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)) {
        print_version();
        return 0;
    }
    if (argc != 2) {
        return usage(argv[0]);
    }
    fprintf(stderr, "flvm: execution not implemented yet (%s)\n", argv[1]);
    return 1;
}
