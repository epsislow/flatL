#include "flat_disasm.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    FlatDisasmOpts opts;
    const char *path = NULL;
    int pr;

    if (argc < 2) {
        return flat_disasm_usage_fbdump(argv[0]);
    }

    pr = flat_disasm_parse_args(argc, argv, 1, &opts, &path);
    if (pr == 1) {
        return flat_disasm_usage_fbdump(argv[0]);
    }
    if (pr != 0 || !path) {
        fprintf(stderr, "fbdump: missing program.fb\n");
        return flat_disasm_usage_fbdump(argv[0]);
    }

    if (flat_disasm_file(path, stdout, &opts) != 0) {
        return 1;
    }
    return 0;
}
