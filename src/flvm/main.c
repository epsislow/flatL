#include "fb_format.h"
#include "flat.h"
#include "flat_native_plugin.h"
#include "flat_time.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_version(void) {
    printf("flvm %s (flatL VM)\n", FLAT_TOOLCHAIN_VERSION);
}

static int usage(const char *prog) {
    fprintf(stderr, "Usage: %s --version\n", prog);
    fprintf(stderr, "       %s [--trace] [--trace-vars] [--max-steps N] [--time] program.fb\n", prog);
    fprintf(stderr, "\n  --trace       log each opcode to stderr\n");
    fprintf(stderr, "  --trace-vars  log opcode + variable snapshot to stderr\n");
    fprintf(stderr, "  --time        print load/run timings on stderr (flvm: time_ms …)\n");
    fprintf(stderr, "  FLAT_TIME=1   same as --time when flag omitted\n");
    fprintf(stderr, "  --max-jumps N cap OP_JUMP count (default %u; 0 = default)\n",
            (unsigned)FLAT_VM_DEFAULT_MAX_JUMPS);
    fprintf(stderr, "  --no-jump-limit disable jump cap\n");
    fprintf(stderr, "  --max-steps N cap total opcode dispatches (0 = unlimited)\n");
    return 2;
}

static void flvm_print_time(double load_ms, double run_ms, int exit_code, double total_ms) {
    char body[256];

    flat_time_line_begin(body, sizeof body);
    flat_time_append_field(body, sizeof body, "load", load_ms);
    flat_time_append_field(body, sizeof body, "run", run_ms);
    flat_time_append_int_field(body, sizeof body, "exit", exit_code);
    flat_time_append_field(body, sizeof body, "total", total_ms);
    flat_time_fprintf_time_ms("flvm", body);
}

int main(int argc, char **argv) {
    FlatBytecode bc;
    FlatVmOptions opts = {0};
    const char *fb_path = NULL;
    int report_time = 0;
    int rc;
    int i;
    double t0 = 0.0;
    double t_load = 0.0;
    double t_run = 0.0;

    report_time = flat_time_env_enabled();

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
        if (strcmp(argv[i], "--time") == 0) {
            report_time = 1;
            continue;
        }
        if (strcmp(argv[i], "--max-steps") == 0) {
            if (i + 1 >= argc) {
                return usage(argv[0]);
            }
            opts.max_steps = (uint64_t)strtoull(argv[++i], NULL, 10);
            continue;
        }
        if (strcmp(argv[i], "--max-jumps") == 0) {
            if (i + 1 >= argc) {
                return usage(argv[0]);
            }
            opts.max_jumps = (uint64_t)strtoull(argv[++i], NULL, 10);
            continue;
        }
        if (strcmp(argv[i], "--no-jump-limit") == 0) {
            opts.no_jump_limit = 1;
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

    if (report_time) {
        t0 = flat_time_monotonic_ms();
    }

    if (flat_fb_load_file(fb_path, &bc) != 0) {
        if (report_time) {
            double t_end = flat_time_monotonic_ms();
            flvm_print_time(0.0, 0.0, 1, t_end - t0);
        }
        return 1;
    }
    if (report_time) {
        t_load = flat_time_monotonic_ms();
    }

    rc = flat_vm_run_opts(&bc, &opts);

    if (report_time) {
        t_run = flat_time_monotonic_ms();
        flvm_print_time(t_load - t0, t_run - t_load, rc, t_run - t0);
    }

    flat_fb_free(&bc);
    return rc;
}
