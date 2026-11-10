#include "fb_format.h"
#include "fgir.h"
#include "flat.h"
#include "flat_link.h"
#include "flat_port.h"
#include "flat_native_plugin.h"
#include "flat_time.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_version(void) {
    printf("flatl %s (flatL driver)\n", FLAT_TOOLCHAIN_VERSION);
}

static int top_usage(const char *prog) {
    fprintf(stderr, "Usage: %s --version\n", prog);
    fprintf(stderr, "       %s compile [--verbose] [--time] -o out.fb program.fl\n", prog);
    fprintf(stderr, "       %s run [--trace] [--trace-vars] [--time] program.fb\n", prog);
    fprintf(stderr, "       %s crun [--trace] [--trace-vars] [--time] [-o out.fb] program.fl\n", prog);
    fprintf(stderr, "\n  --time        print timings on stderr (flatl: time_ms …); FLAT_TIME=1 equivalent\n");
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

static void flatl_print_time_line(const char *body) {
    flat_time_fprintf_time_ms("flatl", body);
}

static int compile_fl_to_fb(const char *in_path, const char *out_path, int verbose,
                            int report_time, double *compile_ms_out, double *link_ms_out,
                            FlatLinkReport *report_out, char *err, size_t err_size) {
    FgirProgram *prog;
    FlatLinkOptions link_opts = {0};
    FlatLinkReport link_report = {0};
    int rc = 1;
    double t0 = 0.0;
    double t_link = 0.0;

    link_opts.verbose = verbose;
    prog = calloc(1, sizeof(FgirProgram));
    if (!prog) {
        snprintf(err, err_size, "out of memory");
        return 1;
    }
    if (report_time) {
        t0 = flat_time_monotonic_ms();
    }
    if (flat_link_program(in_path, &link_opts, prog, &link_report, err, err_size) != 0) {
        goto done;
    }
    if (report_time) {
        t_link = flat_time_monotonic_ms();
        if (link_ms_out) {
            *link_ms_out = t_link - t0;
        }
    }
    if (flat_compile_fgir(prog, out_path, err, err_size) != 0) {
        goto done;
    }
    rc = 0;
    if (report_time) {
        double t_end = flat_time_monotonic_ms();
        if (compile_ms_out) {
            *compile_ms_out = t_end - t_link;
        }
    }
    if (report_out) {
        *report_out = link_report;
    }
done:
    free(prog);
    return rc;
}

static int run_fb_path(const char *fb_path, const FlatVmOptions *opts, int report_time, int *exit_out) {
    FlatBytecode bc;
    int rc;
    double t0 = 0.0;
    double t_load = 0.0;
    double t_run = 0.0;

    if (report_time) {
        t0 = flat_time_monotonic_ms();
    }
    if (flat_fb_load_file(fb_path, &bc) != 0) {
        fprintf(stderr, "flatl: failed to load bytecode: %s\n", fb_path);
        if (report_time) {
            char body[256];
            double t_end = flat_time_monotonic_ms();
            flat_time_line_begin(body, sizeof body);
            flat_time_append_field(body, sizeof body, "load", 0.0);
            flat_time_append_field(body, sizeof body, "run", 0.0);
            flat_time_append_int_field(body, sizeof body, "exit", 1);
            flat_time_append_field(body, sizeof body, "total", t_end - t0);
            flatl_print_time_line(body);
        }
        return 1;
    }
    if (report_time) {
        t_load = flat_time_monotonic_ms();
    }
    rc = flat_vm_run_opts(&bc, opts);
    flat_fb_free(&bc);
    if (report_time) {
        char body[256];
        t_run = flat_time_monotonic_ms();
        flat_time_line_begin(body, sizeof body);
        flat_time_append_field(body, sizeof body, "load", t_load - t0);
        flat_time_append_field(body, sizeof body, "run", t_run - t_load);
        flat_time_append_int_field(body, sizeof body, "exit", rc);
        flat_time_append_field(body, sizeof body, "total", t_run - t0);
        flatl_print_time_line(body);
    }
    if (exit_out) {
        *exit_out = rc;
    }
    return rc;
}

static int parse_run_flags(int *argc, char ***argv, FlatVmOptions *opts, int *report_time) {
    char **a = *argv;
    int n = *argc;
    int w = 0;
    int i;

    if (report_time && !*report_time) {
        *report_time = flat_time_env_enabled();
    }
    memset(opts, 0, sizeof(*opts));
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
        if (strcmp(a[i], "--time") == 0) {
            if (report_time) {
                *report_time = 1;
            }
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
    char body[512];
    int verbose = 0;
    int report_time = 0;
    int i;
    double link_ms = 0.0;
    double compile_ms = 0.0;
    FlatLinkReport report = {0};

    report_time = flat_time_env_enabled();
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
            continue;
        }
        if (strcmp(argv[i], "--time") == 0) {
            report_time = 1;
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
    if (compile_fl_to_fb(in_path, out_path, verbose, report_time, &compile_ms, &link_ms, &report,
                         err, sizeof err) != 0) {
        fprintf(stderr, "flatl compile: %s\n", err);
        if (report_time) {
            flat_time_line_begin(body, sizeof body);
            flat_time_append_field(body, sizeof body, "link", link_ms);
            flat_time_append_field(body, sizeof body, "compile", compile_ms);
            flat_time_append_field(body, sizeof body, "total", link_ms + compile_ms);
            flat_time_append_int_field(body, sizeof body, "units", report.units);
            flat_time_append_int_field(body, sizeof body, "fmod", report.fmod);
            flatl_print_time_line(body);
        }
        return 1;
    }
    if (report_time) {
        flat_time_line_begin(body, sizeof body);
        flat_time_append_field(body, sizeof body, "link", link_ms);
        flat_time_append_field(body, sizeof body, "compile", compile_ms);
        flat_time_append_field(body, sizeof body, "total", link_ms + compile_ms);
        flat_time_append_int_field(body, sizeof body, "units", report.units);
        flat_time_append_int_field(body, sizeof body, "fmod", report.fmod);
        flatl_print_time_line(body);
    }
    return 0;
}

static int cmd_run(int argc, char **argv) {
    FlatVmOptions opts;
    const char *fb_path = NULL;
    int report_time = 0;
    int i;

    parse_run_flags(&argc, &argv, &opts, &report_time);
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
    return run_fb_path(fb_path, &opts, report_time, NULL);
}

static int cmd_crun(int argc, char **argv) {
    FlatVmOptions opts;
    const char *in_path = NULL;
    const char *out_path = NULL;
    char default_fb[FLAT_PORT_PATH_MAX];
    char err[512];
    char body[512];
    int report_time = 0;
    int run_rc = 0;
    int i;
    double crun_t0 = 0.0;
    double compile_ms = 0.0;
    double link_ms = 0.0;
    double run_ms = 0.0;
    FlatLinkReport report = {0};

    parse_run_flags(&argc, &argv, &opts, &report_time);
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

    if (report_time) {
        crun_t0 = flat_time_monotonic_ms();
    }

    if (compile_fl_to_fb(in_path, out_path, 0, report_time, &compile_ms, &link_ms, &report, err,
                         sizeof err) != 0) {
        fprintf(stderr, "flatl crun: %s\n", err);
        if (report_time) {
            double t_end = flat_time_monotonic_ms();
            flat_time_line_begin(body, sizeof body);
            flat_time_append_field(body, sizeof body, "compile", link_ms + compile_ms);
            flat_time_append_field(body, sizeof body, "run", 0.0);
            flat_time_append_field(body, sizeof body, "total", t_end - crun_t0);
            flat_time_append_int_field(body, sizeof body, "units", report.units);
            flat_time_append_int_field(body, sizeof body, "fmod", report.fmod);
            flatl_print_time_line(body);
        }
        return 1;
    }
    fprintf(stderr, "flatl: wrote %s\n", out_path);

    {
        FlatBytecode bc;
        double compile_end = 0.0;

        if (report_time) {
            compile_end = flat_time_monotonic_ms();
            compile_ms = compile_end - crun_t0;
        }
        if (flat_fb_load_file(out_path, &bc) != 0) {
            fprintf(stderr, "flatl: failed to load bytecode: %s\n", out_path);
            return 1;
        }
        run_rc = flat_vm_run_opts(&bc, &opts);
        flat_fb_free(&bc);
        if (report_time) {
            double t_end = flat_time_monotonic_ms();
            run_ms = t_end - compile_end;
            flat_time_line_begin(body, sizeof body);
            flat_time_append_field(body, sizeof body, "compile", compile_ms);
            flat_time_append_field(body, sizeof body, "run", run_ms);
            flat_time_append_field(body, sizeof body, "total", t_end - crun_t0);
            flat_time_append_int_field(body, sizeof body, "units", report.units);
            flat_time_append_int_field(body, sizeof body, "fmod", report.fmod);
            flat_time_append_int_field(body, sizeof body, "exit", run_rc);
            flatl_print_time_line(body);
        }
    }
    return run_rc;
}

int main(int argc, char **argv) {
    int i;

    flat_native_runtime_set_search_dir_from_argv0(argv[0]);
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
