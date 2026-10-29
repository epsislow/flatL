#include "fb_format.h"
#include "fgir.h"
#include "flat.h"
#include "flat_link.h"
#include "flat_port.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_version(void) {
    printf("flatl %s (flatL driver)\n", FLAT_TOOLCHAIN_VERSION);
}

static int top_usage(const char *prog) {
    fprintf(stderr, "Usage: %s --version\n", prog);
    fprintf(stderr, "       %s compile [--verbose] -o out.fb program.fl\n", prog);
    fprintf(stderr, "       %s run [--trace] [--trace-vars] program.fb\n", prog);
    fprintf(stderr, "       %s crun [--trace] [--trace-vars] [-o out.fb] program.fl\n", prog);
    return 2;
}

static int path_has_suffix(const char *path, const char *suffix) {
    size_t plen;
    size_t slen;

    if (!path || !suffix) {
        return 0;
    }
    plen = strlen(path);
    slen = strlen(suffix);
    if (plen < slen) {
        return 0;
    }
    return strcmp(path + plen - slen, suffix) == 0;
}

static int compile_fl_to_fb(const char *in_path, const char *out_path, int verbose, char *err,
                            size_t err_size) {
    FgirProgram *prog;
    FlatLinkOptions link_opts = {0};
    int rc = 1;

    link_opts.verbose = verbose;
    prog = calloc(1, sizeof(FgirProgram));
    if (!prog) {
        snprintf(err, err_size, "out of memory");
        return 1;
    }
    if (flat_link_program(in_path, &link_opts, prog, err, err_size) != 0) {
        goto done;
    }
    if (flat_compile_fgir(prog, out_path, err, err_size) != 0) {
        goto done;
    }
    rc = 0;
done:
    free(prog);
    return rc;
}

static int run_fb_path(const char *fb_path, const FlatVmOptions *opts) {
    FlatBytecode bc;
    int rc;

    if (flat_fb_load_file(fb_path, &bc) != 0) {
        fprintf(stderr, "flatl: failed to load bytecode: %s\n", fb_path);
        return 1;
    }
    rc = flat_vm_run_opts(&bc, opts);
    flat_fb_free(&bc);
    return rc;
}

static int parse_vm_flags(int *argc, char ***argv, FlatVmOptions *opts) {
    char **a = *argv;
    int n = *argc;
    int w = 0;
    int i;

    opts->trace = 0;
    opts->trace_vars = 0;
    for (i = 0; i < n; i++) {
        if (strcmp(a[i], "--trace") == 0) {
            opts->trace = 1;
            continue;
        }
        if (strcmp(a[i], "--trace-vars") == 0) {
            opts->trace = 1;
            opts->trace_vars = 1;
            continue;
        }
        a[w++] = a[i];
    }
    *argc = w;
    *argv = a;
    return 0;
}

static int cmd_compile(int argc, char **argv) {
    const char *in_path = NULL;
    const char *out_path = NULL;
    char err[512];
    int verbose = 0;
    int i;

    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "flatl compile: unknown option: %s\n", argv[i]);
            return 2;
        }
        in_path = argv[i];
    }
    if (!in_path || !out_path) {
        fprintf(stderr, "Usage: flatl compile -o out.fb program.fl\n");
        return 2;
    }
    if (!path_has_suffix(in_path, ".fl")) {
        fprintf(stderr, "flatl compile: expected a .fl source file\n");
        return 2;
    }
    if (compile_fl_to_fb(in_path, out_path, verbose, err, sizeof err) != 0) {
        fprintf(stderr, "flatl compile: %s\n", err);
        return 1;
    }
    return 0;
}

static int cmd_run(int argc, char **argv) {
    FlatVmOptions opts;
    const char *fb_path = NULL;
    int i;

    parse_vm_flags(&argc, &argv, &opts);
    for (i = 0; i < argc; i++) {
        if (argv[i][0] == '-') {
            fprintf(stderr, "flatl run: unknown option: %s\n", argv[i]);
            return 2;
        }
        fb_path = argv[i];
    }
    if (!fb_path) {
        fprintf(stderr, "Usage: flatl run [--trace] program.fb\n");
        return 2;
    }
    if (path_has_suffix(fb_path, ".fl")) {
        fprintf(stderr, "flatl run: expected a .fb file (use flatl crun for .fl sources)\n");
        return 2;
    }
    return run_fb_path(fb_path, &opts);
}

static int cmd_crun(int argc, char **argv) {
    FlatVmOptions opts;
    const char *in_path = NULL;
    const char *out_path = NULL;
    char default_fb[FLAT_PORT_PATH_MAX];
    char err[512];
    int i;
    parse_vm_flags(&argc, &argv, &opts);
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "flatl crun: unknown option: %s\n", argv[i]);
            return 2;
        }
        in_path = argv[i];
    }
    if (!in_path) {
        fprintf(stderr, "Usage: flatl crun [-o out.fb] program.fl\n");
        return 2;
    }
    if (!path_has_suffix(in_path, ".fl")) {
        fprintf(stderr, "flatl crun: expected a .fl source file\n");
        return 2;
    }
    if (!out_path) {
        if (flat_port_make_temp_fb_path(default_fb, sizeof default_fb, NULL) != 0) {
            fprintf(stderr, "flatl crun: no writable temp directory\n");
            return 1;
        }
        out_path = default_fb;
    }
    if (compile_fl_to_fb(in_path, out_path, 0, err, sizeof err) != 0) {
        fprintf(stderr, "flatl crun: %s\n", err);
        return 1;
    }
    fprintf(stderr, "flatl: wrote %s\n", out_path);
    return run_fb_path(out_path, &opts);
}

int main(int argc, char **argv) {
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            print_version();
            return 0;
        }
    }

    if (argc < 2) {
        return top_usage(argv[0]);
    }

    if (strcmp(argv[1], "compile") == 0) {
        return cmd_compile(argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "run") == 0) {
        return cmd_run(argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "crun") == 0) {
        return cmd_crun(argc - 2, argv + 2);
    }

    fprintf(stderr, "flatl: unknown subcommand: %s\n", argv[1]);
    return top_usage(argv[0]);
}
