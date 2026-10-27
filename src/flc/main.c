#include "flat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_version(void) {
    printf("flc %s (flatL compiler)\n", FLAT_TOOLCHAIN_VERSION);
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)) {
        print_version();
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "-o") == 0) {
        fprintf(stderr, "flc: compile not implemented yet (%s -> %s)\n", argv[3], argv[2]);
        return 1;
    }
    fprintf(stderr, "Usage: %s --version\n", argv[0]);
    fprintf(stderr, "       %s -o out.fb in.fl   (not implemented yet)\n", argv[0]);
    return 2;
}
