#include "fgir.h"
#include "flat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_version(void) {
    printf("flc %s (flatL compiler)\n", FLAT_TOOLCHAIN_VERSION);
}

static int usage(const char *prog) {
    fprintf(stderr, "Usage: %s --version\n", prog);
    fprintf(stderr, "       %s --dump-ir program.fl\n", prog);
    fprintf(stderr, "       %s -o out.fb program.fl\n", prog);
    return 2;
}

int main(int argc, char **argv) {
    FgirProgram prog;
    char err[512];
    const char *in_path = NULL;
    const char *out_path = NULL;
    int dump_ir = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(argv[i], "--dump-ir") == 0) {
            dump_ir = 1;
            continue;
        }
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
            continue;
        }
        if (argv[i][0] != '-') {
            in_path = argv[i];
            continue;
        }
        return usage(argv[0]);
    }

    if (!in_path) {
        return usage(argv[0]);
    }
    if (flat_parse_file(in_path, &prog, err, sizeof err) != 0) {
        fprintf(stderr, "flc: parse error: %s\n", err);
        return 1;
    }
    if (dump_ir) {
        flat_fgir_dump(&prog, stdout);
        return 0;
    }
    if (!out_path) {
        fprintf(stderr, "flc: missing -o out.fb\n");
        return 1;
    }
    if (flat_compile_fgir(&prog, out_path, err, sizeof err) != 0) {
        fprintf(stderr, "flc: compile error: %s\n", err);
        return 1;
    }
    return 0;
}
